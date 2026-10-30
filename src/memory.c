/**
 * @file memory.c
 * @brief Memory & Swap statistics collector and worker thread.
 *
 * Course: Operating Systems and Systems Programming (25CS2104E) - Term-I
 * Team: 18 | Section: 6
 * Team Member Responsibility: THYARLA ABHINAV (2520030075)
 *
 * Course Outcomes Addressed:
 * - CO1: Direct system call interface usage (open, read, close).
 * - CO4: Virtual memory, memory management concepts, demand paging, and swap tracking.
 * - CO5: Virtual File System (VFS) abstraction reading /proc/meminfo.
 * - CO6: Thread synchronization using POSIX mutex and condition variable signaling.
 *
 * The Linux kernel exposes physical RAM and virtual swap statistics in /proc/meminfo.
 * This module parses MemTotal, MemAvailable, SwapTotal, and SwapFree using low-level
 * unbuffered system calls, computes memory utilization percentages, and alerts on memory pressure.
 */
// Core monitor interfaces
#include "monitor.h"
#include "memory.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/**
 * @brief Worker thread routine that continually polls and updates memory & swap statistics.
 *
 * Execution Steps:
 * 1. Opens /proc/meminfo using low-level open() system call (CO1, CO5).
 * 2. Reads file contents into stack buffer using read() system call.
 * 3. Closes file descriptor via close() system call.
 * 4. Parses MemTotal, MemAvailable, SwapTotal, SwapFree line-by-line using strtok().
 * 5. Calculates in-use RAM: (MemTotal - MemAvailable) and % utilization.
 * 6. Calculates in-use Swap: (SwapTotal - SwapFree) and % utilization (CO4).
 * 7. Enters mutex critical section (pthread_mutex_lock) and updates monitor_data (CO6).
 * 8. Broadcasts condition variable (pthread_cond_broadcast) to wake display thread.
 * 9. Exits critical section (pthread_mutex_unlock).
 * 10. Enqueues alert into semaphore bounded queue if RAM usage exceeds 90%.
 *
 * @param arg Unused thread argument (NULL).
 * @return void* NULL upon thread exit.
 */
void* memory_worker(void* arg) {
    (void)arg; // Silence unused parameter warning
    char buffer[2048]; // Stack buffer to hold /proc/meminfo text

    // Loop until global signal handler signals shutdown
    while (monitor_data.keep_running) {
        // Open /proc/meminfo using direct unbuffered system call (CO1, CO5)
        int fd = open("/proc/meminfo", O_RDONLY);
        if (fd >= 0) {
            // Read kernel memory statistics into buffer
            ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
            
            // Close file descriptor immediately to avoid resource leakage
            close(fd);

            if (bytes_read > 0) {
                buffer[bytes_read] = '\0'; // Null-terminate string buffer
                
                unsigned long mem_total = 0;
                unsigned long mem_available = 0;
                unsigned long swap_total = 0;
                unsigned long swap_free = 0;
                
                // Parse lines sequentially from buffer (e.g., "MemTotal: 16298412 kB")
                char *line = strtok(buffer, "\n");
                while (line != NULL) {
                    // Extract Total Physical RAM
                    if (strncmp(line, "MemTotal:", 9) == 0) {
                        sscanf(line, "MemTotal: %lu kB", &mem_total);
                    } 
                    // Extract Available Physical RAM (memory available for allocation without swapping)
                    else if (strncmp(line, "MemAvailable:", 13) == 0) {
                        sscanf(line, "MemAvailable: %lu kB", &mem_available);
                    } 
                    // Extract Total Virtual Swap space configured on disk (CO4: Paging/Swapping)
                    else if (strncmp(line, "SwapTotal:", 10) == 0) {
                        sscanf(line, "SwapTotal: %lu kB", &swap_total);
                    } 
                    // Extract Free Virtual Swap space
                    else if (strncmp(line, "SwapFree:", 9) == 0) {
                        sscanf(line, "SwapFree: %lu kB", &swap_free);
                    }
                    // Move to the next line in the buffer
                    line = strtok(NULL, "\n");
                }

                // If valid memory info was retrieved from the kernel, calculate utilization
                if (mem_total > 0) {
                    // Used physical RAM = Total physical RAM - Available physical RAM
                    unsigned long mem_used = (mem_total >= mem_available) ? (mem_total - mem_available) : 0;
                    // Calculate physical RAM utilization percentage
                    double mem_usage_pct = ((double)mem_used / mem_total) * 100.0;

                    // Calculate Swap utilization (CO4: Virtual Memory paging/swapping)
                    unsigned long swap_used = (swap_total >= swap_free) ? (swap_total - swap_free) : 0;
                    // Calculate virtual Swap utilization percentage
                    double swap_usage_pct = (swap_total > 0) ? (((double)swap_used / swap_total) * 100.0) : 0.0;

                    // Critical section: acquire mutex to safely write to shared state (CO6)
                    pthread_mutex_lock(&monitor_data.lock);
                    
                    // Populate system statistics in shared global structure
                    monitor_data.sys_stats.mem_total = mem_total;
                    monitor_data.sys_stats.mem_available = mem_available;
                    monitor_data.sys_stats.mem_used = mem_used;
                    monitor_data.sys_stats.mem_usage = mem_usage_pct;
                    monitor_data.sys_stats.swap_total = swap_total;
                    monitor_data.sys_stats.swap_free = swap_free;
                    monitor_data.sys_stats.swap_usage = swap_usage_pct;

                    // Notify display worker that memory data was updated (CO6: Condition Variable)
                    pthread_cond_broadcast(&monitor_data.cond_data_ready);
                    
                    // Release the mutex lock
                    pthread_mutex_unlock(&monitor_data.lock);

                    // Produce memory alert event if RAM consumption exceeds 90%
                    // Demonstrates the Semaphore-based Producer-Consumer bounded queue (CO6)
                    if (mem_usage_pct > 90.0) {
                        char alert_msg[128];
                        snprintf(alert_msg, sizeof(alert_msg), "Memory Warning: RAM usage at %.1f%%", mem_usage_pct);
                        log_event_produce(alert_msg);
                    }
                }
            }
        }

        // Sleep for 1 second between memory samples
        sleep(1);
    }
    return NULL;
}
