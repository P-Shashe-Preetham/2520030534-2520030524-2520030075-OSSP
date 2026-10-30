/**
 * @file main.c
 * @brief Application entry point and multi-thread lifecycle orchestrator.
 *
 * Course: Operating Systems and Systems Programming (25CS2104E) - Term-I
 * Section: 6 | Team No: 18
 * Project Title: Linux System Information and Resource Monitoring Tool
 * Team Members:
 *   - THYARLA ABHINAV (2520030075) - CPU, Memory & Disk stats (/proc parsing, file I/O)
 *   - J. ADVAITH (2520030524) - Multithreading, mutex synchronization, signals, integration
 *   - PAPAISETTY SHASHE PREETHAM (2520030534) - Process module, terminal display, logging, docs
 *
 * Course Outcomes Implemented:
 * - CO1: OS as a service layer, user-space argument handling, system call interfaces.
 * - CO2: Process hierarchy, process state monitoring (R, S, Z, T), and signal dispatching.
 * - CO3: IPC via Named Pipes (FIFOs) and POSIX Signals (SIGINT, SIGTERM, SIGUSR1).
 * - CO4: Virtual memory abstraction, address space footprint (VIRT, RES), and swap statistics.
 * - CO5: File system abstractions, Inode inspection (stat), and memory-mapped file I/O (mmap).
 * - CO6: Concurrency via POSIX Threads, mutual exclusion (pthread_mutex_t), condition variables
 *        (pthread_cond_t), and bounded-buffer producer-consumer counting semaphores (sem_t).
 */
// Core monitor interfaces
#include "monitor.h"
#include "cpu.h"
#include "memory.h"
#include "disk.h"
#include "process.h"
#include "display.h"
#include "logger.h"
#include "signals.h"
#include "ipc.h"
#include "snapshot.h"
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/**
 * @brief Global shared data structure containing system stats, process list,
 * mutex locks, condition variables, counting semaphores, and lifecycle flags.
 */
MonitorData monitor_data;

/**
 * @brief Initializes the global MonitorData structure and synchronization primitives.
 *
 * Steps:
 * 1. Zeroes out entire memory region of monitor_data using memset().
 * 2. Initializes the POSIX mutex lock (pthread_mutex_init) with default attributes.
 * 3. Initializes the POSIX condition variable (pthread_cond_init) for worker-to-display signaling.
 * 4. Sets initial process sort mode to SORT_BY_MEM (Physical Memory RES descending).
 * 5. Sets execution flag `keep_running = 1`.
 */
void init_monitor_data(void) {
    // Clear structure memory to avoid uninitialized garbage values
    memset(&monitor_data, 0, sizeof(MonitorData));
    
    // CO6: Initialize POSIX Mutex for mutual exclusion across worker threads
    pthread_mutex_init(&monitor_data.lock, NULL);
    
    // CO6: Initialize POSIX Condition Variable for coordinated thread wakeup without busy-waiting
    pthread_cond_init(&monitor_data.cond_data_ready, NULL);
    
    // Default sorting mode: Physical Memory (RES) descending (Abstract requirement)
    monitor_data.sort_mode = SORT_BY_MEM;

    // Set lifecycle control flags
    monitor_data.keep_running = 1;     // 1 indicates active execution; set to 0 on exit
    monitor_data.is_paused = 0;        // 1 indicates live UI display updates are frozen
    monitor_data.trigger_snapshot = 0; // 1 signals display worker to dump mmap snapshot
}

/**
 * @brief Cleans up and releases synchronization resources in MonitorData.
 *
 * Destroys the condition variable and mutex to return resources to the OS kernel.
 */
void destroy_monitor_data(void) {
    // CO6: Destroy condition variable and mutex
    pthread_cond_destroy(&monitor_data.cond_data_ready);
    pthread_mutex_destroy(&monitor_data.lock);
}

/**
 * @brief Main entry point of the resource monitoring utility.
 *
 * Execution Journey:
 * 1. Ensures directory layout exists (mkdir "logs").
 * 2. Initializes shared memory, mutexes, condition variables, and semaphores.
 * 3. Parses command-line arguments (--sort, --snapshot, --no-log, --help).
 * 4. Initializes logging subsystem and Named Pipe IPC channel.
 * 5. Registers POSIX signal handlers (SIGINT, SIGTERM, SIGUSR1).
 * 6. Spawns 7 concurrent POSIX threads:
 *    - cpu_worker
 *    - memory_worker
 *    - disk_worker
 *    - process_worker
 *    - display_worker
 *    - ipc_worker
 *    - logger_consumer_worker
 * 7. Waits for threads to finish via pthread_join().
 * 8. Cleans up Named Pipe node, destroys mutexes, and exits gracefully.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of argument strings.
 * @return int 0 on success, non-zero on error.
 */
