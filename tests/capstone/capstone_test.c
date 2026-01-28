/**
 * WASI Capstone Integration Test - Native C Version
 *
 * This test demonstrates comprehensive WASI API usage and produces
 * deterministic output that can be compared with the WebAssembly version.
 *
 * This native version uses standard POSIX APIs. When compiled to WebAssembly
 * using wasi-sdk, these same APIs map to WASI system calls.
 *
 * Compile: gcc capstone_test.c -o capstone_native
 * Run: ./capstone_native
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

/* Use fixed seed for deterministic "random" in tests */
static unsigned int test_seed = 12345;

static unsigned int pseudo_random(void) {
    test_seed = test_seed * 1103515245 + 12345;
    return (test_seed >> 16) & 0x7fff;
}

/* Test state tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        tests_failed++; \
        return 0; \
    } \
} while(0)

#define TEST_PASS(name) do { \
    printf("  PASS: %s\n", name); \
    tests_passed++; \
    return 1; \
} while(0)

/* ============================================================================
 * Environment Setup - Create test directory structure
 * ============================================================================ */

static const char *TEST_ROOT = "capstone_testdir";

static int setup_test_environment(void) {
    printf("\n=== Setting Up Test Environment ===\n");

    char path[256];

    /* Remove any existing test directory (ignore errors) */
    snprintf(path, sizeof(path), "%s/subdir2/nested", TEST_ROOT);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/subdir2", TEST_ROOT);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/subdir1/nested_file.txt", TEST_ROOT);
    unlink(path);
    snprintf(path, sizeof(path), "%s/subdir1", TEST_ROOT);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/file1.txt", TEST_ROOT);
    unlink(path);
    snprintf(path, sizeof(path), "%s/file2.dat", TEST_ROOT);
    unlink(path);
    rmdir(TEST_ROOT);

    /* Create root test directory */
    if (mkdir(TEST_ROOT, 0755) != 0 && errno != EEXIST) {
        printf("  Failed to create test root: %s\n", strerror(errno));
        return 0;
    }
    printf("  Created: %s/\n", TEST_ROOT);

    /* Create subdirectories */
    snprintf(path, sizeof(path), "%s/subdir1", TEST_ROOT);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        printf("  Failed to create subdir1: %s\n", strerror(errno));
        return 0;
    }
    printf("  Created: %s/\n", path);

    snprintf(path, sizeof(path), "%s/subdir2", TEST_ROOT);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        printf("  Failed to create subdir2: %s\n", strerror(errno));
        return 0;
    }
    printf("  Created: %s/\n", path);

    snprintf(path, sizeof(path), "%s/subdir2/nested", TEST_ROOT);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        printf("  Failed to create nested: %s\n", strerror(errno));
        return 0;
    }
    printf("  Created: %s/\n", path);

    /* Create test files with known content */
    snprintf(path, sizeof(path), "%s/file1.txt", TEST_ROOT);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Failed to create file1.txt: %s\n", strerror(errno));
        return 0;
    }
    const char *content1 = "Hello from WASI capstone test!\nLine 2\nLine 3\n";
    write(fd, content1, strlen(content1));
    close(fd);
    printf("  Created: %s (%zu bytes)\n", path, strlen(content1));

    snprintf(path, sizeof(path), "%s/file2.dat", TEST_ROOT);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Failed to create file2.dat: %s\n", strerror(errno));
        return 0;
    }
    /* Write binary data using pseudo-random */
    unsigned char binary_data[64];
    for (int i = 0; i < 64; i++) {
        binary_data[i] = (unsigned char)(pseudo_random() & 0xff);
    }
    write(fd, binary_data, 64);
    close(fd);
    printf("  Created: %s (64 bytes binary)\n", path);

    snprintf(path, sizeof(path), "%s/subdir1/nested_file.txt", TEST_ROOT);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Failed to create nested_file.txt: %s\n", strerror(errno));
        return 0;
    }
    const char *content3 = "Nested file content\n";
    write(fd, content3, strlen(content3));
    close(fd);
    printf("  Created: %s (%zu bytes)\n", path, strlen(content3));

    printf("  Environment setup complete.\n");
    return 1;
}

/* ============================================================================
 * Filesystem Tests
 * ============================================================================ */

