/**
 * @file display.h
 * @brief Terminal dashboard user interface interface.
 *
 * Declares the worker function responsible for formatting and rendering
 * the live system monitor metrics to the terminal using ANSI escape sequences.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

/**
 * @brief Thread entry point for the Display worker.
 * Periodically acquires the shared state lock, reads metrics, clears the terminal,
 * and renders the resource monitoring dashboard.
 *
 * @param arg Unused thread argument (NULL).
 * @return void* NULL upon thread termination.
 */
void* display_worker(void* arg);

#endif // DISPLAY_H
