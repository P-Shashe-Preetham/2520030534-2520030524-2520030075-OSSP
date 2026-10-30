/**
 * @file snapshot.c
 * @brief Memory-Mapped File I/O (mmap) and Inode inspection implementation.
 *
 * Course: Operating Systems and Systems Programming (25CS2104E) - Term-I
 * Team: 18 | Section: 6
 * Team Member Responsibility: THYARLA ABHINAV (2520030075) / Team 18
 *
 * Course Outcomes Addressed:
 * - CO1: Low-level file system calls (open, close, ftruncate).
 * - CO4: Virtual memory mapping, page alignment, and dirty page writebacks.
 * - CO5: Virtual File System (VFS), Inodes (stat syscall), and memory-mapped files (mmap, munmap, msync).
 * - CO6: Thread-safe data reading from MonitorData.
 *
 * Operating System Theory:
 * Traditional file I/O requires copying data between user-space application buffers
 * and kernel page cache buffers via read() and write() system calls. Memory-mapped I/O (mmap)
 * maps the file directly into the process's virtual address space (via page table entries).
 * Writing to memory directly modifies the kernel's page cache, and calling msync() synchronizes
 * dirty pages to the physical backing storage device.
 */
// Core monitor interfaces
#include "monitor.h"
#include "snapshot.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

/** Size of the memory-mapped backing store: 16 KB (a 4x multiple of standard 4096-byte page size) */
#define MMAP_SNAPSHOT_SIZE (16 * 1024)

/**
 * @brief Persists the current monitor state using POSIX memory-mapped file I/O (mmap).
 *
 * Step-by-Step System Call Workflow:
 * 1. open() with O_RDWR | O_CREAT | O_TRUNC to open/create the snapshot file.
 * 2. ftruncate() to extend the file to the exact byte size of the mapped memory region.
 * 3. mmap() with PROT_READ | PROT_WRITE and MAP_SHARED to map pages into virtual memory.
 * 4. snprintf() writes formatted text directly into the mapped memory pointer.
 * 5. msync() flushes dirty virtual memory pages down to physical disk storage.
 * 6. munmap() unmaps the memory range from the process page table.
 * 7. close() closes the backing file descriptor.
 *
 * @param filepath Output file path (e.g. "logs/snapshot.dat").
 * @return int 0 on success, -1 on failure.
 */
