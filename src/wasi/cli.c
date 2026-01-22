/**
 * WASI CLI Implementation
 *
 * This file implements the wasi:cli interfaces for UNIX/Linux/macOS.
 *
 * Interfaces implemented:
 *   - wasi:cli/environment@0.2.0     - Environment variables and arguments
 *   - wasi:cli/exit@0.2.0            - Process exit
 *   - wasi:cli/stdin@0.2.0           - Standard input
 *   - wasi:cli/stdout@0.2.0          - Standard output
 *   - wasi:cli/stderr@0.2.0          - Standard error
 *   - wasi:cli/terminal-*@0.2.0      - Terminal detection
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "platform/platform.h"

/* Include the generated bindings header */
#include "../../build/c-bindings/cli/cli_imports.h"

/* External declarations from io.c for stream creation */
extern int32_t wasi_io_streams_create_input_stream(int fd, bool owns_fd);
extern int32_t wasi_io_streams_create_output_stream(int fd, bool owns_fd);

/* External environment from libc */
extern char **environ;

/* Saved command-line arguments (set by main) */
static int saved_argc = 0;
static char **saved_argv = NULL;

/* ============================================================================
 * Helper: cabi_realloc (required by generated bindings)
 * ============================================================================
 */
__attribute__((__weak__))
void *cabi_realloc(void *ptr, size_t old_size, size_t align, size_t new_size) {
    (void)old_size;
    (void)align;
    if (new_size == 0) return (void*)align;
    void *ret = realloc(ptr, new_size);
    if (!ret) abort();
    return ret;
}

/* ============================================================================
 * Initialization function (call from main)
 * ============================================================================
 */

/**
 * Initialize the CLI module with command-line arguments.
 * Call this from main() before using WASI functions.
 */
void wasi_cli_init(int argc, char **argv) {
    saved_argc = argc;
    saved_argv = argv;
}

/* ============================================================================
 * String helpers
 * ============================================================================
 */

static void cli_string_dup(cli_imports_string_t *ret, const char *s) {
    ret->len = strlen(s);
    ret->ptr = (uint8_t *)malloc(ret->len);
    if (ret->ptr) {
        memcpy(ret->ptr, s, ret->len);
    }
}

void cli_imports_string_free(cli_imports_string_t *ret) {
    if (ret->len > 0 && ret->ptr) {
        free(ret->ptr);
    }
    ret->ptr = NULL;
    ret->len = 0;
}

void cli_imports_list_string_free(cli_imports_list_string_t *ptr) {
    if (ptr->len > 0 && ptr->ptr) {
        for (size_t i = 0; i < ptr->len; i++) {
            cli_imports_string_free(&ptr->ptr[i]);
        }
        free(ptr->ptr);
    }
    ptr->ptr = NULL;
    ptr->len = 0;
}

void cli_imports_list_tuple2_string_string_free(cli_imports_list_tuple2_string_string_t *ptr) {
    if (ptr->len > 0 && ptr->ptr) {
        for (size_t i = 0; i < ptr->len; i++) {
            cli_imports_string_free(&ptr->ptr[i].f0);
            cli_imports_string_free(&ptr->ptr[i].f1);
        }
        free(ptr->ptr);
    }
    ptr->ptr = NULL;
    ptr->len = 0;
}

/* ============================================================================
 * wasi:cli/environment Implementation
 * ============================================================================
 */

/**
 * Get the environment variables.
 * Returns a list of (name, value) pairs.
 */
void wasi_cli_environment_get_environment(cli_imports_list_tuple2_string_string_t *ret) {
    /* Count environment variables */
    size_t count = 0;
    for (char **env = environ; *env != NULL; env++) {
        count++;
    }

    if (count == 0) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    ret->ptr = (cli_imports_tuple2_string_string_t *)malloc(
        count * sizeof(cli_imports_tuple2_string_string_t));
    if (!ret->ptr) {
        ret->len = 0;
        return;
    }

    size_t idx = 0;
    for (char **env = environ; *env != NULL && idx < count; env++) {
        char *eq = strchr(*env, '=');
        if (eq) {
            /* Name is everything before '=' */
            size_t name_len = (size_t)(eq - *env);
            ret->ptr[idx].f0.ptr = (uint8_t *)malloc(name_len);
            if (ret->ptr[idx].f0.ptr) {
                memcpy(ret->ptr[idx].f0.ptr, *env, name_len);
                ret->ptr[idx].f0.len = name_len;
            } else {
                ret->ptr[idx].f0.len = 0;
            }

            /* Value is everything after '=' */
            char *value = eq + 1;
            size_t value_len = strlen(value);
            ret->ptr[idx].f1.ptr = (uint8_t *)malloc(value_len);
            if (ret->ptr[idx].f1.ptr) {
                memcpy(ret->ptr[idx].f1.ptr, value, value_len);
                ret->ptr[idx].f1.len = value_len;
            } else {
                ret->ptr[idx].f1.len = 0;
            }

            idx++;
        }
    }

    ret->len = idx;
}

/**
 * Get the command-line arguments.
 */
void wasi_cli_environment_get_arguments(cli_imports_list_string_t *ret) {
    if (saved_argc <= 0 || saved_argv == NULL) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    ret->ptr = (cli_imports_string_t *)malloc(
        (size_t)saved_argc * sizeof(cli_imports_string_t));
    if (!ret->ptr) {
        ret->len = 0;
        return;
    }

    for (int i = 0; i < saved_argc; i++) {
        cli_string_dup(&ret->ptr[i], saved_argv[i]);
    }

    ret->len = (size_t)saved_argc;
}

