/**
 * WASI I/O Implementation
 *
 * This file implements the wasi:io interfaces for macOS (UNIX) and GNU/Linux.
 *
 * Interfaces implemented:
 *   - wasi:io/error@0.2.0      - Error handling
 *   - wasi:io/poll@0.2.0       - Async polling
 *   - wasi:io/streams@0.2.0    - Input/output streams
 */

#ifdef __APPLE__
    #define _DARWIN_C_SOURCE  /* Enable BSD extensions on macOS (fsync, etc.) */
#endif
#define _POSIX_C_SOURCE 199309L

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>

#include "platform/platform.h"
#include "common.h"

/* Include the generated bindings header */
#include "../../build/c-bindings/io/imports.h"

/* ============================================================================
 * Handle Management
 * ============================================================================
 * WASI uses opaque handles to represent resources. We need to map these
 * handles to actual OS resources (file descriptors, allocated memory, etc.)
 */

#define WASI_MAX_HANDLES 4096

typedef enum {
    HANDLE_TYPE_NONE = 0,
    HANDLE_TYPE_ERROR,
    HANDLE_TYPE_POLLABLE,
    HANDLE_TYPE_INPUT_STREAM,
    HANDLE_TYPE_OUTPUT_STREAM,
} wasi_handle_type_t;

/* Pollable types */
typedef enum {
    POLLABLE_TYPE_FD_READ,      /* File descriptor read readiness */
    POLLABLE_TYPE_FD_WRITE,     /* File descriptor write readiness */
    POLLABLE_TYPE_INSTANT,      /* Timer - absolute time */
    POLLABLE_TYPE_DURATION,     /* Timer - relative time */
    POLLABLE_TYPE_ALWAYS_READY, /* Always ready pollable */
} wasi_pollable_type_t;

/* Error resource */
typedef struct {
    char *message;
    size_t message_len;
} wasi_error_resource_t;

/* Pollable resource */
typedef struct {
    wasi_pollable_type_t type;
    int fd;                    /* File descriptor (for FD-based pollables) */
    uint64_t when;             /* Target time (for timer pollables) */
    bool ready;                /* Cached ready state */
} wasi_pollable_resource_t;

/* Stream resource */
typedef struct {
    int fd;                    /* Underlying file descriptor */
    bool closed;               /* Whether stream is closed */
    bool owns_fd;              /* Whether we should close fd on drop */
    uint64_t write_budget;     /* Bytes permitted for next write (output streams) */
    bool flush_pending;        /* Flush operation in progress */
} wasi_stream_resource_t;

typedef struct {
    wasi_handle_type_t type;
    union {
        wasi_error_resource_t *error;
        wasi_pollable_resource_t *pollable;
        wasi_stream_resource_t *stream;
        void *generic;
    } resource;
} wasi_handle_entry_t;

static wasi_handle_entry_t handle_table[WASI_MAX_HANDLES];
static int32_t next_handle = 1;

/**
 * Allocate a new handle for a resource
 */
static int32_t wasi_handle_alloc(wasi_handle_type_t type, void *resource) {
    /* Find a free slot */
    for (int32_t i = 1; i < WASI_MAX_HANDLES; i++) {
        int32_t idx = (next_handle + i - 1) % WASI_MAX_HANDLES;
        if (idx == 0) idx = 1;  /* Skip handle 0 */
        if (handle_table[idx].type == HANDLE_TYPE_NONE) {
            handle_table[idx].type = type;
            handle_table[idx].resource.generic = resource;
            next_handle = idx + 1;
            return idx;
        }
    }
    return -1;  /* Out of handles */
}

/**
 * Get resource for a handle (with type checking)
 */
static void *wasi_handle_get(int32_t handle, wasi_handle_type_t expected_type) {
    if (handle <= 0 || handle >= WASI_MAX_HANDLES) {
        return NULL;
    }
    if (handle_table[handle].type != expected_type) {
        return NULL;
    }
    return handle_table[handle].resource.generic;
}

/**
 * Free a handle (does not free the underlying resource)
 */
static void wasi_handle_free(int32_t handle) {
    if (handle > 0 && handle < WASI_MAX_HANDLES) {
        handle_table[handle].type = HANDLE_TYPE_NONE;
        handle_table[handle].resource.generic = NULL;
    }
}

/* ============================================================================
 * String helpers
 * ============================================================================
 */

void imports_string_set(imports_string_t *ret, const char *s) {
    /* Cast through uintptr_t to acknowledge intentional const-to-non-const.
     * Caller is responsible for ensuring string is not modified through ret. */
    ret->ptr = (uint8_t *)(uintptr_t)s;
    ret->len = strlen(s);
}

