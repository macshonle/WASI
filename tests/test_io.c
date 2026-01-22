/**
 * Tests for WASI I/O Implementation
 *
 * Run: make test-io
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>

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
#include "../build/c-bindings/io/imports.h"

/* External function declarations for io.c internal helpers */
extern int32_t wasi_io_error_create(const char *message);
extern int32_t wasi_io_poll_create_fd_pollable(int fd, bool for_write);
extern int32_t wasi_io_poll_create_ready_pollable(void);
extern int32_t wasi_io_poll_create_duration_pollable(uint64_t duration_ns);
extern int32_t wasi_io_streams_create_input_stream(int fd, bool owns_fd);
extern int32_t wasi_io_streams_create_output_stream(int fd, bool owns_fd);

/* ============================================================================
 * Test: Error creation and debug string
 * ============================================================================
 */
TEST(error_create) {
    int32_t handle = wasi_io_error_create("test error message");
    assert(handle > 0);

    wasi_io_error_borrow_error_t borrow = { handle };
    imports_string_t str;
    wasi_io_error_method_error_to_debug_string(borrow, &str);

    assert(str.len == 18);
    assert(memcmp(str.ptr, "test error message", 18) == 0);

    imports_string_free(&str);
    wasi_io_error_error_drop_own((wasi_io_error_own_error_t){ handle });
}

/* ============================================================================
 * Test: Pollable ready check (always-ready pollable)
 * ============================================================================
 */
TEST(pollable_always_ready) {
    int32_t handle = wasi_io_poll_create_ready_pollable();
    assert(handle > 0);

    wasi_io_poll_borrow_pollable_t borrow = { handle };
    bool ready = wasi_io_poll_method_pollable_ready(borrow);
    assert(ready == true);

    wasi_io_poll_pollable_drop_own((wasi_io_poll_own_pollable_t){ handle });
}

/* ============================================================================
 * Test: Pollable for stdout (should be ready for write)
 * ============================================================================
 */
TEST(pollable_stdout_ready) {
    int32_t handle = wasi_io_poll_create_fd_pollable(STDOUT_FILENO, true);
    assert(handle > 0);

    wasi_io_poll_borrow_pollable_t borrow = { handle };
    bool ready = wasi_io_poll_method_pollable_ready(borrow);
    assert(ready == true);  /* stdout should be ready for writing */

    wasi_io_poll_pollable_drop_own((wasi_io_poll_own_pollable_t){ handle });
}

/* ============================================================================
 * Test: Poll function with ready pollable
 * ============================================================================
 */
TEST(poll_with_ready) {
    int32_t h1 = wasi_io_poll_create_ready_pollable();
    assert(h1 > 0);

    wasi_io_poll_borrow_pollable_t borrows[1];
    borrows[0].__handle = h1;

    wasi_io_poll_list_borrow_pollable_t in;
    in.ptr = borrows;
    in.len = 1;

    imports_list_u32_t ret;
    wasi_io_poll_poll(&in, &ret);

    assert(ret.len >= 1);
    assert(ret.ptr[0] == 0);  /* First pollable should be ready */

    imports_list_u32_free(&ret);
    wasi_io_poll_pollable_drop_own((wasi_io_poll_own_pollable_t){ h1 });
}

/* ============================================================================
 * Test: Output stream write to /dev/null
 * ============================================================================
 */
TEST(output_stream_write) {
    int fd = open("/dev/null", O_WRONLY);
    assert(fd >= 0);

    int32_t handle = wasi_io_streams_create_output_stream(fd, true);
    assert(handle > 0);

    wasi_io_streams_borrow_output_stream_t borrow = { handle };

    /* Check write budget */
    uint64_t budget;
    wasi_io_streams_stream_error_t err;
    bool ok = wasi_io_streams_method_output_stream_check_write(borrow, &budget, &err);
    assert(ok);
    assert(budget > 0);

    /* Write some data */
    uint8_t data[] = "Hello, WASI!";
    imports_list_u8_t contents = { data, sizeof(data) - 1 };
    ok = wasi_io_streams_method_output_stream_write(borrow, &contents, &err);
    assert(ok);

    wasi_io_streams_output_stream_drop_own((wasi_io_streams_own_output_stream_t){ handle });
}

/* ============================================================================
 * Test: Input stream read from pipe
 * ============================================================================
 */
TEST(input_stream_read) {
    int pipefd[2];
    int ret = pipe(pipefd);
    assert(ret == 0);

    /* Write to pipe */
    const char *msg = "test data";
    write(pipefd[1], msg, strlen(msg));
    close(pipefd[1]);

    /* Create input stream from read end */
    int32_t handle = wasi_io_streams_create_input_stream(pipefd[0], true);
    assert(handle > 0);

    wasi_io_streams_borrow_input_stream_t borrow = { handle };

    /* Read data */
    imports_list_u8_t data;
    wasi_io_streams_stream_error_t err;
    bool ok = wasi_io_streams_method_input_stream_blocking_read(borrow, 100, &data, &err);
    assert(ok);
    assert(data.len == strlen(msg));
    assert(memcmp(data.ptr, msg, data.len) == 0);

    imports_list_u8_free(&data);
    wasi_io_streams_input_stream_drop_own((wasi_io_streams_own_input_stream_t){ handle });
}

/* ============================================================================
 * Test: Stream subscription
 * ============================================================================
 */
TEST(stream_subscribe) {
    int fd = open("/dev/null", O_WRONLY);
    assert(fd >= 0);

    int32_t stream_handle = wasi_io_streams_create_output_stream(fd, true);
    assert(stream_handle > 0);

    wasi_io_streams_borrow_output_stream_t borrow = { stream_handle };
    wasi_io_streams_own_pollable_t pollable = wasi_io_streams_method_output_stream_subscribe(borrow);
    assert(pollable.__handle > 0);

    /* Pollable for /dev/null should be ready */
    wasi_io_poll_borrow_pollable_t poll_borrow = wasi_io_poll_borrow_pollable(pollable);
    bool ready = wasi_io_poll_method_pollable_ready(poll_borrow);
    assert(ready == true);

    wasi_io_poll_pollable_drop_own(pollable);
    wasi_io_streams_output_stream_drop_own((wasi_io_streams_own_output_stream_t){ stream_handle });
}

/* ============================================================================
 * Main test runner
 * ============================================================================
 */
int run_io_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("Running WASI I/O tests...\n");

    RUN_TEST(error_create);
    RUN_TEST(pollable_always_ready);
    RUN_TEST(pollable_stdout_ready);
    RUN_TEST(poll_with_ready);
    RUN_TEST(output_stream_write);
    RUN_TEST(input_stream_read);
    RUN_TEST(stream_subscribe);

    printf("\nI/O tests passed: %d\n", tests_passed);
    printf("I/O tests failed: %d\n", tests_failed);

    return tests_failed;
}

/* Allow standalone execution */
#ifndef TEST_RUNNER_MODE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_io_tests();
}
#endif