/**
 * Get the initial current working directory.
 * Returns None if not available.
 */
bool wasi_cli_environment_initial_cwd(cli_imports_string_t *ret) {
    char *cwd = getcwd(NULL, 0);
    if (cwd) {
        cli_string_dup(ret, cwd);
        free(cwd);
        return true;
    }
    return false;
}

/* ============================================================================
 * wasi:cli/exit Implementation
 * ============================================================================
 */

/**
 * Exit the process with the given status.
 */
void wasi_cli_exit_exit(wasi_cli_exit_result_void_void_t *status) {
    int code = status->is_err ? 1 : 0;
    _exit(code);
}

/* ============================================================================
 * wasi:cli/stdin Implementation
 * ============================================================================
 */

/**
 * Get the standard input stream.
 */
wasi_cli_stdin_own_input_stream_t wasi_cli_stdin_get_stdin(void) {
    /* Create input stream for stdin (fd 0), don't take ownership */
    int32_t handle = wasi_io_streams_create_input_stream(STDIN_FILENO, false);
    return (wasi_cli_stdin_own_input_stream_t){ handle };
}

/* ============================================================================
 * wasi:cli/stdout Implementation
 * ============================================================================
 */

/**
 * Get the standard output stream.
 */
wasi_cli_stdout_own_output_stream_t wasi_cli_stdout_get_stdout(void) {
    /* Create output stream for stdout (fd 1), don't take ownership */
    int32_t handle = wasi_io_streams_create_output_stream(STDOUT_FILENO, false);
    return (wasi_cli_stdout_own_output_stream_t){ handle };
}

/* ============================================================================
 * wasi:cli/stderr Implementation
 * ============================================================================
 */

/**
 * Get the standard error stream.
 */
wasi_cli_stderr_own_output_stream_t wasi_cli_stderr_get_stderr(void) {
    /* Create output stream for stderr (fd 2), don't take ownership */
    int32_t handle = wasi_io_streams_create_output_stream(STDERR_FILENO, false);
    return (wasi_cli_stderr_own_output_stream_t){ handle };
}

/* ============================================================================
 * wasi:cli/terminal-* Implementation
 * ============================================================================
 */

/* Simple handle table for terminal resources */
#define MAX_TERMINAL_HANDLES 16
static int terminal_handle_table[MAX_TERMINAL_HANDLES];
static int next_terminal_handle = 1;

static int32_t alloc_terminal_handle(int fd) {
    if (next_terminal_handle >= MAX_TERMINAL_HANDLES) return -1;
    int idx = next_terminal_handle++;
    terminal_handle_table[idx] = fd;
    return idx;
}

/**
 * Get the terminal for stdin, if it's a terminal.
 */
bool wasi_cli_terminal_stdin_get_terminal_stdin(
    wasi_cli_terminal_stdin_own_terminal_input_t *ret
) {
    if (isatty(STDIN_FILENO)) {
        ret->__handle = alloc_terminal_handle(STDIN_FILENO);
        return true;
    }
    return false;
}

/**
 * Get the terminal for stdout, if it's a terminal.
 */
bool wasi_cli_terminal_stdout_get_terminal_stdout(
    wasi_cli_terminal_stdout_own_terminal_output_t *ret
) {
    if (isatty(STDOUT_FILENO)) {
        ret->__handle = alloc_terminal_handle(STDOUT_FILENO);
        return true;
    }
    return false;
}

/**
 * Get the terminal for stderr, if it's a terminal.
 */
bool wasi_cli_terminal_stderr_get_terminal_stderr(
    wasi_cli_terminal_stderr_own_terminal_output_t *ret
) {
    if (isatty(STDERR_FILENO)) {
        ret->__handle = alloc_terminal_handle(STDERR_FILENO);
        return true;
    }
    return false;
}

/**
 * Drop terminal input handle.
 */
void wasi_cli_terminal_input_terminal_input_drop_own(
    wasi_cli_terminal_input_own_terminal_input_t handle
) {
    if (handle.__handle > 0 && handle.__handle < MAX_TERMINAL_HANDLES) {
        terminal_handle_table[handle.__handle] = -1;
    }
}

void wasi_cli_terminal_input_terminal_input_drop_borrow(
    wasi_cli_terminal_input_borrow_terminal_input_t handle
) {
    (void)handle;
}

wasi_cli_terminal_input_borrow_terminal_input_t wasi_cli_terminal_input_borrow_terminal_input(
    wasi_cli_terminal_input_own_terminal_input_t handle
) {
    return (wasi_cli_terminal_input_borrow_terminal_input_t){ handle.__handle };
}

/**
 * Drop terminal output handle.
 */
void wasi_cli_terminal_output_terminal_output_drop_own(
    wasi_cli_terminal_output_own_terminal_output_t handle
) {
    if (handle.__handle > 0 && handle.__handle < MAX_TERMINAL_HANDLES) {
        terminal_handle_table[handle.__handle] = -1;
    }
}

void wasi_cli_terminal_output_terminal_output_drop_borrow(
    wasi_cli_terminal_output_borrow_terminal_output_t handle
) {
    (void)handle;
}

wasi_cli_terminal_output_borrow_terminal_output_t wasi_cli_terminal_output_borrow_terminal_output(
    wasi_cli_terminal_output_own_terminal_output_t handle
) {
    return (wasi_cli_terminal_output_borrow_terminal_output_t){ handle.__handle };
}
