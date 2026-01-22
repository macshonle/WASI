/**
 * Tests for WASI Filesystem Implementation
 *
 * Run: make test-filesystem
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>

/* Test framework macros */
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    test_##name(); \
    printf("  %s: PASS\n", #name); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* Include the generated bindings header */
#include "../build/c-bindings/filesystem/imports.h"

/* Helper to create a WASI string from a C string */
static void make_string(imports_string_t *str, const char *cstr) {
    str->len = strlen(cstr);
    str->ptr = (uint8_t *)malloc(str->len);
    memcpy(str->ptr, cstr, str->len);
}

/* ============================================================================
 * Test: Preopens returns at least one directory
 * ============================================================================
 */
TEST(preopens_get_directories) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
    wasi_filesystem_preopens_get_directories(&preopens);

    /* Should have at least one preopened directory */
    assert(preopens.len >= 1);

    /* First preopen should have a valid handle */
    assert(preopens.ptr[0].f0.__handle > 0);

    /* Clean up */
    for (size_t i = 0; i < preopens.len; i++) {
        wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
        imports_string_free(&preopens.ptr[i].f1);
    }
    free(preopens.ptr);
}

/* ============================================================================
 * Test: Get descriptor type for directory
 * ============================================================================
 */
TEST(descriptor_get_type_directory) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
    wasi_filesystem_preopens_get_directories(&preopens);
    assert(preopens.len >= 1);

    wasi_filesystem_types_borrow_descriptor_t borrow =
        wasi_filesystem_types_borrow_descriptor(preopens.ptr[0].f0);

    wasi_filesystem_types_descriptor_type_t type;
    wasi_filesystem_types_error_code_t err;
    bool ok = wasi_filesystem_types_method_descriptor_get_type(borrow, &type, &err);
    assert(ok);
    assert(type == WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_DIRECTORY);

    /* Clean up */
    for (size_t i = 0; i < preopens.len; i++) {
        wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
        imports_string_free(&preopens.ptr[i].f1);
    }
    free(preopens.ptr);
}

/* ============================================================================
 * Test: Stat on a descriptor
 * ============================================================================
 */
TEST(descriptor_stat) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
    wasi_filesystem_preopens_get_directories(&preopens);
    assert(preopens.len >= 1);

    wasi_filesystem_types_borrow_descriptor_t borrow =
        wasi_filesystem_types_borrow_descriptor(preopens.ptr[0].f0);

    wasi_filesystem_types_descriptor_stat_t stat;
    wasi_filesystem_types_error_code_t err;
    bool ok = wasi_filesystem_types_method_descriptor_stat(borrow, &stat, &err);
    assert(ok);
    assert(stat.type == WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_DIRECTORY);
    assert(stat.link_count >= 1);

    /* Timestamps should be present */
    assert(stat.data_access_timestamp.is_some);
    assert(stat.data_modification_timestamp.is_some);
    assert(stat.status_change_timestamp.is_some);

    /* Clean up */
    for (size_t i = 0; i < preopens.len; i++) {
        wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
        imports_string_free(&preopens.ptr[i].f1);
    }
    free(preopens.ptr);
}

/* ============================================================================
 * Test: Read directory entries
 * ============================================================================
 */
TEST(read_directory) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
    wasi_filesystem_preopens_get_directories(&preopens);
    assert(preopens.len >= 1);

    wasi_filesystem_types_borrow_descriptor_t borrow =
        wasi_filesystem_types_borrow_descriptor(preopens.ptr[0].f0);

    wasi_filesystem_types_own_directory_entry_stream_t dir_stream;
    wasi_filesystem_types_error_code_t err;
    bool ok = wasi_filesystem_types_method_descriptor_read_directory(borrow, &dir_stream, &err);
    assert(ok);
    assert(dir_stream.__handle > 0);

    /* Read at least one entry */
    wasi_filesystem_types_borrow_directory_entry_stream_t stream_borrow =
        wasi_filesystem_types_borrow_directory_entry_stream(dir_stream);

    wasi_filesystem_types_option_directory_entry_t entry;
    ok = wasi_filesystem_types_method_directory_entry_stream_read_directory_entry(stream_borrow, &entry, &err);
    assert(ok);
    /* Root directory should have at least one entry */
    assert(entry.is_some);
    assert(entry.val.name.len > 0);

    /* Free entry name */
    imports_string_free(&entry.val.name);

    /* Clean up */
    wasi_filesystem_types_directory_entry_stream_drop_own(dir_stream);
    for (size_t i = 0; i < preopens.len; i++) {
        wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
        imports_string_free(&preopens.ptr[i].f1);
    }
    free(preopens.ptr);
}