void imports_string_dup(imports_string_t *ret, const char *s) {
    ret->len = strlen(s);
    ret->ptr = (uint8_t *)malloc(ret->len);
    if (ret->ptr) {
        memcpy(ret->ptr, s, ret->len);
    }
}

void imports_string_dup_n(imports_string_t *ret, const char *s, size_t len) {
    ret->len = len;
    ret->ptr = (uint8_t *)malloc(len);
    if (ret->ptr) {
        memcpy(ret->ptr, s, len);
    }
}

__attribute__((__weak__))
void imports_string_free(imports_string_t *ret) {
    if (ret->len > 0 && ret->ptr) {
        free(ret->ptr);
    }
    ret->ptr = NULL;
    ret->len = 0;
}

/* ============================================================================
 * List helpers
 * ============================================================================
 */

__attribute__((__weak__))
void imports_list_u8_free(imports_list_u8_t *ptr) {
    if (ptr->len > 0 && ptr->ptr != NULL) {
        free(ptr->ptr);
    }
    ptr->ptr = NULL;
    ptr->len = 0;
}

void imports_list_u32_free(imports_list_u32_t *ptr) {
    if (ptr->len > 0 && ptr->ptr != NULL) {
        free(ptr->ptr);
    }
    ptr->ptr = NULL;
    ptr->len = 0;
}

void wasi_io_poll_list_borrow_pollable_free(wasi_io_poll_list_borrow_pollable_t *ptr) {
    if (ptr->len > 0 && ptr->ptr != NULL) {
        free(ptr->ptr);
    }
    ptr->ptr = NULL;
    ptr->len = 0;
}

void wasi_io_streams_stream_error_free(wasi_io_streams_stream_error_t *ptr) {
    if (ptr->tag == WASI_IO_STREAMS_STREAM_ERROR_LAST_OPERATION_FAILED) {
        wasi_io_error_error_drop_own(ptr->val.last_operation_failed);
    }
}

void wasi_io_streams_result_list_u8_stream_error_free(wasi_io_streams_result_list_u8_stream_error_t *ptr) {
    if (!ptr->is_err) {
        imports_list_u8_free(&ptr->val.ok);
    } else {
        wasi_io_streams_stream_error_free(&ptr->val.err);
    }
}

void wasi_io_streams_result_u64_stream_error_free(wasi_io_streams_result_u64_stream_error_t *ptr) {
    if (ptr->is_err) {
        wasi_io_streams_stream_error_free(&ptr->val.err);
    }
}

void wasi_io_streams_result_void_stream_error_free(wasi_io_streams_result_void_stream_error_t *ptr) {
    if (ptr->is_err) {
        wasi_io_streams_stream_error_free(&ptr->val.err);
    }
}

/* ============================================================================
 * wasi:io/error Implementation
 * ============================================================================
 */

/**
 * Create a new error with message (internal helper)
 */
int32_t wasi_io_error_create(const char *message) {
    wasi_error_resource_t *err = (wasi_error_resource_t *)malloc(sizeof(wasi_error_resource_t));
    if (!err) return -1;

    err->message_len = strlen(message);
    err->message = (char *)malloc(err->message_len + 1);
    if (!err->message) {
        free(err);
        return -1;
    }
    memcpy(err->message, message, err->message_len + 1);

    return wasi_handle_alloc(HANDLE_TYPE_ERROR, err);
}

/**
 * Drop an owned error handle
 */
void wasi_io_error_error_drop_own(wasi_io_error_own_error_t handle) {
    wasi_error_resource_t *err = (wasi_error_resource_t *)wasi_handle_get(handle.__handle, HANDLE_TYPE_ERROR);
    if (err) {
        if (err->message) free(err->message);
        free(err);
    }
    wasi_handle_free(handle.__handle);
}

/**
 * Drop a borrowed error handle (no-op for borrows)
 */
void wasi_io_error_error_drop_borrow(wasi_io_error_borrow_error_t handle) {
    (void)handle;  /* Borrowed handles don't need cleanup */
}

/**
 * Convert owned to borrowed
 */
wasi_io_error_borrow_error_t wasi_io_error_borrow_error(wasi_io_error_own_error_t handle) {
    return (wasi_io_error_borrow_error_t){ handle.__handle };
}

/**
 * Returns a string that is suitable to assist humans in debugging this error.
 */
void wasi_io_error_method_error_to_debug_string(wasi_io_error_borrow_error_t self, imports_string_t *ret) {
    wasi_error_resource_t *err = (wasi_error_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_ERROR);
    if (err && err->message) {
        imports_string_dup(ret, err->message);
    } else {
        imports_string_dup(ret, "unknown error");
    }
}

