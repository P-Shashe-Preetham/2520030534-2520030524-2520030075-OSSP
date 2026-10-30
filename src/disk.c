/**
 * @file disk.c
 * @brief Disk and Storage I/O statistics collector and worker thread.
 *
 * Course: Operating Systems and Systems Programming (25CS2104E) - Term-I
 * Team: 18 | Section: 6
 * Team Member Responsibility: THYARLA ABHINAV (2520030075)
 *
 * Course Outcomes Addressed:
 * - CO1: Direct system call execution (open, read, close) for storage metrics.
 * - CO5: Virtual File System (VFS) and block device accounting via /proc/diskstats.
 * - CO6: Concurrency and synchronization via POSIX threads and mutex locking.
 *
 * Abstract Requirements Met:
 * - Reports live disk/I/O activity in real-time as specified in Team 18's abstract.
 *
 * Operating System Theory:
 * The Linux block layer tracks cumulative I/O operations and sectors read/written
 * per device in `/proc/diskstats`. A Linux disk sector is standardized as 512 bytes (0.5 kB).
 * By sampling total sectors read and written across a known time interval (1 second),
 * we calculate the active I/O read and write throughput in kB/s.
 */

#include "monitor.h"
#include "disk.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/**
 * @brief Reads /proc/diskstats using low-level system calls to sample block device metrics.
 *
 * Format of /proc/diskstats:
 *   major minor dev_name rd_ios rd_merges rd_sectors rd_ticks wr_ios wr_merges wr_sectors wr_ticks ...
 *
 * Example:
 *   8       0 sda 14820 120 748120 4200 9210 340 512000 8900 ...
 *
 * Metrics extracted:
 * - rd_ios: Number of read I/O operations successfully completed
 * - wr_ios: Number of write I/O operations successfully completed
 * - rd_sectors: Total sectors read (each sector = 512 bytes)
 * - wr_sectors: Total sectors written (each sector = 512 bytes)
 *
 * @param stats Pointer to DiskStats structure to populate.
 * @return int 0 on success, -1 on failure.
 */
int read_disk_statistics(DiskStats* stats) {
    if (!stats) return -1;

    // CO1 & CO5: Open /proc/diskstats using direct unbuffered system call
    int fd = open("/proc/diskstats", O_RDONLY);
    if (fd < 0) {
        return -1; // Unable to access disk statistics
    }

    char buffer[4096];
    // Read kernel block I/O statistics into memory
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    // Close the file descriptor immediately
    close(fd);

    if (bytes <= 0) return -1;
    buffer[bytes] = '\0'; // Null-terminate string buffer

    unsigned long long reads = 0, writes = 0;
    unsigned long long sectors_read = 0, sectors_written = 0;
    char primary_dev[32] = "disk";
    int found_device = 0;

    // Parse each block device entry line-by-line using thread-safe strtok_r
    char *saveptr;
    char *line = strtok_r(buffer, "\n", &saveptr);
    while (line != NULL) {
        int major, minor;
        char dev[32];
        unsigned long long rd_ios, rd_merges, rd_sec, rd_ticks;
        unsigned long long wr_ios, wr_merges, wr_sec, wr_ticks;

        // Parse fields from /proc/diskstats
        if (sscanf(line, "%d %d %31s %llu %llu %llu %llu %llu %llu %llu %llu",
                   &major, &minor, dev,
                   &rd_ios, &rd_merges, &rd_sec, &rd_ticks,
                   &wr_ios, &wr_merges, &wr_sec, &wr_ticks) >= 11) {
            
            // Filter: Target primary physical and virtual storage drives:
            // - "sd[a-z]"   : SCSI / SATA / SAS drives (e.g. sda, sdb)
            // - "vd[a-z]"   : KVM / QEMU virtual virtio block disks (e.g. vda)
            // - "nvme[0-9]n[0-9]": NVMe PCIe SSD drives (e.g. nvme0n1)
            // Skip loop devices (loop*), RAM disks (ram*), and partition slices (e.g. sda1, nvme0n1p1)
            int is_target = 0;
            if (strncmp(dev, "sd", 2) == 0 && strlen(dev) == 3) {
                is_target = 1;
            } else if (strncmp(dev, "vd", 2) == 0 && strlen(dev) == 3) {
                is_target = 1;
            } else if (strncmp(dev, "nvme", 4) == 0 && strstr(dev, "p") == NULL) {
                is_target = 1;
            }

            if (is_target) {
                reads += rd_ios;
                writes += wr_ios;
                sectors_read += rd_sec;
                sectors_written += wr_sec;
                if (!found_device) {
                    strncpy(primary_dev, dev, sizeof(primary_dev) - 1);
                    found_device = 1;
                }
            }
        }
        line = strtok_r(NULL, "\n", &saveptr); // Next line
    }

    // Record primary device name and cumulative operation counters
    strncpy(stats->device_name, primary_dev, sizeof(stats->device_name) - 1);
    stats->reads_completed = reads;
    stats->writes_completed = writes;

    // Track sector changes across consecutive 1-second samples
    static unsigned long long prev_sec_r = 0;
    static unsigned long long prev_sec_w = 0;

    unsigned long long delta_r = (sectors_read >= prev_sec_r) ? (sectors_read - prev_sec_r) : 0;
    unsigned long long delta_w = (sectors_written >= prev_sec_w) ? (sectors_written - prev_sec_w) : 0;

    // Update previous readings for next delta calculation
    prev_sec_r = sectors_read;
    prev_sec_w = sectors_written;

    // Convert sectors to kilobytes per second:
    // Rate (kB/s) = (Sectors * 512 bytes_per_sector) / 1024 bytes_per_kB
    stats->read_rate_kbps = (double)(delta_r * 512) / 1024.0;
    stats->write_rate_kbps = (double)(delta_w * 512) / 1024.0;

    return 0;
}

/**
 * @brief Worker thread routine that continually polls and updates disk I/O statistics.
 *
 * Execution Steps:
 * 1. Takes an initial reading to establish base sector values.
 * 2. In a loop, sleeps for 1 second.
 * 3. Reads /proc/diskstats and calculates throughput rates (kB/s).
 * 4. Enters critical section by locking monitor_data.lock (CO6).
 * 5. Updates monitor_data.sys_stats.disk.
 * 6. Broadcasts condition variable to wake display thread (CO6).
 * 7. Exits critical section by unlocking monitor_data.lock.
 *
 * @param arg Unused thread argument (NULL).
 * @return void* NULL upon thread exit.
 */
void* disk_worker(void* arg) {
    (void)arg; // Silence unused parameter warning
    DiskStats local_disk;
    memset(&local_disk, 0, sizeof(DiskStats));

    // Baseline reading to seed initial sector numbers
    read_disk_statistics(&local_disk);

    // Loop until global signal handler signals shutdown
    while (monitor_data.keep_running) {
        // Sample every 1 second
        sleep(1);

        // Fetch fresh readings and calculate transfer rate
        if (read_disk_statistics(&local_disk) == 0) {
            // Critical section: acquire mutex to safely write to shared state (CO6)
            pthread_mutex_lock(&monitor_data.lock);
            monitor_data.sys_stats.disk = local_disk;

            // Signal condition variable to notify display thread of new metrics (CO6)
            pthread_cond_broadcast(&monitor_data.cond_data_ready);
            
            // Release the mutex lock
            pthread_mutex_unlock(&monitor_data.lock);
        }
    }
    return NULL;
}
