/**
 * WASI Common Definitions
 *
 * Shared declarations used across all WASI implementation modules.
 * This header provides the Component Model ABI functions required by
 * wit-bindgen generated code.
 */

#ifndef WASI_COMMON_H
#define WASI_COMMON_H

#include <stddef.h>

/**
 * Component Model ABI realloc function.
 *
 * This function is required by wit-bindgen generated C bindings for
 * memory allocation. It follows the Component Model canonical ABI.
 *
 * @param ptr       Pointer to existing allocation (NULL for new allocation)
 * @param old_size  Size of existing allocation (ignored, for ABI compatibility)
 * @param align     Required alignment (ignored, standard malloc alignment used)
 * @param new_size  New size to allocate
 * @return          Pointer to allocated memory, or abort() on failure
 */
void *cabi_realloc(void *ptr, size_t old_size, size_t align, size_t new_size);

/* ============================================================================
 * wasi:io helper functions (defined in io.c, used by other modules)
 * ============================================================================ */

#include <stdint.h>
#include <stdbool.h>

/* Error creation */
int32_t wasi_io_error_create(const char *message);

/* Pollable creation */
int32_t wasi_io_poll_create_fd_pollable(int fd, bool for_write);
int32_t wasi_io_poll_create_instant_pollable(uint64_t when);
int32_t wasi_io_poll_create_duration_pollable(uint64_t duration_ns);
int32_t wasi_io_poll_create_ready_pollable(void);

/* Stream creation */
int32_t wasi_io_streams_create_input_stream(int fd, bool owns_fd);
int32_t wasi_io_streams_create_output_stream(int fd, bool owns_fd);

/* ============================================================================
 * wasi:cli helper functions (defined in cli.c)
 * ============================================================================ */

/* Initialize the CLI module with command-line arguments.
 * Call this from main() before using WASI functions. */
void wasi_cli_init(int argc, char **argv);

#endif /* WASI_COMMON_H */
