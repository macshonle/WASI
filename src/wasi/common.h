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

#endif /* WASI_COMMON_H */
