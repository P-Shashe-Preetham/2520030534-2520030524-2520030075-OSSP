/**
 * @file logger.h
 * @brief Asynchronous, bounded-buffer event logging interface.
 *
 * Course Outcomes Addressed:
 * - CO1: Low-level unbuffered Unix file I/O system calls (open, write, close).
 * - CO6: Concurrency synchronization via Counting Semaphores (sem_t) and Mutexes
 *        implementing the classic Producer-Consumer Bounded Buffer pattern.
 */

#ifndef LOGGER_H
#define LOGGER_H

/**
 * @brief Initializes the logging subsystem, file descriptor, and counting semaphores.
 *
 * @param filepath Path to destination log file (e.g. "logs/monitor.log").
 */
void init_logger(const char* filepath);

/**
 * @brief Producer function: enqueues a log event into the bounded buffer.
 *
 * Implements semaphore-based coordination:
 * 1. sem_wait(&sem_slots) - decrements available empty slot count
 * 2. pthread_mutex_lock(&buffer_lock) - enters critical section
 * 3. Enqueues event into circular buffer
 * 4. pthread_mutex_unlock(&buffer_lock) - exits critical section
 * 5. sem_post(&sem_items) - increments available item count
 *
 * @param msg Null-terminated string to log.
 */
void log_event_produce(const char* msg);

/**
 * @brief Legacy/direct log helper for synchronous logs.
 *
 * @param msg Null-terminated string to log.
 */
void log_message(const char* msg);

/**
 * @brief Consumer worker thread routine for the Producer-Consumer bounded queue.
 *
 * Continuously waits on sem_items, dequeues events, and writes them to the log file.
 *
 * @param arg Unused thread argument.
 * @return void* NULL upon exit.
 */
void* logger_consumer_worker(void* arg);

/**
 * @brief Flushes remaining queue items, closes file descriptor, and destroys semaphores.
 */
void close_logger(void);

#endif // LOGGER_H
