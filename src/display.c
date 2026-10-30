/**
 * @file display.c
 * @brief Terminal dashboard user interface and display worker thread.
 *
 * Course: Operating Systems and Systems Programming (25CS2104E) - Term-I
 * Team: 18 | Section: 6
 * Team Member Responsibility: PAPAISETTY SHASHE PREETHAM (2520030534)
 *
 * Course Outcomes Addressed:
 * - CO1: Layered OS abstraction, user space execution, interactive terminal UI.
 * - CO2: Process lifecycle representation (Running, Sleeping, Zombie, Stopped).
 * - CO3: Remote IPC command indicators (PAUSE/RESUME/FIFO path).
 * - CO4: Virtual Memory metrics (VIRT address space size, RES physical RAM footprint, Swap).
 * - CO5: File system abstractions and Inode inspection via stat() system call.
 * - CO6: Thread synchronization using Condition Variables (pthread_cond_timedwait)
 *        and Mutexes (pthread_mutex_lock/unlock) to eliminate busy waiting.
 *
 * Abstract Requirements Met:
 * - Real-time terminal view displaying CPU, Memory, Disk I/O, and sorted process listings.
 *
 * Operating System Theory:
 * ANSI/VT100 escape sequences allow direct terminal display control without third-party
 * GUI dependencies:
 * - `\033[2J` instructs the terminal emulator to clear all displayed cells.
 * - `\033[H` moves the cursor to line 1, column 1 (home position).
 * Re-rendering from home overwrites the screen smoothly with zero flickering.
 */
// Core monitor interfaces
#include "monitor.h"
#include "display.h"
#include "snapshot.h"
#include "logger.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

/**
 * @brief Returns a human-readable string representation of the active sorting mode.
 *
 * @param mode ProcessSortMode enum value.
 * @return const char* Descriptive string label for UI rendering.
 */
static const char* get_sort_label(ProcessSortMode mode) {
    switch (mode) {
        case SORT_BY_MEM:  return "Physical Memory (RES) Descending";
        case SORT_BY_PID:  return "Process ID (PID) Ascending";
        case SORT_BY_NAME: return "Command Name (A-Z)";
        default:           return "Default";
    }
}

/**
 * @brief Worker thread routine that renders the live terminal monitoring UI.
 *
 * Execution Steps:
 * 1. Enters critical section and waits on pthread_cond_timedwait(&cond_data_ready) (CO6).
 *    This eliminates CPU-burning busy loops: the thread only wakes when workers produce new data!
 * 2. Checks if an on-demand snapshot was requested via SIGUSR1 or IPC.
 * 3. Checks if the display was paused via IPC command (`echo "PAUSE" > /tmp/ossp_monitor.fifo`).
 * 4. Takes a consistent copy of system metrics and the top 10 processes under mutex protection.
 * 5. Promptly unlocks the mutex so data-gathering worker threads are never blocked.
 * 6. Queries Inode metadata of the snapshot file via stat() (CO5).
 * 7. Emits VT100 escape sequences to clear and reposition cursor.
 * 8. Prints formatted dashboard showing CPU, RAM, Swap, Disk I/O, and process table.
 * 9. Flushes stdout using fflush() to ensure immediate presentation.
 *
 * @param arg Unused thread argument (NULL).
 * @return void* NULL upon thread exit.
 */
