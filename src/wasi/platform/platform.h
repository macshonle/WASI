/**
 * Platform Abstraction Layer for WASI Implementation
 *
 * This header provides a unified interface for platform-specific operations.
 * Include this header in implementation files, and the correct platform
 * implementation will be linked based on the build target.
 */

#ifndef WASI_PLATFORM_H
#define WASI_PLATFORM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Platform detection macros */
#if defined(__linux__)
    #define WASI_PLATFORM_LINUX 1
    #define WASI_PLATFORM_NAME "linux"
#elif defined(__APPLE__) && defined(__MACH__)
    #define WASI_PLATFORM_DARWIN 1
    #define WASI_PLATFORM_NAME "darwin"
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #define WASI_PLATFORM_BSD 1
    #define WASI_PLATFORM_NAME "bsd"
#else
    #define WASI_PLATFORM_UNKNOWN 1
    #define WASI_PLATFORM_NAME "unknown"
#endif

/* Common POSIX availability */
#if defined(WASI_PLATFORM_LINUX) || defined(WASI_PLATFORM_DARWIN) || defined(WASI_PLATFORM_BSD)
    #define WASI_HAVE_POSIX 1
#endif

/* ============================================================================
 * Random Number Generation
 * ============================================================================
 */

/**
 * Fill buffer with cryptographically secure random bytes.
 *
 * @param buf    Buffer to fill
 * @param len    Number of bytes to generate
 * @return       0 on success, -1 on failure
 */
int wasi_platform_random_secure(uint8_t *buf, size_t len);

/**
 * Fill buffer with fast (non-cryptographic) random bytes.
 *
 * @param buf    Buffer to fill
 * @param len    Number of bytes to generate
 * @return       0 on success, -1 on failure
 */
int wasi_platform_random_insecure(uint8_t *buf, size_t len);

/* ============================================================================
 * Clock Operations
 * ============================================================================
 */

/**
 * Get monotonic clock time in nanoseconds.
 *
 * @return       Time in nanoseconds since an arbitrary point
 */
uint64_t wasi_platform_clock_monotonic(void);

/**
 * Get wall clock time.
 *
 * @param seconds      Output: seconds since Unix epoch
 * @param nanoseconds  Output: additional nanoseconds
 * @return             0 on success, -1 on failure
 */
int wasi_platform_clock_wall(uint64_t *seconds, uint32_t *nanoseconds);

/**
 * Get monotonic clock resolution in nanoseconds.
 *
 * @return       Resolution in nanoseconds
 */
uint64_t wasi_platform_clock_monotonic_resolution(void);

/* ============================================================================
 * File Operations
 * ============================================================================
 */

/**
 * Read from a file descriptor.
 *
 * @param fd     File descriptor
 * @param buf    Buffer to read into
 * @param len    Maximum bytes to read
 * @param nread  Output: actual bytes read
 * @return       0 on success, negative error code on failure
 */
int wasi_platform_fd_read(int fd, uint8_t *buf, size_t len, size_t *nread);

/**
 * Write to a file descriptor.
 *
 * @param fd       File descriptor
 * @param buf      Buffer to write from
 * @param len      Number of bytes to write
 * @param nwritten Output: actual bytes written
 * @return         0 on success, negative error code on failure
 */
int wasi_platform_fd_write(int fd, const uint8_t *buf, size_t len, size_t *nwritten);

/**
 * Close a file descriptor.
 *
 * @param fd     File descriptor
 * @return       0 on success, negative error code on failure
 */
int wasi_platform_fd_close(int fd);

/* ============================================================================
 * Polling Operations
 * ============================================================================
 */

/**
 * Poll file descriptors for readiness.
 *
 * @param fds      Array of file descriptors to poll
 * @param nfds     Number of file descriptors
 * @param timeout  Timeout in nanoseconds (-1 for infinite)
 * @param ready    Output: indices of ready file descriptors
 * @param nready   Output: number of ready file descriptors
 * @return         0 on success, negative error code on failure
 */
int wasi_platform_poll(int *fds, size_t nfds, int64_t timeout,
                       uint32_t *ready, size_t *nready);

/* ============================================================================
 * Environment
 * ============================================================================
 */

/**
 * Get environment variable.
 *
 * @param name   Variable name
 * @return       Variable value or NULL if not set
 */
const char *wasi_platform_getenv(const char *name);

/**
 * Get current working directory.
 *
 * @param buf    Buffer to store path
 * @param size   Buffer size
 * @return       0 on success, negative error code on failure
 */
int wasi_platform_getcwd(char *buf, size_t size);

#endif /* WASI_PLATFORM_H */
