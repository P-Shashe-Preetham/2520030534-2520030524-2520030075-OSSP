/**
 * @file cpu.c
 * @brief CPU statistics collector and worker thread.
 *
 * Course: Operating Systems and Systems Programming (25CS2104E) - Term-I
 * Team: 18 | Section: 6
 * Team Member Responsibility: THYARLA ABHINAV (2520030075)
 *
 * Course Outcomes Addressed:
 * - CO1: Direct system call execution (open, read, close) for kernel service queries.
 * - CO5: Virtual File System (VFS) and /proc pseudo-filesystem parsing.
 * - CO6: Concurrency, mutual exclusion (pthread_mutex_t), condition variable signaling
 *        (pthread_cond_broadcast), and Producer-Consumer event logging via semaphores.
 *
 * The Linux kernel exposes CPU utilization counters in /proc/stat.
 * This module samples those counters periodically, calculates the delta in clock ticks,
 * and derives real-time aggregate CPU usage percentage across all online cores.
 */
// Core monitor interfaces
#include "monitor.h"
#include "cpu.h"
#include "logger.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

/**
 * @brief Queries the operating system for the number of active, online CPU processing cores.
 *
 * Calls sysconf(_SC_NPROCESSORS_ONLN), which invokes a kernel system query
 * to return the number of processors currently available to schedule tasks.
 *
 * @return int Number of online CPU cores (fallback to 1 if detection fails).
 */
static int get_online_cpu_cores(void) {
    // POSIX runtime configuration query for online processors
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores > 0) {
        return (int)cores;
    }
    // Fallback default if sysconf fails or returns an invalid count
    return 1;
}

/**
 * @brief Reads /proc/stat using direct system calls to extract cumulative CPU time ticks.
 *
 * In Linux, the first line of /proc/stat contains aggregate numbers measured in USER_HZ
 * clock ticks (typically 100ths of a second) since system boot:
 * Format:
 *   cpu  user nice system idle iowait irq softirq steal guest guest_nice
 *
 * Definitions:
 * - user: Time spent in user space running normal processes
 * - nice: Time spent in user space with low-priority (niced) processes
 * - system: Time spent in kernel space executing system calls
 * - idle: Time spent idle with no task runnable
 * - iowait: Time spent waiting for outstanding I/O operations to complete
 * - irq: Time servicing hardware interrupts
 * - softirq: Time servicing software interrupts
 * - steal: Involuntary wait time under a virtual hypervisor
 *
 * @param[out] total Pointer to store the calculated total CPU time ticks (idle + active).
 * @param[out] idle Pointer to store the calculated idle CPU time ticks (idle + iowait).
 * @return int 0 on success, -1 on failure.
 */
static int read_cpu_stats(unsigned long long *total, unsigned long long *idle) {
    // Open /proc/stat using low-level unbuffered system call (CO1, CO5)
    // O_RDONLY flag indicates read-only access to the virtual filesystem
    int fd = open("/proc/stat", O_RDONLY);
    if (fd < 0) {
        perror("Error opening /proc/stat via open() system call");
        return -1;
    }

    char buffer[1024];
    // Read the virtual file content into stack memory using read() syscall
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    
    // Always close the file descriptor immediately to avoid descriptor leaks
    close(fd);

    // If zero or negative bytes were read, reading failed
    if (bytes_read <= 0) {
        return -1;
    }

    // Ensure the read data is a valid null-terminated C string
    buffer[bytes_read] = '\0';

    unsigned long long user, nice, system, idl, iowait, irq, softirq, steal, guest, guest_nice;
    
    // Attempt to parse all 10 standard Linux CPU time fields (clock ticks)
    if (sscanf(buffer, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idl, &iowait, &irq, &softirq, &steal, &guest, &guest_nice) < 10) {
        // Fallback for minimal kernel configurations containing only the 4 basic fields
        if (sscanf(buffer, "cpu  %llu %llu %llu %llu", &user, &nice, &system, &idl) < 4) {
            return -1; // Parsing failed
        }
        // Zero out optional advanced tick fields
        iowait = irq = softirq = steal = guest = guest_nice = 0;
    }

    // Total idle time includes normal idle ticks plus I/O wait ticks
    *idle = idl + iowait;

    // Active (non-idle) time is the sum of user, nice, kernel system, interrupt, and steal ticks
    unsigned long long non_idle = user + nice + system + irq + softirq + steal;

    // Total CPU time is the sum of idle time and active non-idle time
    *total = *idle + non_idle;

    return 0; // Successfully extracted tick counters
}

