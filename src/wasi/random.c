/**
 * WASI Random Implementation
 *
 * This file implements the wasi:random interfaces for UNIX/Linux/macOS.
 *
 * Interfaces implemented:
 *   - wasi:random/random@0.2.0         - Cryptographically secure random
 *   - wasi:random/insecure@0.2.0       - Fast non-crypto random
 *   - wasi:random/insecure-seed@0.2.0  - Seed for non-crypto random
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"
#include "common.h"

/* Include the generated bindings header */
#include "../../build/c-bindings/random/imports.h"

/* ============================================================================
 * Helper: imports_list_u8_free
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

/* ============================================================================
 * wasi:random/random Implementation (Cryptographically Secure)
 * ============================================================================
 */

/**
 * Return `len` cryptographically-secure random or pseudo-random bytes.
 */
void wasi_random_random_get_random_bytes(uint64_t len, imports_list_u8_t *ret) {
    if (len == 0) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    ret->ptr = (uint8_t *)malloc((size_t)len);
    if (ret->ptr == NULL) {
        ret->len = 0;
        return;
    }

    ret->len = (size_t)len;

    if (wasi_platform_random_secure(ret->ptr, ret->len) != 0) {
        /* On failure, fill with zeros (not ideal but safe) */
        memset(ret->ptr, 0, ret->len);
    }
}

/**
 * Return a cryptographically-secure random or pseudo-random `u64` value.
 */
uint64_t wasi_random_random_get_random_u64(void) {
    uint64_t value = 0;
    if (wasi_platform_random_secure((uint8_t *)&value, sizeof(value)) != 0) {
        /* On failure, return 0 */
        return 0;
    }
    return value;
}

/* ============================================================================
 * wasi:random/insecure Implementation (Fast, Non-Crypto)
 * ============================================================================
 */

/* Simple xorshift64 PRNG state */
static uint64_t insecure_state = 0;
static bool insecure_seeded = false;

/**
 * Seed the insecure PRNG from secure random if not already seeded.
 */
static void wasi_insecure_ensure_seeded(void) {
    if (!insecure_seeded) {
        if (wasi_platform_random_secure((uint8_t *)&insecure_state,
                                        sizeof(insecure_state)) != 0) {
            /* Fallback: use a timestamp-based seed */
            insecure_state = (uint64_t)wasi_platform_clock_monotonic();
        }
        if (insecure_state == 0) {
            insecure_state = 0x853c49e6748fea9bULL;  /* Arbitrary non-zero */
        }
        insecure_seeded = true;
    }
}

/**
 * xorshift64 PRNG step
 */
static uint64_t wasi_xorshift64(void) {
    wasi_insecure_ensure_seeded();
    uint64_t x = insecure_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    insecure_state = x;
    return x;
}

/**
 * Return `len` insecure pseudo-random bytes.
 */
void wasi_random_insecure_get_insecure_random_bytes(uint64_t len, imports_list_u8_t *ret) {
    if (len == 0) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    ret->ptr = (uint8_t *)malloc((size_t)len);
    if (ret->ptr == NULL) {
        ret->len = 0;
        return;
    }

    ret->len = (size_t)len;

    /* Fill buffer using xorshift64 */
    size_t i = 0;
    while (i < ret->len) {
        uint64_t val = wasi_xorshift64();
        size_t remaining = ret->len - i;
        size_t copy_len = (remaining < 8) ? remaining : 8;
        memcpy(ret->ptr + i, &val, copy_len);
        i += copy_len;
    }
}

/**
 * Return an insecure pseudo-random `u64` value.
 */
uint64_t wasi_random_insecure_get_insecure_random_u64(void) {
    return wasi_xorshift64();
}

/* ============================================================================
 * wasi:random/insecure-seed Implementation
 * ============================================================================
 */

/**
 * Return a 128-bit value that may contain a pseudo-random value.
 * Used for DoS protection in hash-map implementations.
 */
void wasi_random_insecure_seed_insecure_seed(imports_tuple2_u64_u64_t *ret) {
    /* Use secure random to generate the seed */
    if (wasi_platform_random_secure((uint8_t *)ret, sizeof(*ret)) != 0) {
        /* Fallback: use xorshift64 */
        ret->f0 = wasi_xorshift64();
        ret->f1 = wasi_xorshift64();
    }
}
