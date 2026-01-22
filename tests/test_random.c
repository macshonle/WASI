/**
 * Tests for WASI Random Implementation
 *
 * Run: make test-random
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

/* Test framework macros */
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* Include the generated bindings header */
#include "../build/c-bindings/random/imports.h"

/* External function declarations */
extern void wasi_random_random_get_random_bytes(uint64_t len, imports_list_u8_t *ret);
extern uint64_t wasi_random_random_get_random_u64(void);
extern void wasi_random_insecure_get_insecure_random_bytes(uint64_t len, imports_list_u8_t *ret);
extern uint64_t wasi_random_insecure_get_insecure_random_u64(void);
extern void wasi_random_insecure_seed_insecure_seed(imports_tuple2_u64_u64_t *ret);

/* ============================================================================
 * Test: Secure random u64 returns different values
 * ============================================================================
 */
TEST(random_u64_not_constant) {
    /* Get two random values - they should almost certainly be different */
    uint64_t a = wasi_random_random_get_random_u64();
    uint64_t b = wasi_random_random_get_random_u64();
    assert(a != b);
}

/* ============================================================================
 * Test: Secure random bytes fills buffer
 * ============================================================================
 */
TEST(random_bytes_fills_buffer) {
    /* Request random bytes and verify buffer is filled */
    imports_list_u8_t result;
    wasi_random_random_get_random_bytes(32, &result);
    assert(result.len == 32);
    assert(result.ptr != NULL);

    /* Check it's not all zeros (extremely unlikely with random data) */
    bool all_zeros = true;
    for (size_t i = 0; i < result.len; i++) {
        if (result.ptr[i] != 0) { all_zeros = false; break; }
    }
    assert(!all_zeros);

    imports_list_u8_free(&result);
}

/* ============================================================================
 * Test: Zero-length random bytes
 * ============================================================================
 */
TEST(random_bytes_zero_length) {
    imports_list_u8_t result;
    wasi_random_random_get_random_bytes(0, &result);
    assert(result.len == 0);
}

/* ============================================================================
 * Test: Insecure random u64 is fast and varies
 * ============================================================================
 */
TEST(insecure_random_u64) {
    uint64_t a = wasi_random_insecure_get_insecure_random_u64();
    uint64_t b = wasi_random_insecure_get_insecure_random_u64();
    assert(a != b);
}

/* ============================================================================
 * Test: Insecure random bytes
 * ============================================================================
 */
TEST(insecure_random_bytes) {
    imports_list_u8_t result;
    wasi_random_insecure_get_insecure_random_bytes(64, &result);
    assert(result.len == 64);
    assert(result.ptr != NULL);

    /* Check it's not all zeros */
    bool all_zeros = true;
    for (size_t i = 0; i < result.len; i++) {
        if (result.ptr[i] != 0) { all_zeros = false; break; }
    }
    assert(!all_zeros);

    imports_list_u8_free(&result);
}

/* ============================================================================
 * Test: Insecure seed returns two u64 values
 * ============================================================================
 */
TEST(insecure_seed) {
    imports_tuple2_u64_u64_t seed;
    wasi_random_insecure_seed_insecure_seed(&seed);
    /* The seed should not be all zeros */
    assert(seed.f0 != 0 || seed.f1 != 0);
}

/* ============================================================================
 * Test: Random distribution (basic sanity check)
 * ============================================================================
 */
TEST(random_distribution) {
    /* Generate many random bytes and check rough distribution */
    /* This is a basic sanity check, not a proper statistical test */

    const size_t NUM_BYTES = 10000;
    imports_list_u8_t result;
    wasi_random_random_get_random_bytes(NUM_BYTES, &result);
    assert(result.len == NUM_BYTES);

    /* Count bytes in each quarter of the range */
    size_t counts[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < result.len; i++) {
        counts[result.ptr[i] / 64]++;
    }

    /* Each quarter should have roughly 25% of the bytes */
    /* Allow 15-35% range (very loose check) */
    for (int i = 0; i < 4; i++) {
        double pct = (double)counts[i] / NUM_BYTES;
        assert(pct > 0.15 && pct < 0.35);
    }

    imports_list_u8_free(&result);
}

/* ============================================================================
 * Test: Large random bytes request
 * ============================================================================
 */
TEST(random_bytes_large) {
    /* Request a larger amount of random data */
    const size_t SIZE = 65536;
    imports_list_u8_t result;
    wasi_random_random_get_random_bytes(SIZE, &result);
    assert(result.len == SIZE);
    assert(result.ptr != NULL);
    imports_list_u8_free(&result);
}

/* ============================================================================
 * Main test runner
 * ============================================================================
 */
int run_random_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("Running WASI Random tests...\n");

    RUN_TEST(random_u64_not_constant);
    RUN_TEST(random_bytes_fills_buffer);
    RUN_TEST(random_bytes_zero_length);
    RUN_TEST(insecure_random_u64);
    RUN_TEST(insecure_random_bytes);
    RUN_TEST(insecure_seed);
    RUN_TEST(random_distribution);
    RUN_TEST(random_bytes_large);

    printf("\nRandom tests passed: %d\n", tests_passed);
    printf("Random tests failed: %d\n", tests_failed);

    return tests_failed;
}

/* Allow standalone execution */
#ifndef TEST_RUNNER_MODE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_random_tests();
}
#endif
