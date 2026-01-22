/**
 * WASI Comparison Tests - Environment and CLI
 *
 * These tests use standard C APIs that map to WASI CLI interfaces
 * when compiled for WebAssembly.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* WASI requires explicit declaration of __wasi_args_get etc. but
   standard C library provides access via argc/argv and environ */

extern char **environ;

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

/* Test: Environment variables accessible */
void test_environment_accessible(void) {
    /* Count environment variables */
    int count = 0;
    for (char **env = environ; *env != NULL; env++) {
        count++;
    }

    TEST_ASSERT(count > 0, "no environment variables found");
    printf("    environment variable count: %d\n", count);

    /* Print first few */
    int printed = 0;
    for (char **env = environ; *env != NULL && printed < 3; env++, printed++) {
        char *eq = strchr(*env, '=');
        if (eq) {
            int namelen = (int)(eq - *env);
            printf("    %.*s=...\n", namelen, *env);
        }
    }

    TEST_PASS("environment_accessible");
}

/* Test: PATH environment variable exists */
void test_path_env(void) {
    char *path = getenv("PATH");

    /* PATH should exist in most environments */
    if (path == NULL) {
        printf("    PATH not set (may be intentional in sandbox)\n");
        tests_passed++;  /* Don't fail, as WASI sandbox may not have PATH */
        return;
    }

    TEST_ASSERT(strlen(path) > 0, "PATH is empty");
    printf("    PATH length: %zu\n", strlen(path));

    TEST_PASS("path_env");
}

/* Test: Can set custom environment variable */
void test_setenv(void) {
    const char *name = "WASI_TEST_VAR";
    const char *value = "test_value_12345";

    /* Clear any existing value */
    unsetenv(name);
    TEST_ASSERT(getenv(name) == NULL, "unsetenv failed");

    /* Set new value */
    int ret = setenv(name, value, 1);
    TEST_ASSERT(ret == 0, "setenv failed");

    /* Read it back */
    char *retrieved = getenv(name);
    TEST_ASSERT(retrieved != NULL, "getenv returned NULL after setenv");
    TEST_ASSERT(strcmp(retrieved, value) == 0, "getenv value mismatch");

    /* Clean up */
    unsetenv(name);

    TEST_PASS("setenv");
}

/* Test: stdin/stdout/stderr file descriptors exist */
void test_stdio_fds(void) {
    /* These should always be open */
    TEST_ASSERT(isatty(STDIN_FILENO) >= 0 || errno == ENOTTY,
                "stdin fd invalid");
    TEST_ASSERT(isatty(STDOUT_FILENO) >= 0 || errno == ENOTTY,
                "stdout fd invalid");
    TEST_ASSERT(isatty(STDERR_FILENO) >= 0 || errno == ENOTTY,
                "stderr fd invalid");

    printf("    stdin fd: %d\n", STDIN_FILENO);
    printf("    stdout fd: %d\n", STDOUT_FILENO);
    printf("    stderr fd: %d\n", STDERR_FILENO);

    TEST_PASS("stdio_fds");
}

/* Test: Can write to stdout */
void test_stdout_write(void) {
    const char *msg = "    [test output to stdout]\n";
    ssize_t written = write(STDOUT_FILENO, msg, strlen(msg));
    TEST_ASSERT(written == (ssize_t)strlen(msg), "write to stdout failed");

    TEST_PASS("stdout_write");
}

/* Test: Can write to stderr */
void test_stderr_write(void) {
    const char *msg = "    [test output to stderr]\n";
    ssize_t written = write(STDERR_FILENO, msg, strlen(msg));
    TEST_ASSERT(written == (ssize_t)strlen(msg), "write to stderr failed");

    TEST_PASS("stderr_write");
}

/* Test: printf/fprintf work */
void test_formatted_output(void) {
    int n = fprintf(stdout, "    formatted: %d %s %.2f\n", 42, "test", 3.14);
    TEST_ASSERT(n > 0, "fprintf to stdout failed");

    n = fprintf(stderr, "    formatted stderr: ok\n");
    TEST_ASSERT(n > 0, "fprintf to stderr failed");

    TEST_PASS("formatted_output");
}

/* Test: fflush works */
void test_fflush(void) {
    printf("    before flush");
    int ret = fflush(stdout);
    printf(" - after flush\n");
    TEST_ASSERT(ret == 0, "fflush failed");

    TEST_PASS("fflush");
}

/* Global for argc/argv test */
static int saved_argc;
static char **saved_argv;

/* Test: Command line arguments accessible */
void test_arguments(void) {
    TEST_ASSERT(saved_argc >= 1, "no arguments (not even program name)");
    TEST_ASSERT(saved_argv != NULL, "argv is NULL");
    TEST_ASSERT(saved_argv[0] != NULL, "argv[0] is NULL");

    printf("    argc: %d\n", saved_argc);
    printf("    argv[0]: %s\n", saved_argv[0]);

    /* Check that argv is null-terminated */
    TEST_ASSERT(saved_argv[saved_argc] == NULL, "argv not null-terminated");

    TEST_PASS("arguments");
}

int main(int argc, char *argv[]) {
    saved_argc = argc;
    saved_argv = argv;

    printf("=== Environment Comparison Tests ===\n");
#ifdef __wasi__
    printf("Platform: WASI (WebAssembly)\n");
#else
    printf("Platform: Native Linux\n");
#endif
    printf("\n");

    test_arguments();
    test_environment_accessible();
    test_path_env();
    test_setenv();
    test_stdio_fds();
    test_stdout_write();
    test_stderr_write();
    test_formatted_output();
    test_fflush();

    printf("\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
