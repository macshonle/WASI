/**
 * Darwin (macOS) Platform Implementation for WASI
 *
 * This file provides macOS-specific implementations of platform operations.
 * Compile this file only on macOS systems.
 */

#if defined(__APPLE__) && defined(__MACH__)

#include "platform.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
#include <stdlib.h>
#include <mach/mach_time.h>

/* ============================================================================
 * Random Number Generation
 * ============================================================================
 */

int wasi_platform_random_secure(uint8_t *buf, size_t len) {
    /* arc4random_buf is the preferred API on macOS */
    /* It always succeeds and is cryptographically secure */
    arc4random_buf(buf, len);
    return 0;
}

int wasi_platform_random_insecure(uint8_t *buf, size_t len) {
    /* On macOS, arc4random is fast enough for insecure use too */
    arc4random_buf(buf, len);
    return 0;
}

/* ============================================================================
 * Clock Operations
 * ============================================================================
 */

/* Mach timebase info for converting to nanoseconds */
static mach_timebase_info_data_t timebase_info = {0, 0};

static void ensure_timebase_info(void) {
    if (timebase_info.denom == 0) {
        mach_timebase_info(&timebase_info);
    }
}

uint64_t wasi_platform_clock_monotonic(void) {
    ensure_timebase_info();

    uint64_t mach_time = mach_absolute_time();

    /* Convert to nanoseconds */
    return mach_time * timebase_info.numer / timebase_info.denom;
}

int wasi_platform_clock_wall(uint64_t *seconds, uint32_t *nanoseconds) {
    /* clock_gettime is available on macOS 10.12+ */
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return -1;
    }
    *seconds = (uint64_t)ts.tv_sec;
    *nanoseconds = (uint32_t)ts.tv_nsec;
    return 0;
}

uint64_t wasi_platform_clock_monotonic_resolution(void) {
    ensure_timebase_info();

    /* The resolution is essentially the timebase ratio */
    /* Most Macs have 1ns resolution nowadays */
    return timebase_info.numer / timebase_info.denom;
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

#endif /* __APPLE__ && __MACH__ */