/**
 * @brief Computes the CPU usage percentage since the previous invocation.
 *
 * Mathematical derivation:
 *   delta_total = current_total_ticks - previous_total_ticks
 *   delta_idle  = current_idle_ticks - previous_idle_ticks
 *   CPU Usage % = ((delta_total - delta_idle) / delta_total) * 100.0
 *
 * Static variables preserve the previous sample's readings between invocations.
 *
 * @return double Calculated CPU utilization percentage (0.0% to 100.0%).
 */
double get_cpu_usage(void) {
    // Static variables maintain state across multiple function calls
    static unsigned long long prev_total = 0;
    static unsigned long long prev_idle = 0;

    unsigned long long total = 0, idle = 0;
    // Query latest tick numbers from /proc/stat
    if (read_cpu_stats(&total, &idle) != 0) {
        return 0.0;
    }

    // Calculate tick changes that occurred over the last interval
    unsigned long long total_d = total - prev_total;
    unsigned long long idle_d = idle - prev_idle;

    double cpu_usage_pct = 0.0;
    // Guard against division by zero if called too rapidly
    if (total_d > 0) {
        // Percentage of active CPU time over elapsed total time
        cpu_usage_pct = ((double)(total_d - idle_d) / total_d) * 100.0;
    }

    // Store current readings for the next delta calculation
    prev_total = total;
    prev_idle = idle;

    return cpu_usage_pct;
}

/**
 * @brief Worker thread routine that continually polls and updates CPU statistics.
 *
 * Execution Steps:
 * 1. Discovers number of active CPU cores via sysconf.
 * 2. In a loop, calculates CPU usage percentage every 1 second.
 * 3. Enters critical section by locking monitor_data.lock (CO6).
 * 4. Updates shared CPU metrics.
 * 5. Broadcasts pthread_cond_broadcast(&monitor_data.cond_data_ready) to wake display.
 * 6. Exits critical section by unlocking monitor_data.lock.
 * 7. If utilization exceeds 85.0%, enqueues alert into semaphore bounded queue.
 *
 * @param arg Unused thread argument (NULL).
 * @return void* NULL upon thread termination.
 */
void* cpu_worker(void* arg) {
    (void)arg; // Silence unused parameter compiler warning
    
    // Query total online processing cores once during startup
    int cores = get_online_cpu_cores();

    // Loop until global signal handler signals shutdown
    while (monitor_data.keep_running) {
        // Calculate CPU utilization over the previous 1-second interval
        double usage = get_cpu_usage();

        // Critical section: acquire mutex to safely write to shared state (CO6)
        pthread_mutex_lock(&monitor_data.lock);
        
        // Write metrics into shared global structure
        monitor_data.sys_stats.cpu_usage = usage;
        monitor_data.sys_stats.cpu_cores = cores;

        // CO6: Signal condition variable to notify display worker that new data is ready
        pthread_cond_broadcast(&monitor_data.cond_data_ready);
        
        // Release the mutex lock
        pthread_mutex_unlock(&monitor_data.lock);

        // CO6: Produce high utilization alert event if CPU spikes > 85%
        // Demonstrates the Semaphore-based Producer-Consumer bounded queue
        if (usage > 85.0) {
            char alert_msg[128];
            snprintf(alert_msg, sizeof(alert_msg), "CPU Alert: High utilization detected (%.1f%%)", usage);
            log_event_produce(alert_msg);
        }

        // Sleep for 1 second before taking next sample
        sleep(1);
    }
    return NULL;
}