/* ============================================================================
 * wasi:io/poll Implementation
 * ============================================================================
 */

/**
 * Create a pollable for a file descriptor (internal helper)
 */
int32_t wasi_io_poll_create_fd_pollable(int fd, bool for_write) {
    wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)malloc(sizeof(wasi_pollable_resource_t));
    if (!pollable) return -1;

    pollable->type = for_write ? POLLABLE_TYPE_FD_WRITE : POLLABLE_TYPE_FD_READ;
    pollable->fd = fd;
    pollable->when = 0;
    pollable->ready = false;

    return wasi_handle_alloc(HANDLE_TYPE_POLLABLE, pollable);
}

/**
 * Create a timer pollable for an instant (internal helper)
 */
int32_t wasi_io_poll_create_instant_pollable(uint64_t when) {
    wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)malloc(sizeof(wasi_pollable_resource_t));
    if (!pollable) return -1;

    pollable->type = POLLABLE_TYPE_INSTANT;
    pollable->fd = -1;
    pollable->when = when;
    pollable->ready = false;

    return wasi_handle_alloc(HANDLE_TYPE_POLLABLE, pollable);
}

/**
 * Create a timer pollable for a duration (internal helper)
 */
int32_t wasi_io_poll_create_duration_pollable(uint64_t duration_ns) {
    wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)malloc(sizeof(wasi_pollable_resource_t));
    if (!pollable) return -1;

    pollable->type = POLLABLE_TYPE_DURATION;
    pollable->fd = -1;
    pollable->when = wasi_platform_clock_monotonic() + duration_ns;
    pollable->ready = false;

    return wasi_handle_alloc(HANDLE_TYPE_POLLABLE, pollable);
}

/**
 * Create an always-ready pollable (internal helper)
 */
int32_t wasi_io_poll_create_ready_pollable(void) {
    wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)malloc(sizeof(wasi_pollable_resource_t));
    if (!pollable) return -1;

    pollable->type = POLLABLE_TYPE_ALWAYS_READY;
    pollable->fd = -1;
    pollable->when = 0;
    pollable->ready = true;

    return wasi_handle_alloc(HANDLE_TYPE_POLLABLE, pollable);
}

/**
 * Drop an owned pollable handle
 */
void wasi_io_poll_pollable_drop_own(wasi_io_poll_own_pollable_t handle) {
    wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)wasi_handle_get(handle.__handle, HANDLE_TYPE_POLLABLE);
    if (pollable) {
        free(pollable);
    }
    wasi_handle_free(handle.__handle);
}

/**
 * Drop a borrowed pollable handle (no-op for borrows)
 */
void wasi_io_poll_pollable_drop_borrow(wasi_io_poll_borrow_pollable_t handle) {
    (void)handle;  /* Borrowed handles don't need cleanup */
}

/**
 * Convert owned to borrowed
 */
wasi_io_poll_borrow_pollable_t wasi_io_poll_borrow_pollable(wasi_io_poll_own_pollable_t handle) {
    return (wasi_io_poll_borrow_pollable_t){ handle.__handle };
}

/**
 * Check if a single pollable is ready (internal helper)
 */
static bool wasi_pollable_check_ready(wasi_pollable_resource_t *pollable) {
    if (pollable->ready) return true;

    switch (pollable->type) {
        case POLLABLE_TYPE_ALWAYS_READY:
            return true;

        case POLLABLE_TYPE_FD_READ:
        case POLLABLE_TYPE_FD_WRITE: {
            struct pollfd pfd;
            pfd.fd = pollable->fd;
            pfd.events = (pollable->type == POLLABLE_TYPE_FD_READ) ? POLLIN : POLLOUT;
            pfd.revents = 0;
            int result = poll(&pfd, 1, 0);  /* Non-blocking */
            if (result > 0 && pfd.revents != 0) {
                pollable->ready = true;
                return true;
            }
            return false;
        }

        case POLLABLE_TYPE_INSTANT:
        case POLLABLE_TYPE_DURATION: {
            uint64_t now = wasi_platform_clock_monotonic();
            if (now >= pollable->when) {
                pollable->ready = true;
                return true;
            }
            return false;
        }

        default:
            return false;
    }
}

/**
 * Return the readiness of a pollable. This function never blocks.
 */
bool wasi_io_poll_method_pollable_ready(wasi_io_poll_borrow_pollable_t self) {
    wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_POLLABLE);
    if (!pollable) return true;  /* Invalid handle is "ready" */

    return wasi_pollable_check_ready(pollable);
}

/**
 * Block until the pollable is ready.
 */
