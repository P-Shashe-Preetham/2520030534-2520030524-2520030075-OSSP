/**
 * @file ipc.h
 * @brief Inter-Process Communication (IPC) subsystem interface.
 *
 * Course Outcomes Addressed:
 * - CO2: Process control and signal delivery (kill, SIGSTOP, SIGCONT).
 * - CO3: Inter-Process Communication via Named Pipes (FIFOs: mkfifo, open, read, unlink).
 *
 * Exposes a command interface over a named pipe (FIFO_PATH) allowing external
 * processes or shell commands to interact with the running monitor.
 */

#ifndef IPC_H
#define IPC_H

/**
 * @brief Creates the named pipe (FIFO) on the filesystem if it does not exist.
 *
 * @param path Filesystem path for the FIFO (e.g. FIFO_PATH).
 * @return int 0 on success, -1 on failure.
 */
int init_ipc(const char* path);

/**
 * @brief Worker thread routine that listens on the named pipe for incoming commands.
 *
 * Supported commands sent to the FIFO (e.g., `echo "PAUSE" > /tmp/ossp_monitor.fifo`):
 * - PAUSE          : Freeze live display updating.
 * - RESUME         : Unfreeze live display updating.
 * - SNAPSHOT       : Trigger a memory-mapped dump (CO5).
 * - KILL <pid>     : Send SIGTERM to terminate target process (CO2/CO3).
 * - STOP <pid>     : Send SIGSTOP to suspend target process.
 * - CONT <pid>     : Send SIGCONT to resume stopped process.
 * - QUIT / EXIT    : Request graceful monitor shutdown.
 *
 * @param arg Unused thread parameter.
 * @return void* NULL upon thread termination.
 */
void* ipc_worker(void* arg);

/**
 * @brief Closes and unlinks (deletes) the named pipe from the filesystem.
 *
 * @param path Filesystem path of the FIFO to remove.
 */
void cleanup_ipc(const char* path);

#endif // IPC_H
