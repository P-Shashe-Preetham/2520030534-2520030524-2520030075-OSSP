/**
 * @file process.c
 * @brief Process table scanner, PCB inspection, sorting, and worker thread.
 *
 * Course: Operating Systems and Systems Programming (25CS2104E) - Term-I
 * Team: 18 | Section: 6
 * Team Member Responsibility: PAPAISETTY SHASHE PREETHAM (2520030534)
 *
 * Course Outcomes Addressed:
 * - CO1: Low-level file I/O (open, read, close) for kernel state inspection.
 * - CO2: Process lifecycle, Process Control Block (PCB) attributes, parent-child hierarchies
 *        (PID, PPID), and detection of process state transitions (Running, Sleeping, Zombie, Stopped).
 * - CO4: Virtual memory breakdown (VIRT virtual address space size, RES physical memory footprint)
 *        via /proc/[pid]/statm and /proc/[pid]/status.
 * - CO5: Virtual File System (VFS) traversal via opendir(), readdir(), closedir().
 * - CO6: Concurrency and thread synchronization using POSIX mutex locking.
 *
 * Abstract Requirements Met:
 * - Scans /proc/[pid]/stat and /proc/[pid]/status directly using unbuffered I/O.
 * - Provides dynamic process table sorting (by Memory RES, PID, or Name) on request.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE // Exposes POSIX directory entry type constants like DT_DIR
#endif
#include "monitor.h"
#include "process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>

/**
 * @brief Checks if a given directory name string consists entirely of digits.
 *
 * In the Linux /proc filesystem, every active process is represented by a directory
 * whose name is its numeric Process Identifier (PID). Named entries like /proc/sys,
 * /proc/net, or /proc/cpuinfo are non-numeric system directories.
 *
 * @param str The string (directory name) to evaluate.
 * @return int Returns 1 if all characters are decimal digits, 0 if null, empty, or non-digit.
 */
static int is_numeric(const char* str) {
    // A null pointer or empty string is not a valid numeric PID
    if (!str || !*str) return 0;
    
    // Iterate through each character in the string
    while (*str) {
        // Cast to unsigned char to avoid undefined behavior with negative values
        if (!isdigit((unsigned char)*str)) {
            return 0; // Found non-digit character (e.g. 'c' in "cpuinfo")
        }
        str++; // Advance to the next character
    }
    return 1; // All characters verified as digits
}

/**
 * @brief Reads process memory consumption (virtual & resident) from /proc/[pid]/statm.
 *
 * /proc/[pid]/statm provides memory metrics in units of system memory pages:
 * - Field 1: Total virtual memory size (total program size in pages)
 * - Field 2: Resident set size (pages in physical RAM)
 * - Field 3: Shared pages (pages backed by files or shared memory)
 * - Field 4: Text code pages
 * - Field 5: Library pages
 * - Field 6: Data + Stack pages
 * - Field 7: Dirty pages
 *
 * @param pid_str String representation of the PID (e.g. "1240").
 * @param[out] vsize_kb Pointer to store calculated virtual memory size in kB.
 * @param[out] rss_kb Pointer to store calculated physical resident memory in kB.
 * @param page_size_kb System memory page size in kB (typically 4 kB).
 */
static void read_process_memory(const char* pid_str, unsigned long* vsize_kb, unsigned long* rss_kb, long page_size_kb) {
    char path[256];
    char buf[128];
    *vsize_kb = 0; // Initialize output to zero
    *rss_kb = 0;   // Initialize output to zero

    // Construct the absolute path: /proc/<pid>/statm
    snprintf(path, sizeof(path), "/proc/%s/statm", pid_str);

    // Open statm using direct unbuffered system call (CO1, CO5)
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        // Read memory pages from kernel buffer
        ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
        close(fd); // Promptly close file descriptor to prevent descriptor exhaustion

        if (bytes > 0) {
            buf[bytes] = '\0'; // Null-terminate the string buffer
            unsigned long pages_total = 0, pages_rss = 0;
            
            // Extract total virtual pages and resident physical pages
            if (sscanf(buf, "%lu %lu", &pages_total, &pages_rss) >= 2) {
                // Convert page count to kilobytes (Pages * PageSizeInKB)
                *vsize_kb = pages_total * (unsigned long)page_size_kb;
                *rss_kb = pages_rss * (unsigned long)page_size_kb;
            }
        }
    }
}

/**
 * @brief Reads /proc/[pid]/status to extract thread count and verify memory (Abstract requirement).
 *
 * While /proc/[pid]/stat provides space-delimited counters, /proc/[pid]/status
 * provides human-readable key-value pairs formatted by the kernel.
 * Here we extract the "Threads:" attribute representing active POSIX threads in the process.
 *
 * @param pid_str String representation of the PID.
 * @param[out] threads Pointer to store the number of active threads.
 */
