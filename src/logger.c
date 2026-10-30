/**
 * @file logger.c
 * @brief Thread-safe asynchronous event logger implementing the Producer-Consumer pattern.
 *
 * Course: Operating Systems and Systems Programming (25CS2104E) - Term-I
 * Team: 18 | Section: 6
 * Team Member Responsibility: PAPAISETTY SHASHE PREETHAM (2520030534)
 *
 * Course Outcomes Addressed:
 * - CO1: Low-level unbuffered Unix file I/O system calls (open, write, close).
 * - CO5: File system abstractions, file creation flags (O_WRONLY, O_CREAT, O_APPEND).
 * - CO6: Concurrency synchronization using Counting Semaphores (sem_t) and Mutexes
 *        (pthread_mutex_t) to manage a thread-safe bounded circular queue.
 *
 * This module implements Dijkstra's classic Bounded Buffer Producer-Consumer problem
 * using two counting semaphores (`sem_slots` and `sem_items`) and a binary mutex lock (`buffer_lock`).
 */
// Core monitor interfaces
#include "monitor.h"
#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

/** File descriptor for log file; initialized to -1 when logger is inactive */
static int log_fd = -1;

/** Dedicated static mutex guarding direct synchronous log_message calls */
static pthread_mutex_t direct_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Initializes the logging subsystem, file descriptor, and counting semaphores.
 *
 * Configures the Producer-Consumer bounded queue:
 * - sem_slots: initialized to EVENT_BUFFER_SIZE (counting available vacant buffer slots)
 * - sem_items: initialized to 0 (counting filled available items ready for consumption)
 * - buffer_lock: mutex ensuring mutual exclusion during enqueue/dequeue operations
 *
 * @param filepath Path to destination log file (e.g. "logs/monitor.log").
 */
void init_logger(const char* filepath) {
    if (filepath) {
        // Open or create log file using direct system call (CO1, CO5)
        // O_WRONLY: write-only mode
        // O_CREAT: create file if it does not exist
        // O_APPEND: all writes append data to the end of the file
        // 0644: permissions (read/write for owner, read-only for group/others)
        log_fd = open(filepath, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd < 0) {
            perror("Failed to open log file via open() system call");
        }
    }

    // Initialize circular buffer pointers
    monitor_data.event_queue.head = 0; // Read index
    monitor_data.event_queue.tail = 0; // Write index
    
    // Initialize mutex protecting the circular buffer critical section
    pthread_mutex_init(&monitor_data.event_queue.buffer_lock, NULL);

    // Counting semaphore for available empty slots: initialized to EVENT_BUFFER_SIZE (16)
    // 2nd argument (0) indicates the semaphore is shared between threads of a process
    sem_init(&monitor_data.event_queue.sem_slots, 0, EVENT_BUFFER_SIZE);

    // Counting semaphore for available filled items: initialized to 0 (queue starts empty)
    sem_init(&monitor_data.event_queue.sem_items, 0, 0);
}

/**
 * @brief Producer function: enqueues a log event into the bounded buffer.
 *
 * Implements Dijkstra's classic Semaphore Producer Protocol (CO6):
 * 1. sem_wait(&sem_slots) - Decrement empty slot count (blocks if buffer is completely full).
 * 2. pthread_mutex_lock(&buffer_lock) - Acquire mutex for critical section buffer access.
 * 3. Enqueue event into circular array at index `tail`.
 * 4. Advance tail index: tail = (tail + 1) % EVENT_BUFFER_SIZE.
 * 5. pthread_mutex_unlock(&buffer_lock) - Release mutex.
 * 6. sem_post(&sem_items) - Increment filled item count (wakes waiting consumer thread).
 *
 * @param msg Null-terminated string to log.
 */
void log_event_produce(const char* msg) {
    if (!msg) return;

    // CO6: Wait operation (P) on slots semaphore. Blocks if buffer is full (slots == 0)
    sem_wait(&monitor_data.event_queue.sem_slots);

    // Critical section: acquire buffer lock to protect circular queue integrity
    pthread_mutex_lock(&monitor_data.event_queue.buffer_lock);

    // Copy event message into current tail slot
    int tail = monitor_data.event_queue.tail;
    strncpy(monitor_data.event_queue.events[tail].message, msg, sizeof(monitor_data.event_queue.events[tail].message) - 1);
    monitor_data.event_queue.events[tail].message[sizeof(monitor_data.event_queue.events[tail].message) - 1] = '\0';
    monitor_data.event_queue.events[tail].timestamp = time(NULL);

    // Circular buffer index advancement with modulo wrap-around
    monitor_data.event_queue.tail = (tail + 1) % EVENT_BUFFER_SIZE;

    // Exit critical section
    pthread_mutex_unlock(&monitor_data.event_queue.buffer_lock);

    // CO6: Signal operation (V) on items semaphore. Increments available item count
    sem_post(&monitor_data.event_queue.sem_items);
}

