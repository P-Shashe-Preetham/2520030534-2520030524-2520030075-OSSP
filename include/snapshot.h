/**
 * @file snapshot.h
 * @brief Memory-Mapped File I/O and Inode inspection interface.
 *
 * Course Outcomes Addressed:
 * - CO4: Virtual memory mapping and address space interactions.
 * - CO5: File system abstractions, Inodes, VFS metadata, and memory-mapped files (mmap, munmap, msync).
 *
 * Provides functionality to persist current system and process snapshots
 * directly into a file mapped into process memory space via mmap().
 */

#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <sys/types.h>

/**
 * @brief Persists the current monitor state using POSIX memory-mapped file I/O (mmap).
 *
 * Demonstrates:
 * 1. open() with O_RDWR | O_CREAT | O_TRUNC
 * 2. ftruncate() to size the file backing store
 * 3. mmap() with PROT_READ | PROT_WRITE and MAP_SHARED
 * 4. Writing formatted structured data into virtual memory pages
 * 5. msync() to flush dirty pages to physical storage
 * 6. munmap() and close() for resource reclamation
 *
 * @param filepath Output file path for the snapshot.
 * @return int 0 on success, -1 on failure.
 */
int save_mmap_snapshot(const char* filepath);

/**
 * @brief Inspects a file's VFS metadata and Inode number using the stat() system call.
 *
 * @param filepath Path to the file.
 * @param[out] inode Pointer to store the inode number (st_ino).
 * @param[out] size Pointer to store the file size in bytes (st_size).
 * @return int 0 on success, -1 on failure.
 */
int get_file_stat_info(const char* filepath, unsigned long* inode, off_t* size);

#endif // SNAPSHOT_H
