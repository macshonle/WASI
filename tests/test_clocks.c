/**
 * Tests for WASI Clocks Implementation
 *
 * Run: make test-clocks
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <time.h>

/* Test framework macros */
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    test_##name(); \
    printf("  %s: PASS\n", #name); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* Forward declaration for test runner */
int run_clocks_tests(void);

/* Include the generated bindings header */
#include "../build/c-bindings/clocks/imports.h"

/* ============================================================================
 * Test: Monotonic clock returns non-zero value
 * ============================================================================
 */
TEST(monotonic_now) {
    wasi_clocks_monotonic_clock_instant_t now = wasi_clocks_monotonic_clock_now();
    /* Monotonic clock should return a value > 0 (system has been running) */
    assert(now > 0);
}

/* ============================================================================
 * Test: Monotonic clock is monotonically increasing
 * ============================================================================
 */
TEST(monotonic_increases) {
    wasi_clocks_monotonic_clock_instant_t t1 = wasi_clocks_monotonic_clock_now();

    /* Busy wait for a tiny bit */
    volatile int x = 0;
    for (int i = 0; i < 100000; i++) x++;

    wasi_clocks_monotonic_clock_instant_t t2 = wasi_clocks_monotonic_clock_now();

    /* t2 should be >= t1 */
    assert(t2 >= t1);
}

/* ============================================================================
 * Test: Monotonic clock resolution is reasonable
 * ============================================================================
 */
TEST(monotonic_resolution) {
    wasi_clocks_monotonic_clock_duration_t res = wasi_clocks_monotonic_clock_resolution();
    /* Resolution should be between 1ns and 1s */
    assert(res >= 1);
    assert(res <= 1000000000ULL);
}

/* ============================================================================
 * Test: Wall clock returns reasonable value
 * ============================================================================
 */
TEST(wall_clock_now) {
    wasi_clocks_wall_clock_datetime_t now;
    wasi_clocks_wall_clock_now(&now);

    /* Should be after year 2020 (1577836800 seconds since epoch) */
    assert(now.seconds > 1577836800ULL);

    /* Nanoseconds should be less than 1 second */
    assert(now.nanoseconds < 1000000000);
}

/* ============================================================================
 * Test: Wall clock resolution is reasonable
 * ============================================================================
 */
TEST(wall_clock_resolution) {
    wasi_clocks_wall_clock_datetime_t res;
    wasi_clocks_wall_clock_resolution(&res);

    /* Resolution should be at most 1 second */
    uint64_t total_ns = res.seconds * 1000000000ULL + res.nanoseconds;
    assert(total_ns >= 1);
    assert(total_ns <= 1000000000ULL);
}

/* ============================================================================
 * Test: Subscribe instant creates valid pollable
 * ============================================================================
 */
TEST(subscribe_instant) {
    wasi_clocks_monotonic_clock_instant_t now = wasi_clocks_monotonic_clock_now();
    /* Subscribe for 10ms in the future */
    wasi_clocks_monotonic_clock_own_pollable_t pollable =
        wasi_clocks_monotonic_clock_subscribe_instant(now + 10000000ULL);

    /* Should return a valid handle */
    assert(pollable.__handle > 0);

    /* Drop the pollable */
    wasi_io_poll_pollable_drop_own((wasi_io_poll_own_pollable_t){ pollable.__handle });
}

/* ============================================================================
 * Test: Subscribe duration creates valid pollable
 * ============================================================================
 */
TEST(subscribe_duration) {
    /* Subscribe for 10ms duration */
    wasi_clocks_monotonic_clock_own_pollable_t pollable =
        wasi_clocks_monotonic_clock_subscribe_duration(10000000ULL);

    /* Should return a valid handle */
    assert(pollable.__handle > 0);

    /* Drop the pollable */
    wasi_io_poll_pollable_drop_own((wasi_io_poll_own_pollable_t){ pollable.__handle });
}

/* ============================================================================
 * Test: Timer pollable becomes ready
 * ============================================================================
 */
TEST(timer_becomes_ready) {
    /* Create pollable that should be ready immediately (time in past) */
    wasi_clocks_monotonic_clock_instant_t past = 1;  /* Very early instant */
    wasi_clocks_monotonic_clock_own_pollable_t pollable =
        wasi_clocks_monotonic_clock_subscribe_instant(past);

    assert(pollable.__handle > 0);

    /* Should be ready immediately */
    wasi_io_poll_borrow_pollable_t borrow = wasi_io_poll_borrow_pollable(
        (wasi_io_poll_own_pollable_t){ pollable.__handle });
    bool ready = wasi_io_poll_method_pollable_ready(borrow);
    assert(ready == true);

    wasi_io_poll_pollable_drop_own((wasi_io_poll_own_pollable_t){ pollable.__handle });
}

/* ============================================================================
 * Main test runner
 * ============================================================================
 */
int run_clocks_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("Running WASI Clocks tests...\n");

    RUN_TEST(monotonic_now);
    RUN_TEST(monotonic_increases);
    RUN_TEST(monotonic_resolution);
    RUN_TEST(wall_clock_now);
    RUN_TEST(wall_clock_resolution);
    RUN_TEST(subscribe_instant);
    RUN_TEST(subscribe_duration);
    RUN_TEST(timer_becomes_ready);

    return tests_failed;
}

/* Allow standalone execution */
#ifndef TEST_RUNNER_MODE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_clocks_tests();
}
#endif