void wasi_io_poll_method_pollable_block(wasi_io_poll_borrow_pollable_t self) {
    wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_POLLABLE);
    if (!pollable) return;

    if (pollable->ready) return;

    switch (pollable->type) {
        case POLLABLE_TYPE_ALWAYS_READY:
            return;

        case POLLABLE_TYPE_FD_READ:
        case POLLABLE_TYPE_FD_WRITE: {
            struct pollfd pfd;
            pfd.fd = pollable->fd;
            pfd.events = (pollable->type == POLLABLE_TYPE_FD_READ) ? POLLIN : POLLOUT;
            pfd.revents = 0;
            poll(&pfd, 1, -1);  /* Blocking */
            pollable->ready = true;
            return;
        }

        case POLLABLE_TYPE_INSTANT:
        case POLLABLE_TYPE_DURATION: {
            uint64_t now = wasi_platform_clock_monotonic();
            if (now < pollable->when) {
                uint64_t wait_ns = pollable->when - now;
                struct timespec ts;
                ts.tv_sec = (time_t)(wait_ns / 1000000000ULL);
                ts.tv_nsec = (long)(wait_ns % 1000000000ULL);
                nanosleep(&ts, NULL);
            }
            pollable->ready = true;
            return;
        }

        default:
            return;
    }
}

/**
 * Poll for completion on a set of pollables.
 */
void wasi_io_poll_poll(wasi_io_poll_list_borrow_pollable_t *in, imports_list_u32_t *ret) {
    if (in->len == 0) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    /* First, check if any are already ready */
    uint32_t *ready_indices = (uint32_t *)malloc(in->len * sizeof(uint32_t));
    if (!ready_indices) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    size_t num_ready = 0;
    int64_t min_timeout_ns = -1;  /* -1 = infinite */
    int num_fd_pollables = 0;

    /* First pass: count FD pollables and check for ready ones */
    for (size_t i = 0; i < in->len; i++) {
        wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)wasi_handle_get(
            in->ptr[i].__handle, HANDLE_TYPE_POLLABLE);
        if (!pollable) continue;

        if (wasi_pollable_check_ready(pollable)) {
            ready_indices[num_ready++] = (uint32_t)i;
        } else {
            if (pollable->type == POLLABLE_TYPE_FD_READ || pollable->type == POLLABLE_TYPE_FD_WRITE) {
                num_fd_pollables++;
            } else if (pollable->type == POLLABLE_TYPE_INSTANT || pollable->type == POLLABLE_TYPE_DURATION) {
                uint64_t now = wasi_platform_clock_monotonic();
                if (pollable->when > now) {
                    int64_t timeout = (int64_t)(pollable->when - now);
                    if (min_timeout_ns < 0 || timeout < min_timeout_ns) {
                        min_timeout_ns = timeout;
                    }
                } else {
                    ready_indices[num_ready++] = (uint32_t)i;
                }
            }
        }
    }

    /* If any are ready, return immediately */
    if (num_ready > 0) {
        ret->ptr = (uint32_t *)malloc(num_ready * sizeof(uint32_t));
        if (ret->ptr) {
            memcpy(ret->ptr, ready_indices, num_ready * sizeof(uint32_t));
            ret->len = num_ready;
        } else {
            ret->len = 0;
        }
        free(ready_indices);
        return;
    }

    /* Build poll array for FD pollables */
    if (num_fd_pollables > 0) {
        struct pollfd *pfds = (struct pollfd *)malloc((size_t)num_fd_pollables * sizeof(struct pollfd));
        size_t *pfd_indices = (size_t *)malloc((size_t)num_fd_pollables * sizeof(size_t));
        if (!pfds || !pfd_indices) {
            free(pfds);
            free(pfd_indices);
            free(ready_indices);
            ret->ptr = NULL;
            ret->len = 0;
            return;
        }

        int pfd_count = 0;
        for (size_t i = 0; i < in->len; i++) {
            wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)wasi_handle_get(
                in->ptr[i].__handle, HANDLE_TYPE_POLLABLE);
            if (!pollable) continue;

            if (pollable->type == POLLABLE_TYPE_FD_READ || pollable->type == POLLABLE_TYPE_FD_WRITE) {
                pfds[pfd_count].fd = pollable->fd;
                pfds[pfd_count].events = (short)((pollable->type == POLLABLE_TYPE_FD_READ) ? POLLIN : POLLOUT);
                pfds[pfd_count].revents = 0;
                pfd_indices[pfd_count] = i;
                pfd_count++;
            }
        }

        /* Calculate timeout in milliseconds */
        int timeout_ms = -1;
        if (min_timeout_ns >= 0) {
            timeout_ms = (int)(min_timeout_ns / 1000000);
            if (timeout_ms == 0 && min_timeout_ns > 0) {
                timeout_ms = 1;  /* Minimum 1ms */
            }
        }

        int result = poll(pfds, (nfds_t)pfd_count, timeout_ms);

        if (result > 0) {
            for (int i = 0; i < pfd_count; i++) {
                if (pfds[i].revents != 0) {
                    ready_indices[num_ready++] = (uint32_t)pfd_indices[i];
                    wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)wasi_handle_get(
                        in->ptr[pfd_indices[i]].__handle, HANDLE_TYPE_POLLABLE);
                    if (pollable) pollable->ready = true;
                }
            }
        }

        free(pfds);
        free(pfd_indices);
    } else if (min_timeout_ns > 0) {
        /* Only timer pollables, sleep */
        struct timespec ts;
        /* Cast to unsigned is safe since we checked min_timeout_ns > 0 */
        uint64_t timeout_ns = (uint64_t)min_timeout_ns;
        ts.tv_sec = (time_t)(timeout_ns / 1000000000ULL);
        ts.tv_nsec = (long)(timeout_ns % 1000000000ULL);
        nanosleep(&ts, NULL);
    }

    /* Check timers again */
    for (size_t i = 0; i < in->len; i++) {
        wasi_pollable_resource_t *pollable = (wasi_pollable_resource_t *)wasi_handle_get(
            in->ptr[i].__handle, HANDLE_TYPE_POLLABLE);
        if (!pollable) continue;

        if (pollable->type == POLLABLE_TYPE_INSTANT || pollable->type == POLLABLE_TYPE_DURATION) {
            if (wasi_pollable_check_ready(pollable)) {
                /* Check if not already in ready list */
                bool found = false;
                for (size_t j = 0; j < num_ready; j++) {
                    if (ready_indices[j] == (uint32_t)i) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ready_indices[num_ready++] = (uint32_t)i;
                }
            }
        }
    }

    /* Return ready indices */
    if (num_ready > 0) {
        ret->ptr = (uint32_t *)malloc(num_ready * sizeof(uint32_t));
        if (ret->ptr) {
            memcpy(ret->ptr, ready_indices, num_ready * sizeof(uint32_t));
            ret->len = num_ready;
        } else {
            ret->len = 0;
        }
    } else {
        ret->ptr = NULL;
        ret->len = 0;
    }

    free(ready_indices);
}

