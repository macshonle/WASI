/**
 * Tests for WASI CLI Implementation
 *
 * Run: make test-cli
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>

/* Test framework macros */
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    test_##name(); \
    printf("  %s: PASS\n", #name); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* Include the generated bindings header */
#include "../build/c-bindings/cli/cli_imports.h"

/* External initialization function */
extern void wasi_cli_init(int argc, char **argv);

/* ============================================================================
 * Test: Get environment variables
 * ============================================================================
 */
TEST(get_environment) {
    cli_imports_list_tuple2_string_string_t env;
    wasi_cli_environment_get_environment(&env);

    /* Should have at least some environment variables */
    assert(env.len > 0);

    /* Each entry should have non-empty name */
    for (size_t i = 0; i < env.len && i < 5; i++) {
        assert(env.ptr[i].f0.len > 0);
    }

    cli_imports_list_tuple2_string_string_free(&env);
}

/* ============================================================================
 * Test: Get arguments
 * ============================================================================
 */
TEST(get_arguments) {
    cli_imports_list_string_t args;
    wasi_cli_environment_get_arguments(&args);

    /* Should have at least the program name */
    assert(args.len >= 1);
    assert(args.ptr[0].len > 0);

    cli_imports_list_string_free(&args);
}

/* ============================================================================
 * Test: Get initial cwd
 * ============================================================================
 */
TEST(get_initial_cwd) {
    cli_imports_string_t cwd;
    bool ok = wasi_cli_environment_initial_cwd(&cwd);

    /* Should succeed and return a non-empty path */
    assert(ok);
    assert(cwd.len > 0);
    /* Path should start with / on Unix */
    assert(cwd.ptr[0] == '/');

    cli_imports_string_free(&cwd);
}

/* ============================================================================
 * Test: Get stdin stream
 * ============================================================================
 */
TEST(get_stdin) {
    wasi_cli_stdin_own_input_stream_t stdin_stream = wasi_cli_stdin_get_stdin();

    /* Should return a valid handle */
    assert(stdin_stream.__handle > 0);

    /* Clean up */
    wasi_io_streams_input_stream_drop_own(
        (wasi_io_streams_own_input_stream_t){ stdin_stream.__handle });
}

/* ============================================================================
 * Test: Get stdout stream
 * ============================================================================
 */
TEST(get_stdout) {
    wasi_cli_stdout_own_output_stream_t stdout_stream = wasi_cli_stdout_get_stdout();

    /* Should return a valid handle */
    assert(stdout_stream.__handle > 0);

    /* Clean up */
    wasi_io_streams_output_stream_drop_own(
        (wasi_io_streams_own_output_stream_t){ stdout_stream.__handle });
}

/* ============================================================================
 * Test: Get stderr stream
 * ============================================================================
 */
TEST(get_stderr) {
    wasi_cli_stderr_own_output_stream_t stderr_stream = wasi_cli_stderr_get_stderr();

    /* Should return a valid handle */
    assert(stderr_stream.__handle > 0);

    /* Clean up */
    wasi_io_streams_output_stream_drop_own(
        (wasi_io_streams_own_output_stream_t){ stderr_stream.__handle });
}

/* ============================================================================
 * Test: Terminal detection for stdout
 * ============================================================================
 */
TEST(terminal_detection) {
    wasi_cli_terminal_stdout_own_terminal_output_t term;
    bool is_tty = wasi_cli_terminal_stdout_get_terminal_stdout(&term);

    /* Result should match isatty */
    bool expected = isatty(STDOUT_FILENO) != 0;
    assert(is_tty == expected);

    if (is_tty) {
        wasi_cli_terminal_output_terminal_output_drop_own(
            (wasi_cli_terminal_output_own_terminal_output_t){ term.__handle });
    }
}

/* ============================================================================
 * Test: Write to stdout stream
 * ============================================================================
 */
TEST(write_to_stdout) {
    wasi_cli_stdout_own_output_stream_t stdout_stream = wasi_cli_stdout_get_stdout();
    assert(stdout_stream.__handle > 0);

    wasi_io_streams_borrow_output_stream_t borrow = { stdout_stream.__handle };

    /* Check write budget */
    uint64_t budget;
    wasi_io_streams_stream_error_t err;
    bool ok = wasi_io_streams_method_output_stream_check_write(borrow, &budget, &err);
    assert(ok);
    assert(budget > 0);

    /* Write a test message (it will appear in test output) */
    uint8_t msg[] = "(test output)\n";
    cli_imports_list_u8_t contents = { msg, sizeof(msg) - 1 };
    ok = wasi_io_streams_method_output_stream_write(borrow, &contents, &err);
    assert(ok);

    wasi_io_streams_output_stream_drop_own(
        (wasi_io_streams_own_output_stream_t){ stdout_stream.__handle });
}

/* ============================================================================
 * Main test runner
 * ============================================================================
 */
int run_cli_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("Running WASI CLI tests...\n");

    RUN_TEST(get_environment);
    RUN_TEST(get_arguments);
    RUN_TEST(get_initial_cwd);
    RUN_TEST(get_stdin);
    RUN_TEST(get_stdout);
    RUN_TEST(get_stderr);
    RUN_TEST(terminal_detection);
    RUN_TEST(write_to_stdout);

    return tests_failed;
}

/* Allow standalone execution */
#ifndef TEST_RUNNER_MODE
int main(int argc, char *argv[]) {
    /* Initialize CLI module with args */
    wasi_cli_init(argc, argv);

    return run_cli_tests();
}
#endif
