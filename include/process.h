/**
 * @file process.h
 * @brief Process inspection and scanning interface.
 *
 * Declares the worker function responsible for traversing the `/proc` directory
 * to collect information about all active Linux processes.
 */

#ifndef PROCESS_H
#define PROCESS_H

/**
 * @brief Thread entry point for the Process worker.
 * Periodically scans numeric PID directories in /proc, extracts process status,
 * and atomically updates the shared process list in MonitorData.
 *
 * @param arg Unused thread argument (NULL).
 * @return void* NULL upon thread termination.
 */
void* process_worker(void* arg);

#endif // PROCESS_H