/**
 * @brief Synchronous direct log writer (bypasses bounded buffer queue for startup/shutdown).
 *
 * @param msg Message string.
 */
void log_message(const char* msg) {
    if (log_fd >= 0 && msg) {
        // Guard direct writes to prevent interleaved output lines
        pthread_mutex_lock(&direct_log_mutex);
        
        // Format ISO timestamp: [YYYY-MM-DD HH:MM:SS]
        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S] ", localtime(&now));
        
        // Low-level unbuffered write calls (CO1)
        write(log_fd, timestamp, strlen(timestamp));
        write(log_fd, msg, strlen(msg));
        write(log_fd, "\n", 1);
        
        pthread_mutex_unlock(&direct_log_mutex);
    }
}

/**
 * @brief Consumer worker thread routine for the Producer-Consumer bounded queue.
 *
 * Implements Dijkstra's classic Semaphore Consumer Protocol (CO6):
 * 1. sem_timedwait(&sem_items) - Wait for an item (timed to check keep_running flag).
 * 2. pthread_mutex_lock(&buffer_lock) - Acquire mutex for critical section access.
 * 3. Dequeue event from circular array at index `head`.
 * 4. Advance head index: head = (head + 1) % EVENT_BUFFER_SIZE.
 * 5. pthread_mutex_unlock(&buffer_lock) - Release mutex.
 * 6. sem_post(&sem_slots) - Increment empty slot count (wakes any blocked producer threads).
 * 7. Write timestamped log message to disk via write() system call.
 *
 * @param arg Unused thread argument.
 * @return void* NULL upon thread exit.
 */
void* logger_consumer_worker(void* arg) {
    (void)arg; // Silence unused parameter warning

    while (monitor_data.keep_running) {
        // Use sem_timedwait with a 1-second timeout so the consumer thread
        // periodically checks if the monitor is shutting down (keep_running == 0)
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        if (sem_timedwait(&monitor_data.event_queue.sem_items, &ts) == -1) {
            if (errno == ETIMEDOUT) {
                continue; // Timeout expired, loop back to check keep_running flag
            }
            break; // Other error occurred, break out of loop
        }

        // Critical section: acquire buffer lock to safely extract item from queue
        pthread_mutex_lock(&monitor_data.event_queue.buffer_lock);
        
        int head = monitor_data.event_queue.head;
        LogEvent event = monitor_data.event_queue.events[head];
        // Advance head index with modulo arithmetic
        monitor_data.event_queue.head = (head + 1) % EVENT_BUFFER_SIZE;

        // Release buffer lock immediately
        pthread_mutex_unlock(&monitor_data.event_queue.buffer_lock);

        // CO6: Notify producers that an empty buffer slot has been freed (V operation on slots)
        sem_post(&monitor_data.event_queue.sem_slots);

        // Perform disk write outside the mutex lock to avoid blocking producers (CO1)
        if (log_fd >= 0) {
            char time_buf[64];
            strftime(time_buf, sizeof(time_buf), "[%Y-%m-%d %H:%M:%S] ", localtime(&event.timestamp));
            write(log_fd, time_buf, strlen(time_buf));
            write(log_fd, event.message, strlen(event.message));
            write(log_fd, "\n", 1);
        }
    }
    return NULL;
}

/**
 * @brief Flushes remaining queue items, closes file descriptor, and destroys semaphores.
 */
void close_logger(void) {
    // Destroy counting semaphores and mutex to free kernel resources (CO6)
    sem_destroy(&monitor_data.event_queue.sem_slots);
    sem_destroy(&monitor_data.event_queue.sem_items);
    pthread_mutex_destroy(&monitor_data.event_queue.buffer_lock);

    // Close the file descriptor (CO1, CO5)
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }
}
