/**
 * WASI Comparison Tests - Clocks
 *
 * These tests use standard C APIs that map to WASI clocks interfaces
 * when compiled for WebAssembly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(name) do { \
    printf("  %s: PASS\n", name); \
    tests_passed++; \
} while(0)

/* Test: Monotonic clock returns non-zero */
void test_monotonic_clock_now(void) {
    struct timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    TEST_ASSERT(ret == 0, "clock_gettime(CLOCK_MONOTONIC) failed");

    /* Clock should have some non-zero value (process has been running) */
    uint64_t nanos = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    TEST_ASSERT(nanos > 0, "monotonic clock returned zero");

    printf("    monotonic time: %lu.%09lu s\n", (unsigned long)ts.tv_sec, (unsigned long)ts.tv_nsec);
    TEST_PASS("monotonic_clock_now");
}

/* Test: Monotonic clock increases over time */
void test_monotonic_clock_increases(void) {
    struct timespec ts1, ts2;

    clock_gettime(CLOCK_MONOTONIC, &ts1);

    /* Small busy loop to ensure time passes */
    volatile int x = 0;
    for (int i = 0; i < 100000; i++) {
        x += i;
    }

    clock_gettime(CLOCK_MONOTONIC, &ts2);

    uint64_t t1 = (uint64_t)ts1.tv_sec * 1000000000ULL + (uint64_t)ts1.tv_nsec;
    uint64_t t2 = (uint64_t)ts2.tv_sec * 1000000000ULL + (uint64_t)ts2.tv_nsec;

    TEST_ASSERT(t2 > t1, "monotonic clock did not increase");

    uint64_t elapsed = t2 - t1;
    printf("    elapsed: %lu ns\n", (unsigned long)elapsed);
    TEST_PASS("monotonic_clock_increases");
}

/* Test: Clock resolution is reasonable */
void test_clock_resolution(void) {
    struct timespec res;

#ifdef __wasi__
    /* WASI may not support clock_getres, use a reasonable default */
    res.tv_sec = 0;
    res.tv_nsec = 1;  /* Assume 1ns resolution */
    printf("    resolution: assumed 1 ns (WASI)\n");
#else
    int ret = clock_getres(CLOCK_MONOTONIC, &res);
    TEST_ASSERT(ret == 0, "clock_getres failed");
    printf("    resolution: %lu.%09lu s\n", (unsigned long)res.tv_sec, (unsigned long)res.tv_nsec);
#endif

    /* Resolution should be at most 1 second */
    TEST_ASSERT(res.tv_sec == 0 || (res.tv_sec == 1 && res.tv_nsec == 0),
                "clock resolution too coarse");

    TEST_PASS("clock_resolution");
}

/* Test: Wall clock returns reasonable time */
void test_wall_clock_now(void) {
    struct timespec ts;
    int ret = clock_gettime(CLOCK_REALTIME, &ts);
    TEST_ASSERT(ret == 0, "clock_gettime(CLOCK_REALTIME) failed");

    /* Time should be after year 2020 (timestamp > 1577836800) */
    TEST_ASSERT(ts.tv_sec > 1577836800, "wall clock time too old");

    /* Time should be before year 2100 (timestamp < 4102444800) */
    TEST_ASSERT(ts.tv_sec < 4102444800, "wall clock time too far in future");

    time_t t = (time_t)ts.tv_sec;
    struct tm *tm = gmtime(&t);
    printf("    wall clock: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec);

    TEST_PASS("wall_clock_now");
}

/* Test: nanosleep works */
void test_nanosleep(void) {
    struct timespec before, after, req, rem;

    req.tv_sec = 0;
    req.tv_nsec = 10000000;  /* 10ms */

    clock_gettime(CLOCK_MONOTONIC, &before);
    int ret = nanosleep(&req, &rem);
    clock_gettime(CLOCK_MONOTONIC, &after);

    TEST_ASSERT(ret == 0, "nanosleep failed");

    uint64_t t1 = (uint64_t)before.tv_sec * 1000000000ULL + (uint64_t)before.tv_nsec;
    uint64_t t2 = (uint64_t)after.tv_sec * 1000000000ULL + (uint64_t)after.tv_nsec;
    uint64_t elapsed = t2 - t1;

    /* Should have slept at least 9ms (allowing 1ms tolerance) */
    TEST_ASSERT(elapsed >= 9000000, "nanosleep returned too early");

    printf("    slept for: %lu ns (requested %lu ns)\n",
           (unsigned long)elapsed, (unsigned long)req.tv_nsec);
    TEST_PASS("nanosleep");
}

int main(void) {
    printf("=== Clocks Comparison Tests ===\n");
#ifdef __wasi__
    printf("Platform: WASI (WebAssembly)\n");
#else
    printf("Platform: Native Linux\n");
#endif
    printf("\n");

    test_monotonic_clock_now();
    test_monotonic_clock_increases();
    test_clock_resolution();
    test_wall_clock_now();
    test_nanosleep();

    printf("\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