static int test_stat_directory(void) {
    struct stat st;
    char path[256];
    snprintf(path, sizeof(path), "%s", TEST_ROOT);

    int ret = stat(path, &st);
    TEST_ASSERT(ret == 0, "stat on test root failed");
    TEST_ASSERT(S_ISDIR(st.st_mode), "test root is not a directory");
    TEST_ASSERT(st.st_nlink >= 2, "directory link count too low");

    printf("    Directory inode: (virtualized)\n");
    printf("    Directory nlink: %lu\n", (unsigned long)st.st_nlink);
    TEST_PASS("stat_directory");
}

static int test_stat_file(void) {
    struct stat st;
    char path[256];
    snprintf(path, sizeof(path), "%s/file1.txt", TEST_ROOT);

    int ret = stat(path, &st);
    TEST_ASSERT(ret == 0, "stat on file1.txt failed");
    TEST_ASSERT(S_ISREG(st.st_mode), "file1.txt is not a regular file");
    TEST_ASSERT(st.st_size == 45, "file1.txt size mismatch");

    printf("    File size: %ld bytes\n", (long)st.st_size);
    TEST_PASS("stat_file");
}

static int test_read_directory(void) {
    char path[256];
    snprintf(path, sizeof(path), "%s", TEST_ROOT);

    DIR *dir = opendir(path);
    TEST_ASSERT(dir != NULL, "opendir failed");

    int file_count = 0;
    int dir_count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Use stat to determine type (portable across POSIX/WASI) */
        snprintf(path, sizeof(path), "%s/%s", TEST_ROOT, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                printf("    [DIR]  %s\n", entry->d_name);
                dir_count++;
            } else if (S_ISREG(st.st_mode)) {
                printf("    [FILE] %s\n", entry->d_name);
                file_count++;
            }
        }
    }

    closedir(dir);

    TEST_ASSERT(file_count == 2, "expected 2 files");
    TEST_ASSERT(dir_count == 2, "expected 2 directories");

    printf("    Total: %d files, %d directories\n", file_count, dir_count);
    TEST_PASS("read_directory");
}

static int test_file_read_write(void) {
    char path[256];
    snprintf(path, sizeof(path), "%s/readwrite_test.txt", TEST_ROOT);

    /* Write test data */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "open for write failed");

    const char *test_data = "WASI read/write test data 12345";
    ssize_t written = write(fd, test_data, strlen(test_data));
    TEST_ASSERT(written == (ssize_t)strlen(test_data), "write failed");
    close(fd);

    /* Read it back */
    fd = open(path, O_RDONLY);
    TEST_ASSERT(fd >= 0, "open for read failed");

    char buffer[128];
    ssize_t nread = read(fd, buffer, sizeof(buffer) - 1);
    TEST_ASSERT(nread == (ssize_t)strlen(test_data), "read size mismatch");
    buffer[nread] = '\0';
    close(fd);

    TEST_ASSERT(strcmp(buffer, test_data) == 0, "data content mismatch");

    /* Clean up */
    unlink(path);

    printf("    Write: %zd bytes, Read: %zd bytes\n", written, nread);
    TEST_PASS("file_read_write");
}

static int test_file_seek(void) {
    char path[256];
    snprintf(path, sizeof(path), "%s/file1.txt", TEST_ROOT);

    int fd = open(path, O_RDONLY);
    TEST_ASSERT(fd >= 0, "open failed");

    /* Seek to position 6 ("from WASI...") */
    off_t pos = lseek(fd, 6, SEEK_SET);
    TEST_ASSERT(pos == 6, "SEEK_SET failed");

    char buffer[32];
    ssize_t nread = read(fd, buffer, 4);
    TEST_ASSERT(nread == 4, "read after seek failed");
    buffer[4] = '\0';
    TEST_ASSERT(strcmp(buffer, "from") == 0, "seek position wrong");

    /* Seek to end */
    pos = lseek(fd, 0, SEEK_END);
    TEST_ASSERT(pos == 45, "SEEK_END failed");

    /* Seek backwards from end */
    pos = lseek(fd, -8, SEEK_END);
    TEST_ASSERT(pos == 37, "SEEK_END negative failed");

    close(fd);

    printf("    Seek operations verified\n");
    TEST_PASS("file_seek");
}

