/**
 * WASI Common Implementation
 *
 * Shared functions used across all WASI implementation modules.
 */

#include "common.h"
#include <stdlib.h>
#include <string.h>

/**
 * Component Model ABI realloc function.
 *
 * Required by wit-bindgen generated C bindings. Called during string/list
 * construction and return value marshaling.
 */
static bool is_power_of_two(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

void *cabi_realloc(void *ptr, size_t old_size, size_t align, size_t new_size) {
    void *ret = NULL;
    size_t min_align;

    if (!is_power_of_two(align)) {
        abort();
    }

    /* Handle zero-size allocation per Component Model ABI */
    if (new_size == 0) {
        free(ptr);
        return (void *)align;  /* Return non-NULL sentinel */
    }

    min_align = align < sizeof(void *) ? sizeof(void *) : align;

    if (!ptr) {
        if (min_align <= WASI_ALIGNOF(max_align_t)) {
            ret = malloc(new_size);
        } else {
            if (posix_memalign(&ret, min_align, new_size) != 0) {
                ret = NULL;
            }
        }
    } else if (min_align <= WASI_ALIGNOF(max_align_t)) {
        ret = realloc(ptr, new_size);
    } else {
        if (posix_memalign(&ret, min_align, new_size) != 0) {
            ret = NULL;
        }
        if (ret) {
            size_t copy_size = old_size < new_size ? old_size : new_size;
            memcpy(ret, ptr, copy_size);
            free(ptr);
        }
    }

    if (!ret) {
        abort();  /* Component Model requires abort on allocation failure */
    }
    if (((uintptr_t)ret & (align - 1)) != 0) {
        abort();
    }
    return ret;
}

void *wasi_cabi_alloc(size_t align, size_t size) {
    if (size == 0) {
        return NULL;
    }
    return cabi_realloc(NULL, 0, align, size);
}

void wasi_cabi_free(void *ptr, size_t align) {
    if (!ptr) {
        return;
    }
    (void)cabi_realloc(ptr, 0, align, 0);
}

bool wasi_cabi_alloc_list(size_t len, size_t elem_size, size_t align, void **out_ptr) {
    size_t byte_len;

    if (len == 0) {
        *out_ptr = NULL;
        return true;
    }
    if (elem_size != 0 && len > UINT32_MAX / elem_size) {
        return false;
    }
    byte_len = len * elem_size;
    if (byte_len > UINT32_MAX) {
        return false;
    }
    *out_ptr = wasi_cabi_alloc(align, byte_len);
    return *out_ptr != NULL;
}

bool wasi_cabi_alloc_string(size_t len, uint8_t **out_ptr) {
    if (len == 0) {
        *out_ptr = NULL;
        return true;
    }
    if (len > UINT32_MAX) {
        return false;
    }
    *out_ptr = (uint8_t *)wasi_cabi_alloc(1, len);
    return *out_ptr != NULL;
}

bool wasi_utf8_validate(const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c;

        c = data[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            uint8_t c1;
            uint32_t code;

            if (i + 1 >= len) return false;
            c1 = data[i + 1];
            if ((c1 & 0xC0) != 0x80) return false;
            code = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
            if (code < 0x80) return false;
            i += 2;
            continue;
        }
        if ((c & 0xF0) == 0xE0) {
            uint8_t c1;
            uint8_t c2;
            uint32_t code;

            if (i + 2 >= len) return false;
            c1 = data[i + 1];
            c2 = data[i + 2];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
            code = ((uint32_t)(c & 0x0F) << 12) |
                   ((uint32_t)(c1 & 0x3F) << 6) |
                   (uint32_t)(c2 & 0x3F);
            if (code < 0x800) return false;
            if (code >= 0xD800 && code <= 0xDFFF) return false;
            i += 3;
            continue;
        }
        if ((c & 0xF8) == 0xF0) {
            uint8_t c1;
            uint8_t c2;
            uint8_t c3;
            uint32_t code;

            if (i + 3 >= len) return false;
            c1 = data[i + 1];
            c2 = data[i + 2];
            c3 = data[i + 3];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
                return false;
            }
            code = ((uint32_t)(c & 0x07) << 18) |
                   ((uint32_t)(c1 & 0x3F) << 12) |
                   ((uint32_t)(c2 & 0x3F) << 6) |
                   (uint32_t)(c3 & 0x3F);
            if (code < 0x10000 || code > 0x10FFFF) return false;
            i += 4;
            continue;
        }
        return false;
    }
    return true;
}

void wasi_utf8_validate_or_abort(const uint8_t *data, size_t len) {
    if (!wasi_utf8_validate(data, len)) {
        abort();
    }
}
