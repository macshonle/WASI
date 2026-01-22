/**
 * WASI Capstone Test 2: Data Processing Pipeline
 *
 * This test exercises multiple WASI interfaces in a realistic data processing
 * scenario:
 *   - Random number generation for test data
 *   - File I/O (create, write, read, seek, truncate)
 *   - Clock measurements for performance timing
 *   - Environment variable access
 *   - Standard I/O operations
 *
 * Non-deterministic elements are handled with property-based validation:
 *   - Timestamps: validated as being within reasonable ranges
 *   - Random data: validated for statistical properties
 *   - Timing: reported but not compared exactly
 *
 * Compile (native): gcc -Wall -Wextra -std=c11 capstone_pipeline.c -o pipeline_native
 * Compile (wasi):   wasm32-wasip2-clang -Wall -Wextra capstone_pipeline.c -o pipeline.wasm
 * Run (native):     ./pipeline_native
 * Run (wasi):       wasmtime run --dir=. --env=TEST_VAR=test_value pipeline.wasm
 */

#ifdef __APPLE__
#define _DARWIN_C_SOURCE  /* Enable arc4random_buf on macOS */
#endif
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <math.h>

/* ============================================================================
 * Test Infrastructure
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_DIR "pipeline_testdir"

#define REPORT_PASS(name) do { \
    printf("  PASS: %s\n", name); \
    tests_passed++; \
} while(0)

#define REPORT_FAIL(name, reason) do { \
    printf("  FAIL: %s - %s\n", name, reason); \
    tests_failed++; \
} while(0)

#define REPORT_INFO(fmt, ...) printf("    " fmt "\n", ##__VA_ARGS__)

/* Get current time in nanoseconds (monotonic if available) */
static uint64_t get_time_ns(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Platform-specific includes for random */
#ifdef __wasi__
    #include <wasi/api.h>
#endif

/* Platform-specific random bytes */
static int get_random_bytes(void *buf, size_t len) {
#ifdef __wasi__
    /* WASI: Use __wasi_random_get from wasi/api.h */
    __wasi_errno_t err = __wasi_random_get((uint8_t *)buf, len);
    return (err == 0) ? 0 : -1;
#elif defined(__APPLE__)
    arc4random_buf(buf, len);
    return 0;
#else
    /* Linux: use /dev/urandom */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
#endif
}

/* ============================================================================
 * Test Setup and Cleanup
 * ============================================================================ */

static void cleanup_test_dir(void) {
    char path[512];

    /* Remove files */
    snprintf(path, sizeof(path), "%s/input.dat", TEST_DIR);
    unlink(path);
    snprintf(path, sizeof(path), "%s/output.dat", TEST_DIR);
    unlink(path);
    snprintf(path, sizeof(path), "%s/stats.txt", TEST_DIR);
    unlink(path);
    snprintf(path, sizeof(path), "%s/large.bin", TEST_DIR);
    unlink(path);

    /* Remove directory */
    rmdir(TEST_DIR);
}

static int setup_test_dir(void) {
    cleanup_test_dir();

    if (mkdir(TEST_DIR, 0755) != 0) {
        printf("  Failed to create test directory: %s\n", strerror(errno));
        return 0;
    }
    return 1;
}

/* ============================================================================
 * Test 1: Random Data Generation and Validation
 * ============================================================================ */

static int test_random_data_generation(void) {
    printf("\n--- Test: Random Data Generation ---\n");

    /* Generate random bytes */
    uint8_t data[1024];
    if (get_random_bytes(data, sizeof(data)) != 0) {
        REPORT_FAIL("random_generation", "Failed to get random bytes");
        return 0;
    }

    /* Validate: count byte distribution (should be roughly uniform) */
    int histogram[256] = {0};
    for (size_t i = 0; i < sizeof(data); i++) {
        histogram[data[i]]++;
    }

    /* Check that no byte value dominates (>10% of total would be suspicious) */
    int max_count = 0;
    for (int i = 0; i < 256; i++) {
        if (histogram[i] > max_count) max_count = histogram[i];
    }

    REPORT_INFO("Generated %zu random bytes", sizeof(data));
    REPORT_INFO("Max frequency for any byte value: %d (threshold: %zu)",
                max_count, sizeof(data) / 10);

    if (max_count > (int)(sizeof(data) / 10)) {
        REPORT_FAIL("random_distribution", "Byte distribution too skewed");
        return 0;
    }

    /* Validate: data should not be all zeros or all same value */
    int all_same = 1;
    for (size_t i = 1; i < sizeof(data); i++) {
        if (data[i] != data[0]) {
            all_same = 0;
            break;
        }
    }

    if (all_same) {
        REPORT_FAIL("random_variety", "All bytes are identical");
        return 0;
    }

    REPORT_PASS("random_data_generation");
    return 1;
}

/* ============================================================================
 * Test 2: File Write Pipeline
 * ============================================================================ */

static int test_file_write_pipeline(void) {
    printf("\n--- Test: File Write Pipeline ---\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/input.dat", TEST_DIR);

    /* Create and write data */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        REPORT_FAIL("file_create", strerror(errno));
        return 0;
    }

    /* Generate structured test data: header + payload */
    uint8_t header[16] = "PIPELINE_V1\0\0\0\0";
    uint32_t payload_size = 4096;
    memcpy(header + 12, &payload_size, sizeof(payload_size));

    if (write(fd, header, sizeof(header)) != sizeof(header)) {
        REPORT_FAIL("write_header", strerror(errno));
        close(fd);
        return 0;
    }

    /* Write payload: sequence of incrementing bytes */
    uint8_t *payload = (uint8_t *)malloc(payload_size);
    if (!payload) {
        REPORT_FAIL("malloc", "Out of memory");
        close(fd);
        return 0;
    }

    for (uint32_t i = 0; i < payload_size; i++) {
        payload[i] = (uint8_t)(i % 256);
    }

    ssize_t written = write(fd, payload, payload_size);
    free(payload);

    if (written != (ssize_t)payload_size) {
        REPORT_FAIL("write_payload", strerror(errno));
        close(fd);
        return 0;
    }

    /* Sync and close */
    fsync(fd);
    close(fd);

    /* Verify file size */
    struct stat st;
    if (stat(path, &st) != 0) {
        REPORT_FAIL("stat", strerror(errno));
        return 0;
    }

    size_t expected_size = sizeof(header) + payload_size;
    REPORT_INFO("Written file size: %lld bytes (expected: %zu)",
                (long long)st.st_size, expected_size);

    if ((size_t)st.st_size != expected_size) {
        REPORT_FAIL("file_size", "Size mismatch");
        return 0;
    }

    REPORT_PASS("file_write_pipeline");
    return 1;
}