/* ============================================================================
 * wasi:io/streams Implementation
 * ============================================================================
 */

/**
 * Create an input stream from a file descriptor (internal helper)
 */
int32_t wasi_io_streams_create_input_stream(int fd, bool owns_fd) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)malloc(sizeof(wasi_stream_resource_t));
    if (!stream) return -1;

    stream->fd = fd;
    stream->closed = false;
    stream->owns_fd = owns_fd;
    stream->write_budget = 0;
    stream->flush_pending = false;

    return wasi_handle_alloc(HANDLE_TYPE_INPUT_STREAM, stream);
}

/**
 * Create an output stream from a file descriptor (internal helper)
 */
int32_t wasi_io_streams_create_output_stream(int fd, bool owns_fd) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)malloc(sizeof(wasi_stream_resource_t));
    if (!stream) return -1;

    stream->fd = fd;
    stream->closed = false;
    stream->owns_fd = owns_fd;
    stream->write_budget = 65536;  /* Default write budget */
    stream->flush_pending = false;

    return wasi_handle_alloc(HANDLE_TYPE_OUTPUT_STREAM, stream);
}

/**
 * Drop an owned input stream handle
 */
void wasi_io_streams_input_stream_drop_own(wasi_io_streams_own_input_stream_t handle) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(handle.__handle, HANDLE_TYPE_INPUT_STREAM);
    if (stream) {
        if (stream->owns_fd && stream->fd >= 0 && !stream->closed) {
            close(stream->fd);
        }
        free(stream);
    }
    wasi_handle_free(handle.__handle);
}

void wasi_io_streams_input_stream_drop_borrow(wasi_io_streams_borrow_input_stream_t handle) {
    (void)handle;
}

wasi_io_streams_borrow_input_stream_t wasi_io_streams_borrow_input_stream(wasi_io_streams_own_input_stream_t handle) {
    return (wasi_io_streams_borrow_input_stream_t){ handle.__handle };
}

/**
 * Drop an owned output stream handle
 */
void wasi_io_streams_output_stream_drop_own(wasi_io_streams_own_output_stream_t handle) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(handle.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    if (stream) {
        if (stream->owns_fd && stream->fd >= 0 && !stream->closed) {
            close(stream->fd);
        }
        free(stream);
    }
    wasi_handle_free(handle.__handle);
}

void wasi_io_streams_output_stream_drop_borrow(wasi_io_streams_borrow_output_stream_t handle) {
    (void)handle;
}