int main(int argc, char *argv[]) {
    int use_logging = 1; // Default to enabled for bounded buffer demonstration
    const char* log_file = "logs/monitor.log";

    // Ensure logs directory exists with 0755 permissions (CO5: Directory Management)
    mkdir("logs", 0755);

    // Initialize core shared state, mutexes, condition variables, and semaphores
    init_monitor_data();

    // Parse command line arguments (CO1: User-Space Argument Processing)
    for (int i = 1; i < argc; i++) {
        // Option to disable logging
        if (strcmp(argv[i], "--no-log") == 0) {
            use_logging = 0;
        } 
        // Option to configure initial process sorting mode (Abstract requirement)
        else if (strcmp(argv[i], "--sort") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "pid") == 0) {
                monitor_data.sort_mode = SORT_BY_PID;
            } else if (strcmp(argv[i], "name") == 0) {
                monitor_data.sort_mode = SORT_BY_NAME;
            } else {
                monitor_data.sort_mode = SORT_BY_MEM;
            }
        } 
        // Option to generate an instant one-shot mmap snapshot and exit (CO5)
        else if (strcmp(argv[i], "--snapshot") == 0) {
            printf("Generating one-shot memory-mapped snapshot via mmap()...\n");
            get_cpu_usage(); // Seed initial CPU metrics
            sleep(1);
            get_cpu_usage();
            save_mmap_snapshot(SNAPSHOT_FILE);
            
            // Query and display VFS Inode information using stat() system call (CO5)
            unsigned long inode = 0;
            off_t size = 0;
            get_file_stat_info(SNAPSHOT_FILE, &inode, &size);
            printf("Snapshot successfully mapped and written to %s (Inode: %lu, Size: %ld B)\n",
                   SNAPSHOT_FILE, inode, (long)size);
            destroy_monitor_data();
            return 0;
        } 
        // Comprehensive help screen detailing Team 18 and all commands
        else if (strcmp(argv[i], "--help") == 0) {
            printf("====================================================================\n");
            printf("  Linux System Resource Monitor | Team 18 (Section 6)\n");
            printf("  Course: Operating Systems & Systems Programming (25CS2104E)\n");
            printf("====================================================================\n");
            printf("Team Members & Responsibilities:\n");
            printf("  - Thyarla Abhinav (2520030075)         : CPU/Memory/Disk (/proc)\n");
            printf("  - J. Advaith (2520030524)              : Pthreads, Mutex, Signals\n");
            printf("  - Papaisetty Shashe Preetham (2520030534): Process module, UI, Logging\n\n");
            printf("Usage: %s [OPTIONS]\n\n", argv[0]);
            printf("Options:\n");
            printf("  --snapshot       Generate a one-shot mmap() snapshot and exit\n");
            printf("  --sort <mem|pid|name>  Set initial process sorting criteria (default: mem)\n");
            printf("  --no-log         Disable asynchronous event logging to %s\n", log_file);
            printf("  --help           Display this comprehensive help message\n\n");
            printf("Interactive & IPC Commands (send to %s):\n", FIFO_PATH);
            printf("  echo 'PAUSE'         > %s  : Freeze live display\n", FIFO_PATH);
            printf("  echo 'RESUME'        > %s  : Unfreeze live display\n", FIFO_PATH);
            printf("  echo 'SNAPSHOT'      > %s  : Trigger mmap snapshot dump\n", FIFO_PATH);
            printf("  echo 'SORT MEM'      > %s  : Sort processes by memory\n", FIFO_PATH);
            printf("  echo 'SORT PID'      > %s  : Sort processes by PID\n", FIFO_PATH);
            printf("  echo 'SORT NAME'     > %s  : Sort processes alphabetically\n", FIFO_PATH);
            printf("  echo 'KILL <pid>'    > %s  : Send SIGTERM to process\n", FIFO_PATH);
            printf("  echo 'STOP <pid>'    > %s  : Send SIGSTOP to suspend process\n", FIFO_PATH);
            printf("  echo 'CONT <pid>'    > %s  : Send SIGCONT to resume process\n", FIFO_PATH);
            printf("  echo 'QUIT'          > %s  : Terminate monitor gracefully\n", FIFO_PATH);
            destroy_monitor_data();
            return 0;
        }
    }

    // Initialize logging subsystem with counting semaphore bounded queue (CO6)
    if (use_logging) {
        init_logger(log_file);
        log_event_produce("Linux Resource Monitor (Team 18) initialized successfully");
    }

    // Initialize Named Pipe (FIFO) for IPC (CO3)
    if (init_ipc(FIFO_PATH) == 0 && use_logging) {
        log_event_produce("Named Pipe IPC channel established at " FIFO_PATH);
    }

    // Register POSIX signal handlers (SIGINT, SIGTERM, SIGUSR1) via sigaction() (CO3)
    setup_signal_handlers();

    // Thread identifiers for concurrent workers (CO6: Multithreading)
    // 7 Concurrent POSIX Threads: CPU, Memory, Disk, Process, Display, IPC, and Logger Consumer
    pthread_t cpu_thread, mem_thread, disk_thread, proc_thread, disp_thread, ipc_thread, log_thread;

    // 1. Spawning CPU Worker Thread
    if (pthread_create(&cpu_thread, NULL, cpu_worker, NULL) != 0) {
        perror("Failed to create CPU worker thread");
        return 1;
    }

    // 2. Spawning Memory & Swap Worker Thread
    if (pthread_create(&mem_thread, NULL, memory_worker, NULL) != 0) {
        perror("Failed to create Memory worker thread");
        return 1;
    }

    // 3. Spawning Disk I/O Worker Thread (Abstract requirement)
    if (pthread_create(&disk_thread, NULL, disk_worker, NULL) != 0) {
        perror("Failed to create Disk worker thread");
        return 1;
    }

    // 4. Spawning Process Table Worker Thread
    if (pthread_create(&proc_thread, NULL, process_worker, NULL) != 0) {
        perror("Failed to create Process worker thread");
        return 1;
    }

    // 5. Spawning Terminal Display Worker Thread
    if (pthread_create(&disp_thread, NULL, display_worker, NULL) != 0) {
        perror("Failed to create Display worker thread");
        return 1;
    }

    // 6. Spawning Named Pipe (FIFO) IPC Worker Thread (CO3)
    if (pthread_create(&ipc_thread, NULL, ipc_worker, (void*)FIFO_PATH) != 0) {
        perror("Failed to create IPC worker thread");
        return 1;
    }

    // 7. Spawning Logging Consumer Worker with Counting Semaphores (CO6)
    if (use_logging) {
        if (pthread_create(&log_thread, NULL, logger_consumer_worker, NULL) != 0) {
            perror("Failed to create Logger consumer worker thread");
            return 1;
        }
    }

    // Join workers upon termination signal (CO6: POSIX Thread Synchronization)
    // The main thread waits for polling workers to complete their loop
    pthread_join(cpu_thread, NULL);
    pthread_join(mem_thread, NULL);
    pthread_join(disk_thread, NULL);
    pthread_join(proc_thread, NULL);

    // Wake up display worker if it is currently sleeping on the condition variable
    pthread_mutex_lock(&monitor_data.lock);
    pthread_cond_broadcast(&monitor_data.cond_data_ready);
    pthread_mutex_unlock(&monitor_data.lock);
    pthread_join(disp_thread, NULL);

    // Cancel and join the IPC thread in case it is blocked on FIFO open()
    pthread_cancel(ipc_thread);
    pthread_join(ipc_thread, NULL);

    // Join logger consumer thread if logging was enabled
    if (use_logging) {
        pthread_join(log_thread, NULL);
    }

    // Graceful Cleanup after all worker threads have terminated
    printf("\n[Monitor] Shutting down monitor gracefully...\n");
    if (use_logging) {
        log_message("Monitor stopped gracefully");
        close_logger();
    }
    
    // Remove Named Pipe (FIFO) node from the filesystem (CO3, CO5)
    cleanup_ipc(FIFO_PATH);

    // Destroy mutex and condition variables to release kernel resources (CO6)
    destroy_monitor_data();
    
    printf("[Monitor] Cleanup complete. All resources freed successfully.\n");
    return 0;
}