/* ============================================================================
 * Test 3: File Read and Transform Pipeline
 * ============================================================================ */

static int test_file_read_transform(void) {
    printf("\n--- Test: File Read and Transform ---\n");

    char input_path[512], output_path[512];
    snprintf(input_path, sizeof(input_path), "%s/input.dat", TEST_DIR);
    snprintf(output_path, sizeof(output_path), "%s/output.dat", TEST_DIR);

    /* Open input file */
    int in_fd = open(input_path, O_RDONLY);
    if (in_fd < 0) {
        REPORT_FAIL("open_input", strerror(errno));
        return 0;
    }

    /* Read header */
    uint8_t header[16];
    if (read(in_fd, header, sizeof(header)) != sizeof(header)) {
        REPORT_FAIL("read_header", strerror(errno));
        close(in_fd);
        return 0;
    }

    /* Validate header magic */
    if (memcmp(header, "PIPELINE_V1", 11) != 0) {
        REPORT_FAIL("header_magic", "Invalid header");
        close(in_fd);
        return 0;
    }

    uint32_t payload_size;
    memcpy(&payload_size, header + 12, sizeof(payload_size));
    REPORT_INFO("Input payload size: %u bytes", payload_size);

    /* Read payload */
    uint8_t *payload = (uint8_t *)malloc(payload_size);
    if (!payload) {
        REPORT_FAIL("malloc", "Out of memory");
        close(in_fd);
        return 0;
    }

    ssize_t nread = read(in_fd, payload, payload_size);
    close(in_fd);

    if (nread != (ssize_t)payload_size) {
        REPORT_FAIL("read_payload", "Short read");
        free(payload);
        return 0;
    }

    /* Transform: XOR each byte with 0x55 (invertible transform) */
    for (uint32_t i = 0; i < payload_size; i++) {
        payload[i] ^= 0x55;
    }

    /* Write transformed output */
    int out_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        REPORT_FAIL("open_output", strerror(errno));
        free(payload);
        return 0;
    }

    /* Update header for output */
    memcpy(header, "XFORMED_V1\0\0", 12);

    if (write(out_fd, header, sizeof(header)) != sizeof(header)) {
        REPORT_FAIL("write_out_header", strerror(errno));
        close(out_fd);
        free(payload);
        return 0;
    }

    if (write(out_fd, payload, payload_size) != (ssize_t)payload_size) {
        REPORT_FAIL("write_out_payload", strerror(errno));
        close(out_fd);
        free(payload);
        return 0;
    }

    close(out_fd);
    free(payload);

    REPORT_INFO("Transform complete: XOR 0x55 applied to %u bytes", payload_size);
    REPORT_PASS("file_read_transform");
    return 1;
}

/* ============================================================================
 * Test 4: Seek and Partial Read
 * ============================================================================ */