wasi_io_streams_borrow_output_stream_t wasi_io_streams_borrow_output_stream(wasi_io_streams_own_output_stream_t handle) {
    return (wasi_io_streams_borrow_output_stream_t){ handle.__handle };
}

/**
 * Helper to create stream error
 */
static void set_stream_error_closed(wasi_io_streams_stream_error_t *err) {
    err->tag = WASI_IO_STREAMS_STREAM_ERROR_CLOSED;
}

static void set_stream_error_failed(wasi_io_streams_stream_error_t *err, const char *message) {
    err->tag = WASI_IO_STREAMS_STREAM_ERROR_LAST_OPERATION_FAILED;
    int32_t error_handle = wasi_io_error_create(message);
    err->val.last_operation_failed = (wasi_io_streams_own_error_t){ error_handle };
}

/**
 * Perform a non-blocking read from the stream.
 */
bool wasi_io_streams_method_input_stream_read(
    wasi_io_streams_borrow_input_stream_t self,
    uint64_t len,
    imports_list_u8_t *ret,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_INPUT_STREAM);
    if (!stream) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    if (len == 0) {
        ret->ptr = NULL;
        ret->len = 0;
        return true;
    }

    /* Set non-blocking */
    int flags = fcntl(stream->fd, F_GETFL, 0);
    bool was_blocking = !(flags & O_NONBLOCK);
    if (was_blocking) {
        fcntl(stream->fd, F_SETFL, flags | O_NONBLOCK);
    }

    /* Allocate buffer */
    size_t to_read = (len > 65536) ? 65536 : (size_t)len;
    uint8_t *buf = (uint8_t *)malloc(to_read);
    if (!buf) {
        if (was_blocking) fcntl(stream->fd, F_SETFL, flags);
        set_stream_error_failed(err, "memory allocation failed");
        return false;
    }

    ssize_t nread = read(stream->fd, buf, to_read);

    /* Restore blocking mode */
    if (was_blocking) {
        fcntl(stream->fd, F_SETFL, flags);
    }

    if (nread < 0) {
        free(buf);
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* No data available, return empty list */
            ret->ptr = NULL;
            ret->len = 0;
            return true;
        }
        set_stream_error_failed(err, strerror(errno));
        return false;
    }

    if (nread == 0) {
        /* EOF */
        free(buf);
        stream->closed = true;
        set_stream_error_closed(err);
        return false;
    }

    ret->ptr = buf;
    ret->len = (size_t)nread;
    return true;
}

/**
 * Read bytes from a stream, after blocking until at least one byte can be read.
 */
bool wasi_io_streams_method_input_stream_blocking_read(
    wasi_io_streams_borrow_input_stream_t self,
    uint64_t len,
    imports_list_u8_t *ret,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_INPUT_STREAM);
    if (!stream) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    if (len == 0) {
        ret->ptr = NULL;
        ret->len = 0;
        return true;
    }

    /* Allocate buffer */
    size_t to_read = (len > 65536) ? 65536 : (size_t)len;
    uint8_t *buf = (uint8_t *)malloc(to_read);
    if (!buf) {
        set_stream_error_failed(err, "memory allocation failed");
        return false;
    }

    ssize_t nread = read(stream->fd, buf, to_read);

    if (nread < 0) {
        free(buf);
        set_stream_error_failed(err, strerror(errno));
        return false;
    }

    if (nread == 0) {
        /* EOF */
        free(buf);
        stream->closed = true;
        set_stream_error_closed(err);
        return false;
    }

    ret->ptr = buf;
    ret->len = (size_t)nread;
    return true;
}

/**
 * Skip bytes from a stream.
 */
bool wasi_io_streams_method_input_stream_skip(
    wasi_io_streams_borrow_input_stream_t self,
    uint64_t len,
    uint64_t *ret,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_INPUT_STREAM);
    if (!stream) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    /* Try lseek first (for seekable streams) */
    off_t cur = lseek(stream->fd, 0, SEEK_CUR);
    if (cur >= 0) {
        off_t end = lseek(stream->fd, 0, SEEK_END);
        if (end >= 0) {
            off_t available = end - cur;
            off_t to_skip = (off_t)len;
            if (to_skip > available) to_skip = available;
            lseek(stream->fd, cur + to_skip, SEEK_SET);
            *ret = (uint64_t)to_skip;
            return true;
        }
        lseek(stream->fd, cur, SEEK_SET);  /* Restore position */
    }

    /* Fall back to reading and discarding */
    uint8_t buf[4096];
    uint64_t skipped = 0;
    while (skipped < len) {
        size_t to_read = (len - skipped > sizeof(buf)) ? sizeof(buf) : (size_t)(len - skipped);
        ssize_t nread = read(stream->fd, buf, to_read);
        if (nread < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            set_stream_error_failed(err, strerror(errno));
            return false;
        }
        if (nread == 0) break;
        skipped += (uint64_t)nread;
    }

    *ret = skipped;
    return true;
}

