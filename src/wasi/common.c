/**
 * WASI Common Implementation
 *
 * Shared functions used across all WASI implementation modules.
 */

#include "common.h"
#include <stdlib.h>

/**
 * Component Model ABI realloc function.
 *
 * Required by wit-bindgen generated C bindings. Called during string/list
 * construction and return value marshaling.
 */
void *cabi_realloc(void *ptr, size_t old_size, size_t align, size_t new_size) {
    (void)old_size;
    (void)align;

    /* Handle zero-size allocation per Component Model ABI */
    if (new_size == 0) {
        free(ptr);
        return (void*)align;  /* Return non-NULL sentinel */
    }

    void *ret = realloc(ptr, new_size);
    if (!ret) {
        abort();  /* Component Model requires abort on allocation failure */
    }
    return ret;
}