static int test_seek_and_partial_read(void) {
    printf("\n--- Test: Seek and Partial Read ---\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/input.dat", TEST_DIR);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        REPORT_FAIL("open", strerror(errno));
        return 0;
    }

    /* Seek to middle of payload (skip header + 2048 bytes) */
    off_t seek_pos = 16 + 2048;
    off_t actual = lseek(fd, seek_pos, SEEK_SET);
    if (actual != seek_pos) {
        REPORT_FAIL("seek", "Seek position mismatch");
        close(fd);
        return 0;
    }

    /* Read a small chunk */
    uint8_t chunk[64];
    ssize_t nread = read(fd, chunk, sizeof(chunk));
    if (nread != sizeof(chunk)) {
        REPORT_FAIL("read_chunk", "Short read");
        close(fd);
        return 0;
    }

    /* Validate: at position 2048 in payload, bytes should be (2048 % 256), (2049 % 256), ... */
    int valid = 1;
    for (size_t i = 0; i < sizeof(chunk); i++) {
        uint8_t expected = (uint8_t)((2048 + i) % 256);
        if (chunk[i] != expected) {
            REPORT_INFO("Byte mismatch at offset %zu: got %u, expected %u",
                        i, chunk[i], expected);
            valid = 0;
            break;
        }
    }

    /* Seek to end and verify position */
    off_t end_pos = lseek(fd, 0, SEEK_END);
    off_t expected_end = 16 + 4096;  /* header + payload */

    REPORT_INFO("File end position: %lld (expected: %lld)",
                (long long)end_pos, (long long)expected_end);

    close(fd);

    if (!valid || end_pos != expected_end) {
        REPORT_FAIL("seek_and_partial_read", "Data or position mismatch");
        return 0;
    }

    REPORT_PASS("seek_and_partial_read");
    return 1;
}

/* ============================================================================
 * Test 5: Timing Measurement (Non-deterministic - property based)
 * ============================================================================ */

static int test_timing_measurement(void) {
    printf("\n--- Test: Timing Measurement ---\n");

    uint64_t start = get_time_ns();

    /* Do some measurable work: write and read a file */
    char path[512];
    snprintf(path, sizeof(path), "%s/large.bin", TEST_DIR);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        REPORT_FAIL("open_large", strerror(errno));
        return 0;
    }

    /* Write 64KB of data */
    uint8_t *buffer = (uint8_t *)malloc(65536);
    if (!buffer) {
        REPORT_FAIL("malloc", "Out of memory");
        close(fd);
        return 0;
    }

    memset(buffer, 0xAB, 65536);
    ssize_t written = write(fd, buffer, 65536);
    fsync(fd);
    close(fd);

    if (written != 65536) {
        REPORT_FAIL("write_large", "Short write");
        free(buffer);
        return 0;
    }

    /* Read it back */
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        REPORT_FAIL("reopen_large", strerror(errno));
        free(buffer);
        return 0;
    }

    ssize_t nread = read(fd, buffer, 65536);
    close(fd);

    uint64_t end = get_time_ns();
    uint64_t elapsed_ns = end - start;
    double elapsed_ms = (double)elapsed_ns / 1000000.0;

    free(buffer);

    if (nread != 65536) {
        REPORT_FAIL("read_large", "Short read");
        return 0;
    }

    /* Property check: timing should be positive and reasonable (<10 seconds) */
    REPORT_INFO("Elapsed time: %.3f ms", elapsed_ms);
    REPORT_INFO("Timing validity: %s",
                (elapsed_ns > 0 && elapsed_ns < 10000000000ULL) ? "VALID" : "SUSPICIOUS");

    if (elapsed_ns == 0 || elapsed_ns >= 10000000000ULL) {
        REPORT_FAIL("timing_measurement", "Elapsed time out of expected range");
        return 0;
    }

    REPORT_PASS("timing_measurement");
    return 1;
}

/* ============================================================================
 * Test 6: Statistics Generation Pipeline
 * ============================================================================ */

