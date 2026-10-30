/**
 * @file cpu.h
 * @brief CPU monitoring interface.
 *
 * Declares functions responsible for sampling `/proc/stat`, calculating
 * aggregate CPU utilization, and running the background CPU worker thread.
 */

#ifndef CPU_H
#define CPU_H

/**
 * @brief Thread entry point for the CPU worker.
 * Periodically calculates CPU usage and safely updates the shared MonitorData structure.
 *
 * @param arg Unused thread argument (NULL).
 * @return void* NULL upon thread termination.
 */
void* cpu_worker(void* arg);

/**
 * @brief Reads /proc/stat to compute the percentage of CPU time used since last sample.
 *
 * @return double CPU usage percentage (between 0.0% and 100.0%).
 */
double get_cpu_usage(void);

#endif // CPU_H