void* display_worker(void* arg) {
    (void)arg; // Silence unused parameter compiler warning
    
    // Continue running until SIGINT/SIGTERM or IPC QUIT sets keep_running to 0
    while (monitor_data.keep_running) {
        // Critical section: acquire mutex lock before checking conditions or waiting
        pthread_mutex_lock(&monitor_data.lock);

        // CO6: Wait on condition variable with a 1-second timeout
        // - Atomically releases monitor_data.lock and puts thread to sleep
        // - Wakes up when any worker thread calls pthread_cond_broadcast()
        // - Re-acquires monitor_data.lock upon waking
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        pthread_cond_timedwait(&monitor_data.cond_data_ready, &monitor_data.lock, &ts);

        // Check if an on-demand snapshot was requested via SIGUSR1 or IPC (CO3, CO5)
        if (monitor_data.trigger_snapshot) {
            monitor_data.trigger_snapshot = 0; // Reset atomic flag
            pthread_mutex_unlock(&monitor_data.lock); // Release lock before file I/O

            // Persist current state to disk using memory-mapped I/O (mmap)
            save_mmap_snapshot(SNAPSHOT_FILE);
            log_event_produce("Memory-mapped snapshot saved to logs/snapshot.dat");

            pthread_mutex_lock(&monitor_data.lock); // Re-acquire lock to resume display logic
        }

        // If user paused display via IPC command, do not re-render (CO3)
        if (monitor_data.is_paused) {
            pthread_mutex_unlock(&monitor_data.lock);
            sleep(1); // Sleep while paused
            continue; // Skip rendering
        }

        // Make local stack copies of all shared metrics while holding the lock
        SystemStats sys = monitor_data.sys_stats;
        int proc_count = monitor_data.process_count;
        int r_count = monitor_data.running_count;
        int s_count = monitor_data.sleeping_count;
        int z_count = monitor_data.zombie_count;
        int t_count = monitor_data.stopped_count;
        ProcessSortMode current_sort = monitor_data.sort_mode;
        
        // Extract up to 10 top processes for terminal display
        int display_count = proc_count > 10 ? 10 : proc_count;
        ProcessInfo procs[10];
        for (int i = 0; i < display_count; i++) {
            procs[i] = monitor_data.processes[i];
        }
        
        // Release the mutex lock promptly to minimize critical section latency (CO6)
        pthread_mutex_unlock(&monitor_data.lock);

        // CO5: Query Inode metadata of the snapshot file using stat() system call
        unsigned long snap_inode = 0;
        off_t snap_size = 0;
        get_file_stat_info(SNAPSHOT_FILE, &snap_inode, &snap_size);

        // VT100 ANSI escape codes:
        // \033[2J : Clears entire screen
        // \033[H  : Repositions cursor to row 1, column 1
        printf("\033[2J\033[H");
        
        // Print Team 18 Header Banner
        printf("================================================================================\n");
        printf("   LINUX SYSTEM RESOURCE MONITOR | Team 18 (Sec 6) | 25CS2104E (Term-I)         \n");
        printf("================================================================================\n");
        
        // Print CPU metrics: aggregate usage percentage and active online core count
        printf("CPU Utilization:  %5.1f%% (%d Cores Online)\n", sys.cpu_usage, sys.cpu_cores);
        
        // Print Physical RAM metrics: percentage, megabytes used, and total installed RAM
        printf("Physical RAM:     %5.1f%% (%lu MB used / %lu MB total)\n", 
               sys.mem_usage, sys.mem_used / 1024, sys.mem_total / 1024);
        
        // Print Virtual Swap metrics: percentage, free megabytes, and total swap configured
        printf("Virtual Swap:     %5.1f%% (%lu MB free / %lu MB total)\n",
               sys.swap_usage, sys.swap_free / 1024, sys.swap_total / 1024);
        
        // Print Live Disk / Storage I/O metrics (Abstract requirement): throughput & ops
        printf("Disk I/O Activity:%s | Read: %6.1f kB/s | Write: %6.1f kB/s (Total: %llu R / %llu W)\n",
               sys.disk.device_name[0] ? sys.disk.device_name : "disk",
               sys.disk.read_rate_kbps, sys.disk.write_rate_kbps,
               sys.disk.reads_completed, sys.disk.writes_completed);
        
        // Print Process Lifecycle State Counts (CO2: Running, Sleeping, Zombie, Stopped)
        printf("Processes:        Total: %d | Run: %d | Sleep: %d | Zombie: %d | Stop: %d\n",
               proc_count, r_count, s_count, z_count, t_count);
        
        // Print active process table sort criteria
        printf("Process Sorting:  %s\n", get_sort_label(current_sort));
        
        // Print IPC communication path
        printf("IPC Channel:      Named Pipe active at %s\n", FIFO_PATH);
        
        // Print VFS Inode information if snapshot file exists on disk (CO5)
        if (snap_inode > 0) {
            printf("VFS Inode (CO5):  Snapshot '%s' -> Inode: %lu (Size: %ld B)\n",
                   SNAPSHOT_FILE, snap_inode, (long)snap_size);
        }
        
        // Print Process Table Column Headers
        printf("--------------------------------------------------------------------------------\n");
        printf("%-7s %-7s %-5s %-4s %-12s %-12s %s\n", "PID", "PPID", "STATE", "THR", "VIRT (kB)", "RES (kB)", "COMMAND");
        printf("--------------------------------------------------------------------------------\n");
        
        // Render process table rows
        for (int i = 0; i < display_count; i++) {
            printf("%-7d %-7d %-5c %-4d %-12lu %-12lu %s\n", 
                   procs[i].pid, procs[i].ppid, procs[i].state, procs[i].threads_count,
                   procs[i].mem_vsize_kb, procs[i].mem_rss_kb, procs[i].name);
        }
        
        // Print Interactive Keyboard & IPC command instructions
        printf("================================================================================\n");
        printf("Controls: Ctrl+C=Exit | SIGUSR1=Snapshot | IPC: echo 'SORT PID' > %s\n", FIFO_PATH);
        printf("================================================================================\n");
        
        // Flush standard output buffer so changes are rendered instantaneously
        fflush(stdout);
    }
    
    return NULL;
}
