/**
 * Linux Platform Implementation for WASI
 *
 * This file provides Linux-specific implementations of platform operations.
 * Compile this file only on Linux systems.
 */

#ifdef __linux__

#define _GNU_SOURCE
#include "platform.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
#include <sys/random.h>
#include <stdlib.h>

/* ============================================================================
 * Random Number Generation
 * ============================================================================
 */

int wasi_platform_random_secure(uint8_t *buf, size_t len) {
    /* Use getrandom() syscall - available since Linux 3.17 */
    while (len > 0) {
        ssize_t result = getrandom(buf, len, 0);
        if (result < 0) {
            if (errno == EINTR) continue;  /* Interrupted, retry */
            return -1;
        }
        buf += result;
        len -= result;
    }
    return 0;
}

int wasi_platform_random_insecure(uint8_t *buf, size_t len) {
    /* For insecure random, we can use the same source but with GRND_INSECURE */
    /* If that's not available, fall back to secure random */
#ifdef GRND_INSECURE
    while (len > 0) {
        ssize_t result = getrandom(buf, len, GRND_INSECURE);
        if (result < 0) {
            if (errno == EINTR) continue;
            /* Fall back to secure if insecure fails */
            return wasi_platform_random_secure(buf, len);
        }
        buf += result;
        len -= result;
    }
    return 0;
#else
    return wasi_platform_random_secure(buf, len);
#endif
}

/* ============================================================================
 * Clock Operations
 * ============================================================================
 */

uint64_t wasi_platform_clock_monotonic(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int wasi_platform_clock_wall(uint64_t *seconds, uint32_t *nanoseconds) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return -1;
    }
    *seconds = (uint64_t)ts.tv_sec;
    *nanoseconds = (uint32_t)ts.tv_nsec;
    return 0;
}

uint64_t wasi_platform_clock_monotonic_resolution(void) {
    struct timespec res;
    if (clock_getres(CLOCK_MONOTONIC, &res) != 0) {
        return 1;  /* Default to 1ns if we can't determine */
    }
    return (uint64_t)res.tv_sec * 1000000000ULL + (uint64_t)res.tv_nsec;
}

/* ============================================================================
 * File Operations
 * ============================================================================
 */

int wasi_platform_fd_read(int fd, uint8_t *buf, size_t len, size_t *nread) {
    ssize_t result = read(fd, buf, len);
    if (result < 0) {
        *nread = 0;
        return -errno;
    }
    *nread = (size_t)result;
    return 0;
}

int wasi_platform_fd_write(int fd, const uint8_t *buf, size_t len, size_t *nwritten) {
    ssize_t result = write(fd, buf, len);
    if (result < 0) {
        *nwritten = 0;
        return -errno;
    }
    *nwritten = (size_t)result;
    return 0;
}

int wasi_platform_fd_close(int fd) {
    if (close(fd) != 0) {
        return -errno;
    }
    return 0;
}

/* ============================================================================
 * Polling Operations
 * ============================================================================
 */

int wasi_platform_poll(int *fds, size_t nfds, int64_t timeout,
                       uint32_t *ready, size_t *nready) {
    if (nfds == 0) {
        *nready = 0;
        return 0;
    }

    /* Allocate pollfd array */
    struct pollfd *pfds = malloc(nfds * sizeof(struct pollfd));
    if (!pfds) {
        return -ENOMEM;
    }

    /* Set up poll structures */
    for (size_t i = 0; i < nfds; i++) {
        pfds[i].fd = fds[i];
        pfds[i].events = POLLIN | POLLOUT;
        pfds[i].revents = 0;
    }

    /* Convert timeout to milliseconds (-1 for infinite) */
    int timeout_ms = -1;
    if (timeout >= 0) {
        timeout_ms = (int)(timeout / 1000000);  /* ns to ms */
        if (timeout_ms == 0 && timeout > 0) {
            timeout_ms = 1;  /* Minimum 1ms if non-zero */
        }
    }

    int result = poll(pfds, nfds, timeout_ms);
    if (result < 0) {
        free(pfds);
        return -errno;
    }

    /* Collect ready indices */
    *nready = 0;
    for (size_t i = 0; i < nfds && *nready < (size_t)result; i++) {
        if (pfds[i].revents != 0) {
            ready[*nready] = (uint32_t)i;
            (*nready)++;
        }
    }

    free(pfds);
    return 0;
}

/* ============================================================================
 * Environment
 * ============================================================================
 */

const char *wasi_platform_getenv(const char *name) {
    return getenv(name);
}

int wasi_platform_getcwd(char *buf, size_t size) {
    if (getcwd(buf, size) == NULL) {
        return -errno;
    }
    return 0;
}

#endif /* __linux__ */
