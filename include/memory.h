/**
 * @file memory.h
 * @brief Memory monitoring interface.
 *
 * Declares the worker function responsible for sampling `/proc/meminfo`,
 * tracking physical RAM utilization, and updating system statistics.
 */

#ifndef MEMORY_H
#define MEMORY_H

/**
 * @brief Thread entry point for the Memory worker.
 * Periodically parses /proc/meminfo and updates memory stats in MonitorData under mutex lock.
 *
 * @param arg Unused thread argument (NULL).
 * @return void* NULL upon thread termination.
 */
void* memory_worker(void* arg);

#endif // MEMORY_H
