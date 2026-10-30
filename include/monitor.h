/**
 * @file monitor.h
 * @brief Global definitions, data structures, and shared state for Team 18's Linux Resource Monitor.
 *
 * Course: Operating Systems and Systems Programming (25CS2104E) - Term-I
 * Section: 6 | Team No: 18
 * Project Title: Linux System Information and Resource Monitoring Tool
 * Team Members:
 *   - THYARLA ABHINAV (2520030075) - CPU, Memory & Disk stats (/proc parsing, file I/O)
 *   - J. ADVAITH (2520030524) - Multithreading, mutex synchronization, signals, integration
 *   - PAPAISETTY SHASHE PREETHAM (2520030534) - Process module, terminal display, logging, docs
 *
 * Course Outcomes Implemented:
 * - CO1: OS as a Service Layer & System Calls (open, read, write, close).
 * - CO2: Process lifecycle, state transitions (Running, Sleeping, Zombie, Stopped).
 * - CO3: IPC via Named Pipes (FIFOs) and POSIX Signals (SIGINT, SIGTERM, SIGUSR1).
 * - CO4: Virtual memory, process address space metrics (VIRT, RES), and swap tracking.
 * - CO5: File system abstractions, inodes, directory iteration, and mmap() memory-mapped I/O.
 * - CO6: Concurrency and synchronization using POSIX threads (pthread), mutual exclusion
 *        mutexes (pthread_mutex_t), condition variables (pthread_cond_t), and counting
 *        semaphores (sem_t).
 */

#ifndef MONITOR_H
#define MONITOR_H

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

/** Maximum number of processes tracked in memory */
#define MAX_PROCESSES 1024

/** Maximum capacity of the log event bounded buffer (CO6: Producer-Consumer) */
#define EVENT_BUFFER_SIZE 16

/** Named Pipe (FIFO) filesystem path for IPC communication (CO3) */
#define FIFO_PATH "/tmp/ossp_monitor.fifo"

/** Default memory-mapped snapshot dump path (CO5) */
#define SNAPSHOT_FILE "logs/snapshot.dat"

/**
 * @brief Sorting modes for the active process table (Abstract requirement).
 */
typedef enum {
    SORT_BY_MEM,  /**< Sort by Resident physical memory (RES) descending */
    SORT_BY_PID,  /**< Sort by Process ID ascending */
    SORT_BY_NAME  /**< Sort alphabetically by command name */
} ProcessSortMode;

/**
 * @brief Live Disk / Storage I/O statistics (Abstract requirement).
 * Parsed from /proc/diskstats using low-level system calls.
 */
typedef struct {
    char device_name[32];        /**< Primary block device name (e.g. sda, nvme0n1) */
    unsigned long long reads_completed;  /**< Total number of read I/O operations */
    unsigned long long writes_completed; /**< Total number of write I/O operations */
    double read_rate_kbps;       /**< Current read throughput in kB/s */
    double write_rate_kbps;      /**< Current write throughput in kB/s */
} DiskStats;

/**
 * @brief System-wide performance statistics (CO1, CO4).
 * Stores CPU, RAM, Swap, and Disk I/O metrics parsed from /proc.
 */
typedef struct {
    double cpu_usage;            /**< Aggregate CPU utilization percentage (0.0% to 100.0%) */
    int cpu_cores;               /**< Number of active online CPU cores detected */
    unsigned long mem_total;     /**< Total physical RAM (in kB) */
    unsigned long mem_available; /**< Available physical RAM (in kB) */
    unsigned long mem_used;      /**< In-use physical RAM (in kB) */
    double mem_usage;            /**< Memory usage percentage */
    unsigned long swap_total;    /**< Total Swap space configured (in kB) (CO4) */
    unsigned long swap_free;     /**< Free Swap space available (in kB) (CO4) */
    double swap_usage;           /**< Swap usage percentage */
    DiskStats disk;              /**< Disk and storage I/O statistics */
} SystemStats;

/**
 * @brief Process information record (CO2, CO4).
 * Captures process lifecycle attributes, memory consumption, and state transitions.
 * Derived from /proc/[pid]/stat, /proc/[pid]/statm, and /proc/[pid]/status.
 */
typedef struct {
    int pid;                     /**< Process ID */
    int ppid;                    /**< Parent Process ID */
    char state;                  /**< State: 'R' (Running), 'S' (Sleeping), 'Z' (Zombie), 'T' (Stopped) */
    char name[256];              /**< Process command name */
    unsigned long mem_vsize_kb;  /**< Virtual memory size in kB (CO4: Process Address Space) */
    unsigned long mem_rss_kb;    /**< Resident Set Size in physical RAM in kB (CO4) */
    int threads_count;           /**< Number of threads in this process (CO6) */
} ProcessInfo;

/**
 * @brief Log event item for the Producer-Consumer bounded queue (CO6).
 */
typedef struct {
    char message[256];
    time_t timestamp;
} LogEvent;

/**
 * @brief Bounded circular buffer for thread-safe asynchronous logging (CO6).
 * Synchronized using counting semaphores (sem_t) and a mutex.
 */
typedef struct {
    LogEvent events[EVENT_BUFFER_SIZE];
    int head;
    int tail;
    pthread_mutex_t buffer_lock; /**< Mutex for critical section buffer access */
    sem_t sem_slots;            /**< Counting semaphore: available empty slots */
    sem_t sem_items;            /**< Counting semaphore: available unread items */
} BoundedEventBuffer;

/**
 * @brief Central shared monitor data structure.
 * Houses system metrics, process table, and full concurrency synchronization primitives.
 */
typedef struct {
    SystemStats sys_stats;                /**< Current snapshot of system CPU, RAM, Swap, and Disk */
    ProcessInfo processes[MAX_PROCESSES]; /**< Array of active processes scanned from /proc */
    int process_count;                    /**< Total number of active processes */
    ProcessSortMode sort_mode;            /**< Active process sorting criteria (Abstract requirement) */
    
    // Process State Breakdown (CO2)
    int running_count;                    /**< Count of processes in 'R' (Running) state */
    int sleeping_count;                   /**< Count of processes in 'S'/'D' (Sleeping) state */
    int zombie_count;                     /**< Count of processes in 'Z' (Zombie) state */
    int stopped_count;                    /**< Count of processes in 'T' (Stopped) state */

    // CO6: POSIX Mutual Exclusion and Condition Synchronization
    pthread_mutex_t lock;                 /**< Mutex protecting monitor_data concurrent access */
    pthread_cond_t cond_data_ready;       /**< Condition variable signaling display worker on updates */

    // CO6: Counting Semaphore Bounded Event Queue
    BoundedEventBuffer event_queue;

    // CO3: Asynchronous Lifecycle Flags
    volatile sig_atomic_t keep_running;   /**< Atomic flag: 1 to continue, 0 to terminate gracefully */
    volatile sig_atomic_t is_paused;      /**< Atomic flag: 1 to freeze display updates, 0 to resume */
    volatile sig_atomic_t trigger_snapshot; /**< Atomic flag: 1 when SIGUSR1 or IPC requests mmap dump */
} MonitorData;

/**
 * @brief Global shared monitor instance.
 */
extern MonitorData monitor_data;

#endif // MONITOR_H
