/**
 * WASI Compatibility Header
 *
 * This header provides portable macros and functions for writing code
 * that works correctly on both native POSIX systems and WASI environments.
 *
 * Key differences addressed:
 * 1. Directory entry type constants (d_type) differ between POSIX and WASI
 * 2. File permission bits are not exposed in WASI sandboxed environments
 * 3. Inode numbers are virtualized in WASI
 *
 * Usage:
 *   #include "wasi_compat.h"
 *
 *   // Instead of: if (entry->d_type == DT_REG)
 *   // Use:
 *   if (wasi_is_regular_file(entry->d_type)) { ... }
 *
 *   // Or use stat() for maximum portability:
 *   struct stat st;
 *   if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) { ... }
 */

#ifndef WASI_COMPAT_H
#define WASI_COMPAT_H

#include <sys/stat.h>
#include <dirent.h>

/*
 * Directory Entry Type Constants Comparison:
 *
 * | Type             | POSIX (Linux) | WASI     |
 * |------------------|---------------|----------|
 * | Unknown          | DT_UNKNOWN=0  | 0        |
 * | Block Device     | DT_BLK=6      | 1        |
 * | Char Device      | DT_CHR=2      | 2        |
 * | Directory        | DT_DIR=4      | 3        |
 * | Regular File     | DT_REG=8      | 4        |
 * | Socket (dgram)   | -             | 5        |
 * | Socket (stream)  | DT_SOCK=12    | 6        |
 * | Symbolic Link    | DT_LNK=10     | 7        |
 * | FIFO             | DT_FIFO=1     | 14       |
 *
 * WARNING: Do NOT compare d_type values directly against DT_* constants
 * when writing portable code. Use the helper functions below or stat().
 */

#ifdef __wasi__

/* WASI-specific d_type values (from WASI Preview 2 specification) */
#define WASI_DTYPE_UNKNOWN        0
#define WASI_DTYPE_BLOCK_DEVICE   1
#define WASI_DTYPE_CHAR_DEVICE    2
#define WASI_DTYPE_DIRECTORY      3
#define WASI_DTYPE_REGULAR_FILE   4
#define WASI_DTYPE_SOCKET_DGRAM   5
#define WASI_DTYPE_SOCKET_STREAM  6
#define WASI_DTYPE_SYMBOLIC_LINK  7
#define WASI_DTYPE_FIFO           14

static inline int wasi_is_directory(unsigned char d_type) {
    return d_type == WASI_DTYPE_DIRECTORY;
}

static inline int wasi_is_regular_file(unsigned char d_type) {
    return d_type == WASI_DTYPE_REGULAR_FILE;
}

static inline int wasi_is_symlink(unsigned char d_type) {
    return d_type == WASI_DTYPE_SYMBOLIC_LINK;
}

static inline int wasi_is_block_device(unsigned char d_type) {
    return d_type == WASI_DTYPE_BLOCK_DEVICE;
}

static inline int wasi_is_char_device(unsigned char d_type) {
    return d_type == WASI_DTYPE_CHAR_DEVICE;
}

static inline int wasi_is_fifo(unsigned char d_type) {
    return d_type == WASI_DTYPE_FIFO;
}

static inline int wasi_is_socket(unsigned char d_type) {
    return d_type == WASI_DTYPE_SOCKET_DGRAM ||
           d_type == WASI_DTYPE_SOCKET_STREAM;
}

#else /* Native POSIX */

static inline int wasi_is_directory(unsigned char d_type) {
    return d_type == DT_DIR;
}

static inline int wasi_is_regular_file(unsigned char d_type) {
    return d_type == DT_REG;
}

static inline int wasi_is_symlink(unsigned char d_type) {
    return d_type == DT_LNK;
}

static inline int wasi_is_block_device(unsigned char d_type) {
    return d_type == DT_BLK;
}

static inline int wasi_is_char_device(unsigned char d_type) {
    return d_type == DT_CHR;
}

static inline int wasi_is_fifo(unsigned char d_type) {
    return d_type == DT_FIFO;
}

static inline int wasi_is_socket(unsigned char d_type) {
    return d_type == DT_SOCK;
}

#endif /* __wasi__ */

/*
 * Portable file type checking using stat()
 *
 * This is the RECOMMENDED approach for maximum portability.
 * These functions use stat() to determine file type, which works
 * correctly on all platforms.
 *
 * Example:
 *   if (wasi_stat_is_directory(path)) {
 *       // path is a directory
 *   }
 */

static inline int wasi_stat_is_directory(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static inline int wasi_stat_is_regular_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static inline int wasi_stat_is_symlink(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

/*
 * Permission Bits Notice
 *
 * WASI does not expose file permission bits for security reasons.
 * On WASI, (st_mode & 0777) will always return 0.
 *
 * The file TYPE bits (S_ISDIR, S_ISREG, etc.) work correctly on all platforms.
 *
 * If you need to check file accessibility, use access() instead of permission bits:
 *   if (access(path, R_OK) == 0) { // file is readable }
 *   if (access(path, W_OK) == 0) { // file is writable }
 *   if (access(path, X_OK) == 0) { // file is executable }
 */

/*
 * Inode Numbers Notice
 *
 * WASI virtualizes inode numbers. They may be large pseudo-random values
 * that can change between program invocations.
 *
 * Do NOT use inode numbers for:
 * - Persistent file identification
 * - File equality testing across runs
 * - Anything requiring stable inode values
 *
 * For file identification, consider:
 * - Using file paths
 * - Computing file hashes
 * - Using application-specific identifiers
 */

#endif /* WASI_COMPAT_H */