static void read_process_status(const char* pid_str, int* threads) {
    char path[256];
    char buf[1024];
    *threads = 1; // Default to single-threaded if not found

    // Construct the path: /proc/<pid>/status
    snprintf(path, sizeof(path), "/proc/%s/status", pid_str);

    // Open status file with low-level unbuffered system call
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
        close(fd); // Always close the file descriptor

        if (bytes > 0) {
            buf[bytes] = '\0';
            
            // Tokenize buffer line by line
            char* line = strtok(buf, "\n");
            while (line != NULL) {
                // Search for the "Threads:" key (e.g. "Threads: 4")
                if (strncmp(line, "Threads:", 8) == 0) {
                    sscanf(line, "Threads: %d", threads);
                    break; // Extracted target field, stop parsing
                }
                line = strtok(NULL, "\n"); // Move to next line
            }
        }
    }
}

/**
 * @brief Comparator for sorting processes by Resident Physical Memory (RES) descending.
 * Used by qsort() to position the highest memory consumers at the top of the table.
 */
static int cmp_by_memory(const void* a, const void* b) {
    const ProcessInfo* pa = (const ProcessInfo*)a;
    const ProcessInfo* pb = (const ProcessInfo*)b;
    // Descending order: return > 0 if pb > pa
    if (pb->mem_rss_kb > pa->mem_rss_kb) return 1;
    if (pb->mem_rss_kb < pa->mem_rss_kb) return -1;
    return 0; // Equal memory usage
}

/**
 * @brief Comparator for sorting processes by PID ascending.
 * Used by qsort() to order processes numerically by Process ID.
 */
static int cmp_by_pid(const void* a, const void* b) {
    const ProcessInfo* pa = (const ProcessInfo*)a;
    const ProcessInfo* pb = (const ProcessInfo*)b;
    // Ascending order: pa->pid - pb->pid
    return pa->pid - pb->pid;
}

/**
 * @brief Comparator for sorting processes alphabetically by command name.
 * Used by qsort() to order processes from A to Z ignoring case.
 */
static int cmp_by_name(const void* a, const void* b) {
    const ProcessInfo* pa = (const ProcessInfo*)a;
    const ProcessInfo* pb = (const ProcessInfo*)b;
    // Case-insensitive string comparison
    return strcasecmp(pa->name, pb->name);
}

/**
 * @brief Worker thread routine that continually scans /proc to collect active process metadata.
 *
 * Execution Journey:
 * 1. Opens the /proc Virtual File System directory using opendir().
 * 2. Iterates over directory entries using readdir().
 * 3. Identifies numeric directory names representing live PIDs.
 * 4. Parses /proc/[pid]/stat for PID, PPID, execution state, and command name.
 * 5. Parses /proc/[pid]/statm for VIRT and RES memory consumption.
 * 6. Parses /proc/[pid]/status for thread counts.
 * 7. Categorizes process state transitions (R, S, Z, T).
 * 8. Dynamically sorts the process table using qsort() according to active sort mode.
 * 9. Batch updates the global MonitorData structure inside a mutex critical section.
 * 10. Signals the condition variable to wake up the terminal display worker.
 *
 * @param arg Unused thread argument (NULL).
 * @return void* NULL upon thread termination.
 */
