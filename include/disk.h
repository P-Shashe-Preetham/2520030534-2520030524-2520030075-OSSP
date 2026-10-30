/**
 * @file disk.h
 * @brief Disk / Storage I/O monitoring interface (Abstract requirement).
 *
 * Team Member Responsibility: THYARLA ABHINAV (2520030075)
 *
 * Declares the worker function and helpers responsible for sampling `/proc/diskstats`,
 * tracking block device reads/writes and throughput rates, and updating the shared MonitorData.
 */

#ifndef DISK_H
#define DISK_H

#include "monitor.h"

/**
 * @brief Thread entry point for the Disk I/O worker.
 * Periodically parses /proc/diskstats using low-level open(), read(), close()
 * and updates DiskStats in monitor_data under mutex lock.
 *
 * @param arg Unused thread argument.
 * @return void* NULL upon thread exit.
 */
void* disk_worker(void* arg);

/**
 * @brief Reads /proc/diskstats to sample block device read/write metrics.
 *
 * @param stats Pointer to DiskStats structure to populate.
 * @return int 0 on success, -1 on failure.
 */
int read_disk_statistics(DiskStats* stats);

#endif // DISK_H