/* ============================================================================
 * Test: Stat at path
 * ============================================================================
 */
TEST(stat_at) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
    wasi_filesystem_preopens_get_directories(&preopens);
    assert(preopens.len >= 1);

    wasi_filesystem_types_borrow_descriptor_t borrow =
        wasi_filesystem_types_borrow_descriptor(preopens.ptr[0].f0);

    imports_string_t path;
    make_string(&path, ".");

    wasi_filesystem_types_descriptor_stat_t stat;
    wasi_filesystem_types_error_code_t err;
    bool ok = wasi_filesystem_types_method_descriptor_stat_at(
        borrow, WASI_FILESYSTEM_TYPES_PATH_FLAGS_SYMLINK_FOLLOW, &path, &stat, &err);
    assert(ok);
    assert(stat.type == WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_DIRECTORY);

    imports_string_free(&path);

    /* Clean up */
    for (size_t i = 0; i < preopens.len; i++) {
        wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
        imports_string_free(&preopens.ptr[i].f1);
    }
    free(preopens.ptr);
}

/* ============================================================================
 * Test: Create and remove directory
 * Note: Skipped in safe mode (WASI_SAFE_MODE) as it's a destructive operation
 * ============================================================================
 */
#ifndef WASI_SAFE_MODE
TEST(create_remove_directory) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
    wasi_filesystem_preopens_get_directories(&preopens);
    assert(preopens.len >= 1);  /* Need "." preopen */

    /* Use "." preopen (index 0 - the writable current directory) */
    wasi_filesystem_types_borrow_descriptor_t borrow =
        wasi_filesystem_types_borrow_descriptor(preopens.ptr[0].f0);

    imports_string_t path;
    make_string(&path, "test_dir_wasi_temp");

    wasi_filesystem_types_error_code_t err;

    /* Remove if exists (ignore errors) */
    wasi_filesystem_types_method_descriptor_remove_directory_at(borrow, &path, &err);

    /* Create directory */
    bool ok = wasi_filesystem_types_method_descriptor_create_directory_at(borrow, &path, &err);
    assert(ok);

    /* Verify it exists */
    wasi_filesystem_types_descriptor_stat_t stat;
    ok = wasi_filesystem_types_method_descriptor_stat_at(
        borrow, WASI_FILESYSTEM_TYPES_PATH_FLAGS_SYMLINK_FOLLOW, &path, &stat, &err);
    assert(ok);
    assert(stat.type == WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_DIRECTORY);

    /* Remove directory */
    ok = wasi_filesystem_types_method_descriptor_remove_directory_at(borrow, &path, &err);
    assert(ok);

    /* Verify it's gone */
    ok = wasi_filesystem_types_method_descriptor_stat_at(
        borrow, WASI_FILESYSTEM_TYPES_PATH_FLAGS_SYMLINK_FOLLOW, &path, &stat, &err);
    assert(!ok);  /* Should fail */
    assert(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_NO_ENTRY);

    imports_string_free(&path);

    /* Clean up */
    for (size_t i = 0; i < preopens.len; i++) {
        wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
        imports_string_free(&preopens.ptr[i].f1);
    }
    free(preopens.ptr);
}
#endif

/* ============================================================================
 * Test: Open and read file
 * ============================================================================
 */