/**
 * Skip bytes from a stream, after blocking.
 */
bool wasi_io_streams_method_input_stream_blocking_skip(
    wasi_io_streams_borrow_input_stream_t self,
    uint64_t len,
    uint64_t *ret,
    wasi_io_streams_stream_error_t *err
) {
    /* For simplicity, just call skip (which does blocking reads internally) */
    return wasi_io_streams_method_input_stream_skip(self, len, ret, err);
}

/**
 * Create a pollable for the input stream.
 */
wasi_io_streams_own_pollable_t wasi_io_streams_method_input_stream_subscribe(
    wasi_io_streams_borrow_input_stream_t self
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_INPUT_STREAM);
    if (!stream || stream->closed) {
        /* Closed streams are always ready */
        return (wasi_io_streams_own_pollable_t){ wasi_io_poll_create_ready_pollable() };
    }

    int32_t handle = wasi_io_poll_create_fd_pollable(stream->fd, false);
    return (wasi_io_streams_own_pollable_t){ handle };
}

/**
 * Check readiness for writing.
 */
bool wasi_io_streams_method_output_stream_check_write(
    wasi_io_streams_borrow_output_stream_t self,
    uint64_t *ret,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    if (!stream) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->flush_pending) {
        *ret = 0;
        return true;
    }

    *ret = stream->write_budget;
    return true;
}

/**
 * Perform a write.
 */
bool wasi_io_streams_method_output_stream_write(
    wasi_io_streams_borrow_output_stream_t self,
    imports_list_u8_t *contents,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    if (!stream) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    if (contents->len == 0) {
        return true;
    }

    /* Set non-blocking */
    int flags = fcntl(stream->fd, F_GETFL, 0);
    bool was_blocking = !(flags & O_NONBLOCK);
    if (was_blocking) {
        fcntl(stream->fd, F_SETFL, flags | O_NONBLOCK);
    }

    ssize_t nwritten = write(stream->fd, contents->ptr, contents->len);

    if (was_blocking) {
        fcntl(stream->fd, F_SETFL, flags);
    }

    if (nwritten < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* Would block, but we said we'd write - this is a bug in check_write */
            set_stream_error_failed(err, "write would block");
            return false;
        }
        set_stream_error_failed(err, strerror(errno));
        return false;
    }

    return true;
}

/**
 * Perform a write of up to 4096 bytes, and then flush the stream.
 */
bool wasi_io_streams_method_output_stream_blocking_write_and_flush(
    wasi_io_streams_borrow_output_stream_t self,
    imports_list_u8_t *contents,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    if (!stream) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    /* Write all data (blocking) */
    size_t written = 0;
    while (written < contents->len) {
        ssize_t n = write(stream->fd, contents->ptr + written, contents->len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            set_stream_error_failed(err, strerror(errno));
            return false;
        }
        written += (size_t)n;
    }

    /* Flush (fsync for files) */
    fsync(stream->fd);

    return true;
}

/**
 * Request to flush buffered output.
 */
bool wasi_io_streams_method_output_stream_flush(
    wasi_io_streams_borrow_output_stream_t self,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    if (!stream) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    stream->flush_pending = true;
    stream->write_budget = 0;

    /* Start async flush (for now, just mark as pending) */
    return true;
}

/**
 * Request to flush buffered output, and block until flush completes.
 */
bool wasi_io_streams_method_output_stream_blocking_flush(
    wasi_io_streams_borrow_output_stream_t self,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    if (!stream) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    /* Sync to disk */
    if (fsync(stream->fd) < 0) {
        set_stream_error_failed(err, strerror(errno));
        return false;
    }

    stream->flush_pending = false;
    stream->write_budget = 65536;

    return true;
}

/**
 * Create a pollable for the output stream.
 */
wasi_io_streams_own_pollable_t wasi_io_streams_method_output_stream_subscribe(
    wasi_io_streams_borrow_output_stream_t self
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    if (!stream || stream->closed) {
        return (wasi_io_streams_own_pollable_t){ wasi_io_poll_create_ready_pollable() };
    }

    int32_t handle = wasi_io_poll_create_fd_pollable(stream->fd, true);
    return (wasi_io_streams_own_pollable_t){ handle };
}

/**
 * Write zeroes to a stream.
 */
