# Linux System Information & Resource Monitoring Tool

**Course**: Operating Systems and Systems Programming (25CS2104E) — 2026–27, Term-I  
**Section**: 6 | **Team Number**: 18  
**Faculty Coordinator**: Dr. K. Anusha  
**GitHub Repository**: [https://github.com/P-Shashe-Preetham/2520030534-2520030524-2520030075-OSSP](https://github.com/P-Shashe-Preetham/2520030534-2520030524-2520030075-OSSP)

---

## Team Members & Responsibilities

| Roll Number | Student Name | Assigned Responsibility |
| :--- | :--- | :--- |
| **2520030075** | **THYARLA ABHINAV** | CPU, Memory & Disk stats modules (`/proc/stat`, `/proc/meminfo`, `/proc/diskstats` parsing), low-level unbuffered file I/O implementation. |
| **2520030524** | **J. ADVAITH** | Multithreaded polling architecture, mutex synchronization, POSIX signal handling, and overall integration. |
| **2520030534** | **PAPAISETTY SHASHE PREETHAM** | Process listing & sorting module (`/proc/[pid]` parsing), terminal UI display, bounded-queue logging, testing & documentation. |

---

## Project Overview (Abstract Verification)

Standard tools like `top`, `htop`, and `ps` abstract away the underlying mechanisms of how system metrics are obtained from the Linux kernel. This project is a transparent, high-performance, user-space monitoring utility implemented in C from first principles.

### Key Abstract Requirements Implemented:
1. **Low-Level Unbuffered System Calls**: Exclusively uses `open()`, `read()`, `write()`, and `close()` directly on `/proc/stat`, `/proc/meminfo`, `/proc/diskstats`, `/proc/[pid]/stat`, and `/proc/[pid]/status`.
2. **CPU, Memory & Real-Time Disk/IO Activity**:
   - **CPU**: Computes aggregate CPU utilization percentage across online cores.
   - **Memory & Swap**: Tracks physical RAM (`MemTotal`, `MemAvailable`) and Virtual Swap space.
   - **Disk I/O**: Reads block device counters from `/proc/diskstats`, calculating real-time read and write throughput in kB/s.
3. **Process Abstraction & Dynamic Sorting**:
   - Inspects `PID`, `PPID`, execution state (`R`, `S`, `Z`, `T`), and thread counts.
   - Calculates Virtual Memory (`VIRT`) and Resident Physical RAM (`RES`) per process via `/proc/[pid]/statm`.
   - **Sorting on Request**: Sorts process listings by **Physical Memory (RES)**, **PID**, or **Name** via command-line flags or live IPC commands.
4. **Multithreaded Polling with Mutex Synchronization**: Spawns 7 concurrent POSIX threads communicating through synchronized shared memory protected by `pthread_mutex_t`.
5. **Signal Handling for Graceful Shutdown**: Intercepts `SIGINT` (Ctrl+C) and `SIGTERM` using `sigaction()` to ensure clean thread termination and resource cleanup without leaks. Supports `SIGUSR1` for on-demand snapshots.
6. **Inter-Process Communication (IPC)**: Provides a Named Pipe (FIFO) at `/tmp/ossp_monitor.fifo` for remote runtime control (`PAUSE`, `RESUME`, `SORT`, `KILL`, `STOP`, `CONT`, `QUIT`).
7. **Memory-Mapped Snapshot Mode (`mmap`)**: Persists structured system and process snapshots directly using `mmap()`, `msync()`, and `munmap()`.
8. **Producer-Consumer Event Logging**: Uses Counting Semaphores (`sem_t`) to manage a thread-safe circular bounded event buffer.

---

## Complete Course Outcomes (CO-1 to CO-6) Implementation Matrix

| Course Outcome (CO) | Topic / Theoretical Foundation | Implementation in Codebase |
| :--- | :--- | :--- |
| **CO-1: OS as a Service Layer & System Calls** | User vs Kernel space, system call interface, command-line arguments | Direct unbuffered system calls (`open`, `read`, `write`, `close`). CLI parsing for `--snapshot`, `--sort`, `--no-log`. |
| **CO-2: Processes & Process Control** | Process lifecycle, PCB attributes, parent-child hierarchies, process state transitions | Categorizes processes into Running (`R`), Sleeping (`S`), Zombie (`Z`), and Stopped (`T`). Remote signal control (`kill`, `SIGSTOP`, `SIGCONT`). |
| **CO-3: Inter-Process Communication & Signals** | Named Pipes (FIFOs), asynchronous signal handling, signal safety | Named Pipe at `/tmp/ossp_monitor.fifo` (`mkfifo`, `open`, `read`, `unlink`). `sigaction()` handling for `SIGINT`, `SIGTERM`, and `SIGUSR1`. |
| **CO-4: Memory Management** | Virtual memory, address space abstractions, page faults, swapping | Reads `/proc/meminfo` (RAM & Swap) and `/proc/[pid]/statm` for Virtual Address Space (`VIRT`) and Physical Footprint (`RES`). |
| **CO-5: File Systems, VFS & Memory-Mapped I/O** | File abstractions, Inodes, directory entries, `mmap()` | VFS directory traversal (`opendir`, `readdir`). Memory-mapped snapshots via `mmap()`, `msync()`, `munmap()`. Inode inspection via `stat()`. |
| **CO-6: Concurrency & Synchronization** | POSIX Threads, mutexes, condition variables, counting semaphores | 7 concurrent POSIX threads. `pthread_mutex_t` mutual exclusion. `pthread_cond_timedwait()` coordination. Counting semaphore bounded queue (`sem_t`). |

---

## How to Build & Run

### 1. Build
```bash
make
```

### 2. Run Live Dashboard
```bash
./monitor
```

### 3. Change Process Sorting
- Launch with sorting by PID:
  ```bash
  ./monitor --sort pid
  ```
- Or change sorting dynamically via IPC while running:
  ```bash
  echo "SORT PID"  > /tmp/ossp_monitor.fifo
  echo "SORT MEM"  > /tmp/ossp_monitor.fifo
  echo "SORT NAME" > /tmp/ossp_monitor.fifo
  ```

### 4. Remote Process Control (IPC)
- Pause and Resume display updates:
  ```bash
  echo "PAUSE"  > /tmp/ossp_monitor.fifo
  echo "RESUME" > /tmp/ossp_monitor.fifo
  ```
- Send signals to target processes:
  ```bash
  echo "STOP <pid>" > /tmp/ossp_monitor.fifo
  echo "CONT <pid>" > /tmp/ossp_monitor.fifo
  echo "KILL <pid>" > /tmp/ossp_monitor.fifo
  ```

### 5. Memory-Mapped Snapshot (`mmap`)
Generate a one-shot snapshot to test memory mapping:
```bash
./monitor --snapshot
cat logs/snapshot.dat
```

### 6. Verify Memory Safety with Valgrind
```bash
valgrind --leak-check=full --show-leak-kinds=all ./monitor --snapshot
```
