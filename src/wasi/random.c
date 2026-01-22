/**
 * WASI Random Implementation
 *
 * This file implements the wasi:random interfaces for UNIX/Linux/macOS.
 *
 * Interfaces implemented:
 *   - wasi:random/random@0.2.0         - Cryptographically secure random
 *   - wasi:random/insecure@0.2.0       - Fast non-crypto random
 *   - wasi:random/insecure-seed@0.2.0  - Seed for non-crypto random
 *
 * Implementation Status:
 *   [ ] random interface
 *   [ ] insecure interface
 *   [ ] insecure-seed interface
 *
 * Platform notes:
 *   - Linux: Use getrandom() syscall or /dev/urandom
 *   - macOS: Use arc4random_buf() (preferred) or /dev/urandom
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

/* Platform detection */
#if defined(__linux__)
    #define WASI_PLATFORM_LINUX 1
    #include <sys/random.h>  /* getrandom() */
#elif defined(__APPLE__)
    #define WASI_PLATFORM_DARWIN 1
    #include <stdlib.h>      /* arc4random_buf() */
#endif

/* Include the generated bindings header */
/* Note: Run 'make v0.2.0' first to generate this file */
// #include "../../build/c-bindings/random/imports.h"

/* ============================================================================
 * wasi:random/random Implementation (Cryptographically Secure)
 * ============================================================================
 */

/**
 * Fill buffer with cryptographically secure random bytes.
 *
 * Platform implementations:
 *   - Linux: getrandom(buf, len, 0) or read from /dev/urandom
 *   - macOS: arc4random_buf(buf, len)
 */
static int wasi_random_fill_secure(uint8_t *buf, size_t len) {
#if WASI_PLATFORM_LINUX
    /* Linux: use getrandom() syscall */
    ssize_t result = getrandom(buf, len, 0);
    if (result < 0 || (size_t)result != len) {
        return -1;
    }
    return 0;
#elif WASI_PLATFORM_DARWIN
    /* macOS: use arc4random_buf() - always succeeds */
    arc4random_buf(buf, len);
    return 0;
#else
    /* Fallback: /dev/urandom */
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    size_t read = fread(buf, 1, len, f);
    fclose(f);
    return (read == len) ? 0 : -1;
#endif
}

/* TODO: Implement wasi_random_random_get_random_bytes */
/*
void wasi_random_random_get_random_bytes(uint64_t len, imports_list_u8_t *ret) {
    ret->ptr = malloc(len);
    ret->len = len;
    if (ret->ptr) {
        wasi_random_fill_secure(ret->ptr, len);
    }
}
*/

/* TODO: Implement wasi_random_random_get_random_u64 */
/*
uint64_t wasi_random_random_get_random_u64(void) {
    uint64_t value;
    wasi_random_fill_secure((uint8_t *)&value, sizeof(value));
    return value;
}
*/

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
        wasi_random_fill_secure((uint8_t *)&insecure_state, sizeof(insecure_state));
        if (insecure_state == 0) insecure_state = 1;  /* Avoid zero state */
        insecure_seeded = true;
    }
}

/**
 * xorshift64 PRNG step
 */
static uint64_t wasi_xorshift64(void) {
    uint64_t x = insecure_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    insecure_state = x;
    return x;
}

/* TODO: Implement wasi_random_insecure_get_insecure_random_bytes */
/*
void wasi_random_insecure_get_insecure_random_bytes(uint64_t len, imports_list_u8_t *ret) {
    wasi_insecure_ensure_seeded();
    ret->ptr = malloc(len);
    ret->len = len;
    if (ret->ptr) {
        for (size_t i = 0; i < len; i += 8) {
            uint64_t val = wasi_xorshift64();
            size_t copy_len = (len - i < 8) ? (len - i) : 8;
            memcpy(ret->ptr + i, &val, copy_len);
        }
    }
}
*/

/* TODO: Implement wasi_random_insecure_get_insecure_random_u64 */
/*
uint64_t wasi_random_insecure_get_insecure_random_u64(void) {
    wasi_insecure_ensure_seeded();
    return wasi_xorshift64();
}
*/

/* ============================================================================
 * wasi:random/insecure-seed Implementation
 * ============================================================================
 */

/* TODO: Implement wasi_random_insecure_seed_insecure_seed */
/*
void wasi_random_insecure_seed_insecure_seed(imports_tuple2_u64_u64_t *ret) {
    wasi_random_fill_secure((uint8_t *)ret, sizeof(*ret));
}
*/