void* process_worker(void* arg) {
    (void)arg; // Silence unused parameter warning
    
    // Allocate local process array to collect data outside the critical section,
    // thereby keeping the mutex lock hold time as short as possible (CO6)
    ProcessInfo local_procs[MAX_PROCESSES];
    char filepath[512];
    char buffer[1024];

    // Determine the kernel memory page size (typically 4096 bytes on x86_64)
    long page_size_bytes = sysconf(_SC_PAGESIZE);
    long page_size_kb = (page_size_bytes > 0) ? (page_size_bytes / 1024) : 4;

    // Loop until the main thread or signal handler sets keep_running to 0
    while (monitor_data.keep_running) {
        // Open the /proc directory stream (CO5: Virtual File System Directory Iteration)
        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) {
            perror("Error opening /proc directory stream");
            sleep(2);
            continue;
        }

        struct dirent* entry;
        int count = 0;
        int r_count = 0, s_count = 0, z_count = 0, t_count = 0;

        // Iterate through all entries in /proc up to maximum capacity
        while ((entry = readdir(proc_dir)) != NULL && count < MAX_PROCESSES) {
            // Check if the directory entry is a directory and has a purely numeric name (PID)
            if (entry->d_type == DT_DIR && is_numeric(entry->d_name)) {
                // Construct the path to the process status file: /proc/<pid>/stat
                snprintf(filepath, sizeof(filepath), "/proc/%s/stat", entry->d_name);
                
                // Open file using low-level unbuffered system call (CO1, CO5)
                int fd = open(filepath, O_RDONLY);
                if (fd >= 0) {
                    // Read file contents into stack buffer
                    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
                    close(fd); // Close descriptor immediately after reading

                    if (bytes > 0) {
                        buffer[bytes] = '\0'; // Null-terminate raw string
                        
                        int pid, ppid;
                        char comm[256];
                        char state;
                        
                        // /proc/[pid]/stat format:
                        // pid (filename/comm) state ppid pgrp session tty_nr ...
                        // Because executable names can contain parentheses or spaces,
                        // find the first '(' and the last ')' to safely isolate comm.
                        char* comm_start = strchr(buffer, '(');
                        char* comm_end = strrchr(buffer, ')');
                        
                        if (comm_start && comm_end && comm_end > comm_start) {
                            // Convert the string prefix before '(' into the numeric PID
                            pid = atoi(buffer);
                            
                            // Calculate length of the executable name inside the parentheses
                            size_t comm_len = comm_end - comm_start - 1;
                            if (comm_len >= sizeof(comm)) comm_len = sizeof(comm) - 1;
                            
                            // Copy isolated command name safely
                            strncpy(comm, comm_start + 1, comm_len);
                            comm[comm_len] = '\0';
                            
                            // Parse process state character and Parent PID (PPID) after ')'
                            // Format: ") <state> <ppid>"
                            sscanf(comm_end + 2, "%c %d", &state, &ppid);
                            
                            // Categorize process state transitions into lifecycle buckets (CO2)
                            if (state == 'R') {
                                r_count++; // Running state
                            } else if (state == 'S' || state == 'D') {
                                s_count++; // Sleeping / Disk Wait state
                            } else if (state == 'Z') {
                                z_count++; // Zombie state: terminated but not reaped by parent
                            } else if (state == 'T' || state == 't') {
                                t_count++; // Stopped state: suspended by SIGSTOP or debugger
                            } else {
                                s_count++; // Fallback for idle/unknown states
                            }

                            // Read virtual memory and resident physical RAM via /proc/[pid]/statm (CO4)
                            unsigned long vsize_kb = 0, rss_kb = 0;
                            read_process_memory(entry->d_name, &vsize_kb, &rss_kb, page_size_kb);

                            // Read thread count via /proc/[pid]/status (Abstract requirement)
                            int threads = 1;
                            read_process_status(entry->d_name, &threads);

                            // Populate local process record
                            local_procs[count].pid = pid;
                            local_procs[count].ppid = ppid;
                            local_procs[count].state = state;
                            local_procs[count].mem_vsize_kb = vsize_kb;
                            local_procs[count].mem_rss_kb = rss_kb;
                            local_procs[count].threads_count = threads;
                            
                            // Copy command name into record ensuring null termination
                            strncpy(local_procs[count].name, comm, sizeof(local_procs[count].name) - 1);
                            local_procs[count].name[sizeof(local_procs[count].name) - 1] = '\0';
                            
                            count++; // Increment valid process count
                        }
                    }
                }
            }
        }
        
        // Close directory stream to release directory file descriptor (CO5)
        closedir(proc_dir);

        // Sort process listings based on current sort mode (Abstract requirement)
        // Sorting is performed locally before acquiring the lock to reduce lock contention
        ProcessSortMode current_sort = monitor_data.sort_mode;
        if (current_sort == SORT_BY_MEM) {
            qsort(local_procs, count, sizeof(ProcessInfo), cmp_by_memory);
        } else if (current_sort == SORT_BY_PID) {
            qsort(local_procs, count, sizeof(ProcessInfo), cmp_by_pid);
        } else if (current_sort == SORT_BY_NAME) {
            qsort(local_procs, count, sizeof(ProcessInfo), cmp_by_name);
        }

        // Critical section: acquire mutex to safely copy local batch into shared memory (CO6)
        pthread_mutex_lock(&monitor_data.lock);
        monitor_data.process_count = count;
        monitor_data.running_count = r_count;
        monitor_data.sleeping_count = s_count;
        monitor_data.zombie_count = z_count;
        monitor_data.stopped_count = t_count;
        
        // Bulk copy local array into shared monitor_data.processes buffer
        memcpy(monitor_data.processes, local_procs, count * sizeof(ProcessInfo));
        
        // Signal condition variable that process table has refreshed (CO6)
        pthread_cond_broadcast(&monitor_data.cond_data_ready);
        
        // Release the mutex lock immediately
        pthread_mutex_unlock(&monitor_data.lock);

        // Process scanning touches hundreds of files; sleep 2 seconds to keep CPU overhead negligible
        sleep(2);
    }
    return NULL;
}