int save_mmap_snapshot(const char* filepath) {
    // Fallback to default snapshot path if none is specified
    if (!filepath) filepath = SNAPSHOT_FILE;

    // CO1 & CO5: Open file with read/write access, create if absent, truncate previous content
    int fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Error opening snapshot file for mmap via open()");
        return -1;
    }

    // Allocate physical file size to backing store prior to mapping (CO5)
    // If a file is mapped with mmap() without allocating space, writing causes SIGBUS!
    if (ftruncate(fd, MMAP_SNAPSHOT_SIZE) == -1) {
        perror("ftruncate failed to allocate backing store for mmap");
        close(fd);
        return -1;
    }

    // CO4 & CO5: Memory-map the file into the process's virtual address space
    // - addr = NULL: Kernel selects a suitable page-aligned virtual address
    // - length = MMAP_SNAPSHOT_SIZE: Number of bytes to map
    // - prot = PROT_READ | PROT_WRITE: Pages may be read and written
    // - flags = MAP_SHARED: Updates to the mapping are shared and written back to the file
    // - fd = backing file descriptor, offset = 0
    char* map_ptr = (char*)mmap(NULL, MMAP_SNAPSHOT_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map_ptr == MAP_FAILED) {
        perror("mmap system call failed");
        close(fd);
        return -1;
    }

    // Acquire shared mutex lock to safely snapshot current state without race conditions (CO6)
    pthread_mutex_lock(&monitor_data.lock);
    SystemStats sys = monitor_data.sys_stats;
    int proc_count = monitor_data.process_count;
    int r_c = monitor_data.running_count;
    int s_c = monitor_data.sleeping_count;
    int z_c = monitor_data.zombie_count;
    int t_c = monitor_data.stopped_count;
    
    // Copy top 25 processes for the detailed snapshot
    int export_count = proc_count > 25 ? 25 : proc_count;
    ProcessInfo procs[25];
    for (int i = 0; i < export_count; i++) {
        procs[i] = monitor_data.processes[i];
    }
    // Release the mutex lock promptly
    pthread_mutex_unlock(&monitor_data.lock);

    // Format snapshot content directly into the memory-mapped virtual address region
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    int offset = 0;
    // Write header, system, memory, and disk statistics directly into the mapped memory buffer
    offset += snprintf(map_ptr + offset, MMAP_SNAPSHOT_SIZE - offset,
        "========================================================================\n"
        "      LINUX RESOURCE MONITOR - MEMORY-MAPPED SNAPSHOT (mmap)           \n"
        "      Team 18 | Section 6 | Operating Systems & Systems Programming    \n"
        "========================================================================\n"
        "Generated At:       %s\n"
        "Online CPU Cores:   %d\n"
        "CPU Utilization:    %.2f%%\n"
        "RAM Usage:          %.2f%% (%lu kB used / %lu kB total)\n"
        "Swap Usage:         %.2f%% (%lu kB free / %lu kB total)\n"
        "Disk Device:        %s (Read: %.1f kB/s, Write: %.1f kB/s)\n"
        "Disk Total Ops:     %llu Reads, %llu Writes\n"
        "Process Breakdown:  %d total (R:%d, S:%d, Z:%d, T:%d)\n"
        "========================================================================\n"
        "%-8s %-8s %-6s %-12s %-12s %s\n"
        "------------------------------------------------------------------------\n",
        time_str, sys.cpu_cores, sys.cpu_usage, sys.mem_usage,
        sys.mem_used, sys.mem_total, sys.swap_usage, sys.swap_free,
        sys.swap_total, sys.disk.device_name[0] ? sys.disk.device_name : "none",
        sys.disk.read_rate_kbps, sys.disk.write_rate_kbps,
        sys.disk.reads_completed, sys.disk.writes_completed,
        proc_count, r_c, s_c, z_c, t_c,
        "PID", "PPID", "STATE", "VIRT(kB)", "RES(kB)", "COMMAND"
    );

    // Append individual process records into the memory-mapped virtual pages
    for (int i = 0; i < export_count && offset < MMAP_SNAPSHOT_SIZE - 256; i++) {
        offset += snprintf(map_ptr + offset, MMAP_SNAPSHOT_SIZE - offset,
            "%-8d %-8d %-6c %-12lu %-12lu %s\n",
            procs[i].pid, procs[i].ppid, procs[i].state,
            procs[i].mem_vsize_kb, procs[i].mem_rss_kb, procs[i].name
        );
    }

    // CO4 & CO5: Flush dirty virtual memory pages to physical disk storage
    // MS_SYNC requests synchronous writeback to persistent storage
    if (msync(map_ptr, MMAP_SNAPSHOT_SIZE, MS_SYNC) == -1) {
        perror("msync system call failed");
    }

    // CO4 & CO5: Unmap the virtual memory pages from the process page table
    if (munmap(map_ptr, MMAP_SNAPSHOT_SIZE) == -1) {
        perror("munmap system call failed");
    }

    // Close the file descriptor (CO1, CO5)
    close(fd);
    return 0; // Snapshot successfully persisted via mmap()
}

/**
 * @brief Inspects a file's VFS metadata and Inode number using the stat() system call.
 *
 * In the Unix Virtual File System (VFS), an Inode (Index Node) stores all metadata
 * about a file (file size, permissions, owner, timestamps, block pointers) except its
 * name and content. This function queries the VFS inode table for demonstration (CO5).
 *
 * @param filepath Path to the target file.
 * @param[out] inode Pointer to store the inode number (st_ino).
 * @param[out] size Pointer to store the file size in bytes (st_size).
 * @return int 0 on success, -1 on failure.
 */
int get_file_stat_info(const char* filepath, unsigned long* inode, off_t* size) {
    if (!filepath || !inode || !size) return -1;

    struct stat st;
    // CO5: stat() system call to examine VFS Inode and file attributes
    if (stat(filepath, &st) == -1) {
        return -1; // File does not exist or inaccessible
    }

    // Extract the unique VFS inode number and file size in bytes
    *inode = (unsigned long)st.st_ino;
    *size = st.st_size;
    return 0;
}