static int test_file_truncate(void) {
    char path[256];
    snprintf(path, sizeof(path), "%s/truncate_test.txt", TEST_ROOT);

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "open failed");

    const char *data = "0123456789ABCDEF";
    write(fd, data, strlen(data));

    /* Truncate to 8 bytes */
    int ret = ftruncate(fd, 8);
    TEST_ASSERT(ret == 0, "ftruncate failed");

    struct stat st;
    fstat(fd, &st);
    TEST_ASSERT(st.st_size == 8, "size after truncate wrong");

    /* Read and verify */
    lseek(fd, 0, SEEK_SET);
    char buffer[32];
    ssize_t nread = read(fd, buffer, sizeof(buffer));
    TEST_ASSERT(nread == 8, "read after truncate wrong");
    buffer[nread] = '\0';
    TEST_ASSERT(strcmp(buffer, "01234567") == 0, "truncated content wrong");

    close(fd);
    unlink(path);

    printf("    Truncate to 8 bytes verified\n");
    TEST_PASS("file_truncate");
}

/* ============================================================================
 * Clock Tests
 * ============================================================================ */

static int test_clock_gettime_realtime(void) {
    struct timespec ts;
    int ret = clock_gettime(CLOCK_REALTIME, &ts);
    TEST_ASSERT(ret == 0, "clock_gettime CLOCK_REALTIME failed");

    /* Verify time is reasonable (after year 2020) */
    TEST_ASSERT(ts.tv_sec > 1577836800, "wall clock time too old");
    TEST_ASSERT(ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000, "invalid nanoseconds");

    printf("    Wall clock: %ld.%09ld\n", (long)ts.tv_sec, ts.tv_nsec);
    TEST_PASS("clock_gettime_realtime");
}

static int test_clock_gettime_monotonic(void) {
    struct timespec ts1, ts2;

    int ret = clock_gettime(CLOCK_MONOTONIC, &ts1);
    TEST_ASSERT(ret == 0, "clock_gettime CLOCK_MONOTONIC failed");

    /* Busy loop to consume time */
    volatile int x = 0;
    for (int i = 0; i < 100000; i++) {
        x += i;
    }

    ret = clock_gettime(CLOCK_MONOTONIC, &ts2);
    TEST_ASSERT(ret == 0, "second clock_gettime failed");

    /* Time should have advanced */
    long long ns1 = (long long)ts1.tv_sec * 1000000000LL + ts1.tv_nsec;
    long long ns2 = (long long)ts2.tv_sec * 1000000000LL + ts2.tv_nsec;
    TEST_ASSERT(ns2 > ns1, "monotonic time did not advance");

    long long elapsed = ns2 - ns1;
    printf("    Elapsed: %lld ns\n", elapsed);
    TEST_PASS("clock_gettime_monotonic");
}

static int test_nanosleep(void) {
    struct timespec req = {0, 10000000}; /* 10ms */
    struct timespec rem;
    struct timespec before, after;

    clock_gettime(CLOCK_MONOTONIC, &before);
    int ret = nanosleep(&req, &rem);
    clock_gettime(CLOCK_MONOTONIC, &after);

    TEST_ASSERT(ret == 0, "nanosleep failed");

    long long ns_before = (long long)before.tv_sec * 1000000000LL + before.tv_nsec;
    long long ns_after = (long long)after.tv_sec * 1000000000LL + after.tv_nsec;
    long long slept_ns = ns_after - ns_before;

    /* Should have slept at least 9ms (allowing some tolerance) */
    TEST_ASSERT(slept_ns >= 9000000, "slept less than expected");
    /* Should not have slept more than 50ms */
    TEST_ASSERT(slept_ns < 50000000, "slept much longer than expected");

    printf("    Requested: 10ms, Actual: %.2fms\n", slept_ns / 1000000.0);
    TEST_PASS("nanosleep");
}

/* ============================================================================
 * Environment Tests
 * ============================================================================ */

extern char **environ;

static int test_environment_vars(void) {
    /* Check if TEST_MODE env var is set */
    char *test_mode = getenv("TEST_MODE");
    if (test_mode != NULL) {
        printf("    TEST_MODE=%s\n", test_mode);
    } else {
        printf("    TEST_MODE not set (expected in native mode)\n");
    }

    /* Count environment variables */
    int count = 0;
    for (char **env = environ; *env != NULL; env++) {
        count++;
    }

    printf("    Environment variable count: %d\n", count);
    TEST_PASS("environment_vars");
}