static int test_statistics_pipeline(void) {
    printf("\n--- Test: Statistics Pipeline ---\n");

    char input_path[512], stats_path[512];
    snprintf(input_path, sizeof(input_path), "%s/input.dat", TEST_DIR);
    snprintf(stats_path, sizeof(stats_path), "%s/stats.txt", TEST_DIR);

    /* Read input file and compute statistics */
    int in_fd = open(input_path, O_RDONLY);
    if (in_fd < 0) {
        REPORT_FAIL("open_input", strerror(errno));
        return 0;
    }

    /* Skip header */
    lseek(in_fd, 16, SEEK_SET);

    /* Read all payload */
    uint8_t buffer[4096];
    ssize_t nread = read(in_fd, buffer, sizeof(buffer));
    close(in_fd);

    if (nread <= 0) {
        REPORT_FAIL("read_input", "Failed to read payload");
        return 0;
    }

    /* Compute statistics */
    uint64_t sum = 0;
    uint8_t min_val = 255, max_val = 0;
    int histogram[256] = {0};

    for (ssize_t i = 0; i < nread; i++) {
        sum += buffer[i];
        if (buffer[i] < min_val) min_val = buffer[i];
        if (buffer[i] > max_val) max_val = buffer[i];
        histogram[buffer[i]]++;
    }

    double mean = (double)sum / (double)nread;

    /* Compute standard deviation */
    double variance = 0;
    for (ssize_t i = 0; i < nread; i++) {
        double diff = (double)buffer[i] - mean;
        variance += diff * diff;
    }
    variance /= (double)nread;
    double stddev = sqrt(variance);

    /* Count unique values */
    int unique = 0;
    for (int i = 0; i < 256; i++) {
        if (histogram[i] > 0) unique++;
    }

    REPORT_INFO("Statistics for %zd bytes:", nread);
    REPORT_INFO("  Min: %u, Max: %u", min_val, max_val);
    REPORT_INFO("  Mean: %.2f, StdDev: %.2f", mean, stddev);
    REPORT_INFO("  Unique values: %d", unique);

    /* Write statistics to file */
    int out_fd = open(stats_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        REPORT_FAIL("open_stats", strerror(errno));
        return 0;
    }

    char stats_buf[512];
    int stats_len = snprintf(stats_buf, sizeof(stats_buf),
        "bytes=%zd\n"
        "min=%u\n"
        "max=%u\n"
        "mean=%.2f\n"
        "stddev=%.2f\n"
        "unique=%d\n",
        nread, min_val, max_val, mean, stddev, unique);

    if (write(out_fd, stats_buf, (size_t)stats_len) != stats_len) {
        REPORT_FAIL("write_stats", strerror(errno));
        close(out_fd);
        return 0;
    }

    close(out_fd);

    /* Validate expected statistics for sequence 0,1,2,...,255,0,1,... repeated */
    /* For 4096 bytes: 16 complete cycles of 0-255 */
    /* Expected mean: 127.5, min: 0, max: 255, unique: 256 */
    int stats_valid = (min_val == 0 && max_val == 255 && unique == 256);
    double expected_mean = 127.5;
    stats_valid = stats_valid && (mean > expected_mean - 0.1 && mean < expected_mean + 0.1);

    if (!stats_valid) {
        REPORT_FAIL("statistics_pipeline", "Statistics don't match expected values");
        return 0;
    }

    REPORT_PASS("statistics_pipeline");
    return 1;
}

/* ============================================================================
 * Test 7: Environment Variable Access
 * ============================================================================ */

static int test_environment_access(void) {
    printf("\n--- Test: Environment Access ---\n");

    /* Check for TEST_VAR environment variable (may or may not be set) */
    char *test_var = getenv("TEST_VAR");
    if (test_var) {
        REPORT_INFO("TEST_VAR is set: '%s'", test_var);
    } else {
        REPORT_INFO("TEST_VAR is not set (this is OK)");
    }

    /* Check for PATH (should exist on most systems, might be limited in WASI) */
    char *path = getenv("PATH");
    if (path) {
        /* Just report presence, don't compare exact value */
        size_t path_len = strlen(path);
        REPORT_INFO("PATH is set (length: %zu)", path_len);
    } else {
        REPORT_INFO("PATH is not set (may be normal in sandboxed WASI)");
    }

    /* Count total environment variables */
    extern char **environ;
    int env_count = 0;
    if (environ) {
        for (char **env = environ; *env != NULL; env++) {
            env_count++;
        }
    }
    REPORT_INFO("Total environment variables: %d", env_count);

    /* Property check: should have at least 0 vars (WASI might have none) */
    /* This test passes as long as getenv doesn't crash */

    REPORT_PASS("environment_access");
    return 1;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(void) {
    printf("========================================\n");
    printf("WASI Capstone Test 2: Data Pipeline\n");
    printf("========================================\n");

#ifdef __wasi__
    printf("Platform: WASI (WebAssembly)\n");
#elif defined(__APPLE__)
    printf("Platform: Native macOS\n");
#else
    printf("Platform: Native Linux\n");
#endif

    /* Setup */
    if (!setup_test_dir()) {
        printf("\nFailed to set up test environment\n");
        return 1;
    }
    printf("  Test directory created: %s/\n", TEST_DIR);

    /* Run tests */
    test_random_data_generation();
    test_file_write_pipeline();
    test_file_read_transform();
    test_seek_and_partial_read();
    test_timing_measurement();
    test_statistics_pipeline();
    test_environment_access();

    /* Cleanup */
    printf("\n--- Cleanup ---\n");
    cleanup_test_dir();
    printf("  Test directory removed\n");

    /* Summary */
    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
