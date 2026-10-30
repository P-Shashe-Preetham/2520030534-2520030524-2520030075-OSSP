/**
 * @file ipc.c
 * @brief Inter-Process Communication (IPC) via Named Pipes (FIFOs).
 *
 * Course: Operating Systems and Systems Programming (25CS2104E) - Term-I
 * Team: 18 | Section: 6
 * Team Member Responsibility: J. ADVAITH (2520030524)
 *
 * Course Outcomes Addressed:
 * - CO2: Process control through signal transmission (kill syscall: SIGTERM, SIGSTOP, SIGCONT).
 * - CO3: Inter-Process Communication (IPC) foundations using Named Pipes (FIFOs: mkfifo, open, read, unlink).
 * - CO6: Thread-safe state modification and event logging coordination.
 *
 * Operating System Theory:
 * An anonymous pipe (pipe()) can only communicate between related processes sharing a common ancestor
 * created via fork(). A Named Pipe (FIFO) creates a special node in the filesystem (via mkfifo())
 * that allows completely unrelated processes to establish bidirectional or unidirectional IPC
 * channels using standard open(), read(), and write() file semantics.
 */

#include "monitor.h"
#include "ipc.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

/**
 * @brief Creates the named pipe on the filesystem using the mkfifo() system call.
 *
 * Checks if the FIFO filesystem node already exists. If not, it invokes the mkfifo()
 * system call with 0666 permissions (read/write for all users, subject to umask).
 *
 * @param path Path of the FIFO to create (e.g. "/tmp/ossp_monitor.fifo").
 * @return int 0 on success, -1 on failure.
 */
int init_ipc(const char* path) {
    if (!path) return -1;

    // CO3 & CO5: Create Named Pipe (FIFO special file) on the VFS
    if (mkfifo(path, 0666) == -1) {
        // If the FIFO already exists on disk from a previous run, that is acceptable
        if (errno != EEXIST) {
            perror("Failed to create Named Pipe via mkfifo() system call");
            return -1;
        }
    }
    return 0; // FIFO is ready for communication
}

/**
 * @brief Worker thread routine that listens on the named pipe for incoming commands.
 *
 * In Unix/Linux, opening a FIFO for reading with open(path, O_RDONLY) blocks until
 * a writer opens the FIFO for writing (e.g. via `echo "PAUSE" > /tmp/ossp_monitor.fifo`).
 * Once the writer writes data and closes its descriptor, read() returns the bytes,
 * and subsequent reads return 0 (EOF). The worker handles the command, closes its read
 * descriptor, and loops back to wait for the next writer.
 *
 * Supported Commands:
 * - PAUSE       : Freezes live UI rendering
 * - RESUME      : Resumes live UI rendering
 * - SNAPSHOT    : Triggers an on-demand mmap() snapshot dump
 * - SORT MEM    : Sorts process table by Physical Memory (RES) descending
 * - SORT PID    : Sorts process table by PID ascending
 * - SORT NAME   : Sorts process table alphabetically by command name
 * - KILL <pid>  : Sends SIGTERM to terminate the specified process (CO2/CO3)
 * - STOP <pid>  : Sends SIGSTOP to suspend the specified process (CO2/CO3)
 * - CONT <pid>  : Sends SIGCONT to resume a suspended process (CO2/CO3)
 * - QUIT / EXIT : Triggers clean, graceful shutdown of the monitor
 *
 * @param arg Pointer to FIFO path string.
 * @return void* NULL upon thread exit.
 */