static int test_stdio_operations(void) {
    /* Test stdout */
    int n = fprintf(stdout, "    stdout test: OK\n");
    TEST_ASSERT(n > 0, "fprintf to stdout failed");

    /* Test stderr */
    n = fprintf(stderr, "    stderr test: OK\n");
    TEST_ASSERT(n > 0, "fprintf to stderr failed");

    /* Test fflush */
    int ret = fflush(stdout);
    TEST_ASSERT(ret == 0, "fflush stdout failed");

    ret = fflush(stderr);
    TEST_ASSERT(ret == 0, "fflush stderr failed");

    TEST_PASS("stdio_operations");
}

/* ============================================================================
 * Combined Workflow Test
 * ============================================================================ */

static int test_combined_workflow(void) {
    char path[256];

    printf("    Running combined WASI workflow...\n");

    /* 1. Create a timestamped log file */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    snprintf(path, sizeof(path), "%s/workflow_log.txt", TEST_ROOT);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "create log file failed");

    char log_entry[256];
    int len = snprintf(log_entry, sizeof(log_entry),
                       "Log entry at timestamp: %ld.%09ld\n",
                       (long)ts.tv_sec, ts.tv_nsec);
    if (len > 0) {
        write(fd, log_entry, (size_t)len);
    }

    /* 2. Add pseudo-random data entry */
    unsigned int rand_val = pseudo_random();
    len = snprintf(log_entry, sizeof(log_entry),
                   "Random value: %u\n", rand_val);
    if (len > 0) {
        write(fd, log_entry, (size_t)len);
    }

    /* 3. Add environment info */
    char *test_mode = getenv("TEST_MODE");
    len = snprintf(log_entry, sizeof(log_entry),
                   "Test mode: %s\n", test_mode ? test_mode : "native");
    if (len > 0) {
        write(fd, log_entry, (size_t)len);
    }

    close(fd);

    /* 4. Read back and verify */
    fd = open(path, O_RDONLY);
    TEST_ASSERT(fd >= 0, "reopen log file failed");

    struct stat st;
    fstat(fd, &st);
    TEST_ASSERT(st.st_size > 0, "log file is empty");

    close(fd);

    /* 5. List all files created */
    DIR *dir = opendir(TEST_ROOT);
    TEST_ASSERT(dir != NULL, "opendir test root failed");

    int file_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            file_count++;
        }
    }
    closedir(dir);

    printf("    Created workflow log, found %d items\n", file_count);

    /* Clean up workflow log */
    unlink(path);

    TEST_PASS("combined_workflow");
}

/* ============================================================================
 * Cleanup
 * ============================================================================ */

static void cleanup_test_environment(void) {
    printf("\n=== Cleaning Up Test Environment ===\n");

    char path[256];

    /* Remove files */
    snprintf(path, sizeof(path), "%s/file1.txt", TEST_ROOT);
    unlink(path);
    snprintf(path, sizeof(path), "%s/file2.dat", TEST_ROOT);
    unlink(path);
    snprintf(path, sizeof(path), "%s/subdir1/nested_file.txt", TEST_ROOT);
    unlink(path);

    /* Remove directories (must be empty, so remove in reverse order) */
    snprintf(path, sizeof(path), "%s/subdir2/nested", TEST_ROOT);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/subdir2", TEST_ROOT);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/subdir1", TEST_ROOT);
    rmdir(path);
    rmdir(TEST_ROOT);

    printf("  Cleanup complete.\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("========================================\n");
    printf("WASI Capstone Integration Test\n");
    printf("========================================\n");

#ifdef __wasi__
    printf("Platform: WASI (WebAssembly)\n");
#else
    printf("Platform: Native C\n");
#endif

    /* Setup */
    if (!setup_test_environment()) {
        printf("Failed to set up test environment!\n");
        return 1;
    }

    /* Filesystem Tests */
    printf("\n=== Filesystem Tests ===\n");
    test_stat_directory();
    test_stat_file();
    test_read_directory();
    test_file_read_write();
    test_file_seek();
    test_file_truncate();

    /* Clock Tests */
    printf("\n=== Clock Tests ===\n");
    test_clock_gettime_realtime();
    test_clock_gettime_monotonic();
    test_nanosleep();

    /* Environment Tests */
    printf("\n=== Environment Tests ===\n");
    test_environment_vars();
    test_stdio_operations();

    /* Combined Workflow */
    printf("\n=== Combined Workflow Test ===\n");
    test_combined_workflow();

    /* Cleanup */
    cleanup_test_environment();

    /* Summary */
    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
