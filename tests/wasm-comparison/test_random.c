/**
 * WASI Comparison Tests - Random Number Generation
 *
 * These tests use standard C APIs that map to WASI random interfaces
 * when compiled for WebAssembly. They can be run both natively and
 * in Wasmtime to compare behavior.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* Platform-specific random includes */
#ifdef __wasi__
    /* WASI provides random_get via wasi/api.h */
    #include <wasi/api.h>
#elif defined(__APPLE__)
    /* arc4random_buf is in stdlib.h on macOS (already included) */
#elif defined(__linux__)
    #include <sys/random.h>  /* for getrandom() */
#endif

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

/* Get random bytes - platform-specific implementations */
static int get_random_bytes(void *buf, size_t len) {
#ifdef __wasi__
    /* WASI provides __wasi_random_get via wasi/api.h */
    __wasi_errno_t err = __wasi_random_get((uint8_t *)buf, len);
    return (err == 0) ? 0 : -1;
#elif defined(__APPLE__)
    /* macOS provides arc4random_buf in stdlib.h */
    arc4random_buf(buf, len);
    return 0;
#elif defined(__linux__)
    /* Linux uses getrandom syscall */
    ssize_t ret = getrandom(buf, len, 0);
    return (ret == (ssize_t)len) ? 0 : -1;
#else
    #error "Unsupported platform for random number generation"
#endif
}

/* Test: Random bytes are generated */
void test_random_bytes_generated(void) {
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));

    int ret = get_random_bytes(buf, sizeof(buf));
    TEST_ASSERT(ret == 0, "get_random_bytes failed");

    /* Check that not all bytes are zero (extremely unlikely for real random) */
    int all_zero = 1;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    TEST_ASSERT(!all_zero, "random bytes all zero");

    TEST_PASS("random_bytes_generated");
}

/* Test: Consecutive calls produce different values */
void test_random_not_constant(void) {
    uint64_t a, b;

    get_random_bytes(&a, sizeof(a));
    get_random_bytes(&b, sizeof(b));

    TEST_ASSERT(a != b, "consecutive random calls returned same value");
    TEST_PASS("random_not_constant");
}

/* Test: Random distribution is reasonable (chi-square-like test) */
void test_random_distribution(void) {
    int buckets[256] = {0};
    uint8_t buf[2560];  /* 10 samples per bucket on average */

    get_random_bytes(buf, sizeof(buf));

    for (size_t i = 0; i < sizeof(buf); i++) {
        buckets[buf[i]]++;
    }

    /* Check that no bucket has more than 5x the average (very loose check) */
    int avg = sizeof(buf) / 256;  /* 10 */
    int bad_buckets = 0;
    for (int i = 0; i < 256; i++) {
        if (buckets[i] > avg * 5) {
            bad_buckets++;
        }
    }

    TEST_ASSERT(bad_buckets < 10, "too many outlier buckets in distribution");
    TEST_PASS("random_distribution");
}

/* Test: Zero-length request succeeds */
void test_random_zero_length(void) {
    uint8_t buf[1] = {0xAB};
    int ret = get_random_bytes(buf, 0);
    TEST_ASSERT(ret == 0, "zero-length random request failed");
    TEST_ASSERT(buf[0] == 0xAB, "zero-length request modified buffer");
    TEST_PASS("random_zero_length");
}

/* Test: Large random request */
void test_random_large(void) {
    size_t size = 65536;  /* 64KB */
    uint8_t *buf = (uint8_t *)malloc(size);
    TEST_ASSERT(buf != NULL, "malloc failed");

    memset(buf, 0, size);
    int ret = get_random_bytes(buf, size);
    TEST_ASSERT(ret == 0, "large random request failed");

    /* Check entropy - count unique byte values */
    int seen[256] = {0};
    for (size_t i = 0; i < size; i++) {
        seen[buf[i]] = 1;
    }
    int unique = 0;
    for (int i = 0; i < 256; i++) {
        if (seen[i]) unique++;
    }

    /* With 64KB of random data, we should see almost all 256 byte values */
    TEST_ASSERT(unique >= 250, "insufficient entropy in large random data");

    free(buf);
    TEST_PASS("random_large");
}

int main(void) {
    printf("=== Random Comparison Tests ===\n");
#ifdef __wasi__
    printf("Platform: WASI (WebAssembly)\n");
#elif defined(__APPLE__)
    printf("Platform: Native macOS\n");
#else
    printf("Platform: Native Linux\n");
#endif
    printf("\n");

    test_random_bytes_generated();
    test_random_not_constant();
    test_random_distribution();
    test_random_zero_length();
    test_random_large();

    printf("\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