TEST(open_read_file) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
    wasi_filesystem_preopens_get_directories(&preopens);
    assert(preopens.len >= 1);

    wasi_filesystem_types_borrow_descriptor_t borrow =
        wasi_filesystem_types_borrow_descriptor(preopens.ptr[0].f0);

    /* Try to open /etc/passwd which should exist on most systems */
    imports_string_t path;
    make_string(&path, "etc/passwd");

    wasi_filesystem_types_own_descriptor_t file;
    wasi_filesystem_types_error_code_t err;
    bool ok = wasi_filesystem_types_method_descriptor_open_at(
        borrow,
        WASI_FILESYSTEM_TYPES_PATH_FLAGS_SYMLINK_FOLLOW,
        &path,
        0,  /* no special open flags */
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_READ,
        &file,
        &err);

    imports_string_free(&path);

    if (!ok) {
        /* /etc/passwd might not exist in some environments, skip test */
        for (size_t i = 0; i < preopens.len; i++) {
            wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
            imports_string_free(&preopens.ptr[i].f1);
        }
        free(preopens.ptr);
        return;
    }

    /* Read some data */
    wasi_filesystem_types_borrow_descriptor_t file_borrow =
        wasi_filesystem_types_borrow_descriptor(file);

    imports_tuple2_list_u8_bool_t read_result;
    ok = wasi_filesystem_types_method_descriptor_read(file_borrow, 100, 0, &read_result, &err);
    assert(ok);
    assert(read_result.f0.len > 0);  /* Should have read something */

    imports_list_u8_free(&read_result.f0);
    wasi_filesystem_types_descriptor_drop_own(file);

    /* Clean up */
    for (size_t i = 0; i < preopens.len; i++) {
        wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
        imports_string_free(&preopens.ptr[i].f1);
    }
    free(preopens.ptr);
}

/* ============================================================================
 * Test: Descriptor flags
 * ============================================================================
 */
TEST(descriptor_get_flags) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
    wasi_filesystem_preopens_get_directories(&preopens);
    assert(preopens.len >= 1);

    wasi_filesystem_types_borrow_descriptor_t borrow =
        wasi_filesystem_types_borrow_descriptor(preopens.ptr[0].f0);

    wasi_filesystem_types_descriptor_flags_t flags;
    wasi_filesystem_types_error_code_t err;
    bool ok = wasi_filesystem_types_method_descriptor_get_flags(borrow, &flags, &err);
    assert(ok);
    /* Preopened directory should have at least READ flag */
    assert(flags & WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_READ);

    /* Clean up */
    for (size_t i = 0; i < preopens.len; i++) {
        wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
        imports_string_free(&preopens.ptr[i].f1);
    }
    free(preopens.ptr);
}

/* ============================================================================
 * Test: Is same object
 * ============================================================================
 */
TEST(is_same_object) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
    wasi_filesystem_preopens_get_directories(&preopens);
    assert(preopens.len >= 1);

    /* Same descriptor should be same object */
    wasi_filesystem_types_borrow_descriptor_t borrow1 =
        wasi_filesystem_types_borrow_descriptor(preopens.ptr[0].f0);
    wasi_filesystem_types_borrow_descriptor_t borrow2 =
        wasi_filesystem_types_borrow_descriptor(preopens.ptr[0].f0);

    bool same = wasi_filesystem_types_method_descriptor_is_same_object(borrow1, borrow2);
    assert(same);

    /* Clean up */
    for (size_t i = 0; i < preopens.len; i++) {
        wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
        imports_string_free(&preopens.ptr[i].f1);
    }
    free(preopens.ptr);
}

/* ============================================================================
 * Main test runner
 * ============================================================================
 */
int run_filesystem_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("Running WASI Filesystem tests...\n");

    RUN_TEST(preopens_get_directories);
    RUN_TEST(descriptor_get_type_directory);
    RUN_TEST(descriptor_stat);
    RUN_TEST(read_directory);
    RUN_TEST(stat_at);
#ifndef WASI_SAFE_MODE
    RUN_TEST(create_remove_directory);
#endif
    RUN_TEST(open_read_file);
    RUN_TEST(descriptor_get_flags);
    RUN_TEST(is_same_object);

    return tests_failed;
}

/* Allow standalone execution */
#ifndef TEST_RUNNER_MODE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_filesystem_tests();
}
#endif