bool wasi_io_streams_method_output_stream_write_zeroes(
    wasi_io_streams_borrow_output_stream_t self,
    uint64_t len,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    if (!stream) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    if (len == 0) {
        return true;
    }

    /* Write zeroes */
    uint8_t zeros[4096] = {0};
    uint64_t written = 0;
    while (written < len) {
        size_t to_write = (len - written > sizeof(zeros)) ? sizeof(zeros) : (size_t)(len - written);
        ssize_t n = write(stream->fd, zeros, to_write);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            set_stream_error_failed(err, strerror(errno));
            return false;
        }
        written += (uint64_t)n;
    }

    return true;
}

/**
 * Perform a write of up to 4096 zeroes, and then flush the stream.
 */
bool wasi_io_streams_method_output_stream_blocking_write_zeroes_and_flush(
    wasi_io_streams_borrow_output_stream_t self,
    uint64_t len,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    if (!stream) {
        set_stream_error_closed(err);
        return false;
    }

    if (stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    /* Write zeroes (blocking) */
    uint8_t zeros[4096] = {0};
    uint64_t written = 0;
    while (written < len) {
        size_t to_write = (len - written > sizeof(zeros)) ? sizeof(zeros) : (size_t)(len - written);
        ssize_t n = write(stream->fd, zeros, to_write);
        if (n < 0) {
            if (errno == EINTR) continue;
            set_stream_error_failed(err, strerror(errno));
            return false;
        }
        written += (uint64_t)n;
    }

    /* Flush */
    fsync(stream->fd);

    return true;
}

/**
 * Read from one stream and write to another.
 */
bool wasi_io_streams_method_output_stream_splice(
    wasi_io_streams_borrow_output_stream_t self,
    wasi_io_streams_borrow_input_stream_t src,
    uint64_t len,
    uint64_t *ret,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *out_stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    wasi_stream_resource_t *in_stream = (wasi_stream_resource_t *)wasi_handle_get(src.__handle, HANDLE_TYPE_INPUT_STREAM);

    if (!out_stream || out_stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    if (!in_stream || in_stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    /* Simple splice implementation: read then write */
    uint8_t buf[4096];
    size_t to_transfer = (len > sizeof(buf)) ? sizeof(buf) : (size_t)len;

    /* Set non-blocking on input */
    int flags = fcntl(in_stream->fd, F_GETFL, 0);
    bool was_blocking = !(flags & O_NONBLOCK);
    if (was_blocking) {
        fcntl(in_stream->fd, F_SETFL, flags | O_NONBLOCK);
    }

    ssize_t nread = read(in_stream->fd, buf, to_transfer);

    if (was_blocking) {
        fcntl(in_stream->fd, F_SETFL, flags);
    }

    if (nread < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *ret = 0;
            return true;
        }
        set_stream_error_failed(err, strerror(errno));
        return false;
    }

    if (nread == 0) {
        *ret = 0;
        return true;
    }

    /* Write to output */
    ssize_t nwritten = write(out_stream->fd, buf, (size_t)nread);
    if (nwritten < 0) {
        set_stream_error_failed(err, strerror(errno));
        return false;
    }

    *ret = (uint64_t)nwritten;
    return true;
}

/**
 * Read from one stream and write to another, with blocking.
 */
bool wasi_io_streams_method_output_stream_blocking_splice(
    wasi_io_streams_borrow_output_stream_t self,
    wasi_io_streams_borrow_input_stream_t src,
    uint64_t len,
    uint64_t *ret,
    wasi_io_streams_stream_error_t *err
) {
    wasi_stream_resource_t *out_stream = (wasi_stream_resource_t *)wasi_handle_get(self.__handle, HANDLE_TYPE_OUTPUT_STREAM);
    wasi_stream_resource_t *in_stream = (wasi_stream_resource_t *)wasi_handle_get(src.__handle, HANDLE_TYPE_INPUT_STREAM);

    if (!out_stream || out_stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    if (!in_stream || in_stream->closed) {
        set_stream_error_closed(err);
        return false;
    }

    /* Blocking splice: read then write */
    uint8_t buf[4096];
    size_t to_transfer = (len > sizeof(buf)) ? sizeof(buf) : (size_t)len;

    ssize_t nread = read(in_stream->fd, buf, to_transfer);
    if (nread < 0) {
        set_stream_error_failed(err, strerror(errno));
        return false;
    }

    if (nread == 0) {
        *ret = 0;
        return true;
    }

    /* Write all data */
    size_t written = 0;
    while (written < (size_t)nread) {
        ssize_t n = write(out_stream->fd, buf + written, (size_t)nread - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            set_stream_error_failed(err, strerror(errno));
            return false;
        }
        written += (size_t)n;
    }

    *ret = written;
    return true;
}
