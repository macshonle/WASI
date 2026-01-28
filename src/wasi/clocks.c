/**
 * WASI Clocks Implementation
 *
 * This file implements the wasi:clocks interfaces for macOS (UNIX) and GNU/Linux.
 *
 * Interfaces implemented:
 *   - wasi:clocks/monotonic-clock@0.2.0  - Monotonic time
 *   - wasi:clocks/wall-clock@0.2.0       - Wall clock time
 */


#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "platform/platform.h"

/* Include the generated bindings header */
#include "../../build/c-bindings/clocks/imports.h"

/* External declarations from io.c for pollable creation */
extern int32_t wasi_io_poll_create_instant_pollable(uint64_t when);
extern int32_t wasi_io_poll_create_duration_pollable(uint64_t duration_ns);

/* ============================================================================
 * wasi:clocks/monotonic-clock Implementation
 * ============================================================================
 */

/**
 * Read the current value of the clock.
 * Returns time in nanoseconds since an unspecified epoch.
 */
wasi_clocks_monotonic_clock_instant_t wasi_clocks_monotonic_clock_now(void) {
    return wasi_platform_clock_monotonic();
}

/**
 * Query the resolution of the clock.
 * Returns the duration of time corresponding to a clock tick.
 */
wasi_clocks_monotonic_clock_duration_t wasi_clocks_monotonic_clock_resolution(void) {
    /* On most systems, monotonic clock has nanosecond resolution */
    /* We can query the actual resolution using clock_getres */
    struct timespec res;
    if (clock_getres(CLOCK_MONOTONIC, &res) == 0) {
        return (uint64_t)res.tv_sec * 1000000000ULL + (uint64_t)res.tv_nsec;
    }
    /* Default to 1 nanosecond if we can't determine */
    return 1;
}

/**
 * Create a pollable which will resolve once the specified instant occurred.
 */
wasi_clocks_monotonic_clock_own_pollable_t wasi_clocks_monotonic_clock_subscribe_instant(
    wasi_clocks_monotonic_clock_instant_t when
) {
    int32_t handle = wasi_io_poll_create_instant_pollable(when);
    return (wasi_clocks_monotonic_clock_own_pollable_t){ handle };
}

/**
 * Create a pollable which will resolve once the given duration has elapsed.
 */
wasi_clocks_monotonic_clock_own_pollable_t wasi_clocks_monotonic_clock_subscribe_duration(
    wasi_clocks_monotonic_clock_duration_t when
) {
    int32_t handle = wasi_io_poll_create_duration_pollable(when);
    return (wasi_clocks_monotonic_clock_own_pollable_t){ handle };
}

/* ============================================================================
 * wasi:clocks/wall-clock Implementation
 * ============================================================================
 */

/**
 * Read the current value of the clock.
 * Returns seconds and nanoseconds since Unix epoch.
 */
void wasi_clocks_wall_clock_now(wasi_clocks_wall_clock_datetime_t *ret) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        ret->seconds = (uint64_t)ts.tv_sec;
        ret->nanoseconds = (uint32_t)ts.tv_nsec;
    } else {
        /* Fallback to time() if clock_gettime fails */
        ret->seconds = (uint64_t)time(NULL);
        ret->nanoseconds = 0;
    }
}

/**
 * Query the resolution of the clock.
 */
void wasi_clocks_wall_clock_resolution(wasi_clocks_wall_clock_datetime_t *ret) {
    struct timespec res;
    if (clock_getres(CLOCK_REALTIME, &res) == 0) {
        ret->seconds = (uint64_t)res.tv_sec;
        ret->nanoseconds = (uint32_t)res.tv_nsec;
    } else {
        /* Default to 1 nanosecond */
        ret->seconds = 0;
        ret->nanoseconds = 1;
    }
}
