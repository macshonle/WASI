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
#include <stdint.h>
#include <stdbool.h>

/* C89-compatible compile-time assertion. */
#define WASI_CONCAT_INNER(a, b) a##b
#define WASI_CONCAT(a, b) WASI_CONCAT_INNER(a, b)
#define WASI_STATIC_ASSERT(cond) \
    typedef char WASI_CONCAT(wasi_static_assert_, __LINE__)[(cond) ? 1 : -1]

/* Alignment helper (use compiler builtin when available). */
#if defined(__clang__) || defined(__GNUC__)
#define WASI_ALIGNOF(type) __alignof__(type)
#else
#define WASI_ALIGNOF(type) offsetof(struct { char c; type member; }, member)
#endif

/**
 * Component Model ABI realloc function.
 *
 * This function is required by wit-bindgen generated C bindings for
 * memory allocation. It follows the Component Model canonical ABI.
 *
 * @param ptr       Pointer to existing allocation (NULL for new allocation)
 * @param old_size  Size of existing allocation (used for aligned reallocation)
 * @param align     Required alignment (enforced)
 * @param new_size  New size to allocate
 * @return          Pointer to allocated memory, or abort() on failure
 */
void *cabi_realloc(void *ptr, size_t old_size, size_t align, size_t new_size);

/* Canonical ABI helpers */
void *wasi_cabi_alloc(size_t align, size_t size);
void wasi_cabi_free(void *ptr, size_t align);
bool wasi_cabi_alloc_list(size_t len, size_t elem_size, size_t align, void **out_ptr);
bool wasi_cabi_alloc_string(size_t len, uint8_t **out_ptr);
bool wasi_utf8_validate(const uint8_t *data, size_t len);
void wasi_utf8_validate_or_abort(const uint8_t *data, size_t len);

/* ABI conformance checks for {ptr,len} structs */
#define WASI_ABI_CHECK_PTR_LEN_TYPE(type) \
    WASI_STATIC_ASSERT(offsetof(type, ptr) == 0); \
    WASI_STATIC_ASSERT(offsetof(type, len) == sizeof(void *)); \
    WASI_STATIC_ASSERT(sizeof(type) == sizeof(void *) + sizeof(size_t))

/* ============================================================================
 * wasi:io helper functions (defined in io.c, used by other modules)
 * ============================================================================ */

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