void* ipc_worker(void* arg) {
    const char* path = (const char*)arg;
    if (!path) path = FIFO_PATH; // Fallback to default path

    char buffer[256];

    // Loop until the monitor is instructed to shut down
    while (monitor_data.keep_running) {
        // CO3: Open the FIFO in Read-Only mode. This blocks until a writer connects!
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            // Check if thread was signaled to exit while blocked
            if (!monitor_data.keep_running) break;
            sleep(1);
            continue;
        }

        // Read command text sent by the writer process
        ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
        
        // Close the read descriptor for this session
        close(fd);

        if (bytes > 0) {
            buffer[bytes] = '\0'; // Ensure string is null-terminated

            // Strip trailing newlines or carriage returns (e.g. added by echo)
            char* newline = strpbrk(buffer, "\r\n");
            if (newline) *newline = '\0';

            // Ignore empty inputs
            if (strlen(buffer) == 0) continue;

            // Log command reception into the semaphore-protected bounded queue
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "IPC Command received: '%.200s'", buffer);
            log_event_produce(log_buf);

            // ==========================================
            // Command Dispatch Table
            // ==========================================
            
            // PAUSE command: freeze terminal dashboard updates
            if (strcasecmp(buffer, "PAUSE") == 0) {
                monitor_data.is_paused = 1;
                log_event_produce("Display paused via IPC command");
            } 
            // RESUME command: unfreeze terminal dashboard updates
            else if (strcasecmp(buffer, "RESUME") == 0) {
                monitor_data.is_paused = 0;
                log_event_produce("Display resumed via IPC command");
            } 
            // SNAPSHOT command: trigger memory-mapped state dump
            else if (strcasecmp(buffer, "SNAPSHOT") == 0) {
                monitor_data.trigger_snapshot = 1;
                log_event_produce("Memory-mapped snapshot requested via IPC");
            } 
            // SORT MEM command: sort processes by resident RAM
            else if (strcasecmp(buffer, "SORT MEM") == 0) {
                monitor_data.sort_mode = SORT_BY_MEM;
                log_event_produce("Process sort set to Physical Memory (RES)");
            } 
            // SORT PID command: sort processes by PID
            else if (strcasecmp(buffer, "SORT PID") == 0) {
                monitor_data.sort_mode = SORT_BY_PID;
                log_event_produce("Process sort set to PID");
            } 
            // SORT NAME command: sort processes alphabetically
            else if (strcasecmp(buffer, "SORT NAME") == 0) {
                monitor_data.sort_mode = SORT_BY_NAME;
                log_event_produce("Process sort set to Name");
            } 
            // KILL <pid> command: dispatch SIGTERM via kill() system call (CO2, CO3)
            else if (strncasecmp(buffer, "KILL ", 5) == 0) {
                int target_pid = atoi(buffer + 5);
                if (target_pid > 0) {
                    if (kill(target_pid, SIGTERM) == 0) {
                        snprintf(log_buf, sizeof(log_buf), "Sent SIGTERM to process %d", target_pid);
                    } else {
                        snprintf(log_buf, sizeof(log_buf), "Failed to send SIGTERM to process %d: %s", target_pid, strerror(errno));
                    }
                    log_event_produce(log_buf);
                }
            } 
            // STOP <pid> command: dispatch SIGSTOP to suspend process execution (CO2, CO3)
            else if (strncasecmp(buffer, "STOP ", 5) == 0) {
                int target_pid = atoi(buffer + 5);
                if (target_pid > 0) {
                    kill(target_pid, SIGSTOP);
                    snprintf(log_buf, sizeof(log_buf), "Sent SIGSTOP to process %d", target_pid);
                    log_event_produce(log_buf);
                }
            } 
            // CONT <pid> command: dispatch SIGCONT to resume suspended process (CO2, CO3)
            else if (strncasecmp(buffer, "CONT ", 5) == 0) {
                int target_pid = atoi(buffer + 5);
                if (target_pid > 0) {
                    kill(target_pid, SIGCONT);
                    snprintf(log_buf, sizeof(log_buf), "Sent SIGCONT to process %d", target_pid);
                    log_event_produce(log_buf);
                }
            } 
            // QUIT / EXIT command: request cooperative monitor shutdown
            else if (strcasecmp(buffer, "QUIT") == 0 || strcasecmp(buffer, "EXIT") == 0) {
                monitor_data.keep_running = 0;
                log_event_produce("Termination requested via IPC QUIT command");
                break;
            }
        }
    }
    return NULL;
}

/**
 * @brief Removes the named pipe node from the filesystem using unlink().
 *
 * In Unix, unlink() deletes the directory entry associated with the FIFO node,
 * releasing the inode once all open file descriptors to it are closed.
 *
 * @param path Path to the FIFO file.
 */
void cleanup_ipc(const char* path) {
    if (path) {
        unlink(path); // CO3 & CO5: Remove filesystem FIFO node
    }
}
