/**
 * WASI Error Case Tests
 *
 * This file tests error handling in the WASI implementation.
 * It verifies that errors are correctly identified and reported.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Include filesystem bindings */
#include "filesystem/imports.h"

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("    FAIL: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(name) do { \
    printf("  %s: PASS\n", name); \
    tests_passed++; \
} while(0)

/* Helper to get the first preopened directory */
static bool get_preopen_dir(wasi_filesystem_types_own_descriptor_t *out_dir) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
    wasi_filesystem_preopens_get_directories(&preopens);

    if (preopens.len == 0) {
        return false;
    }

    /* Take the first preopen */
    *out_dir = preopens.ptr[0].f0;

    /* Drop handles for remaining preopens */
    for (size_t i = 1; i < preopens.len; i++) {
        wasi_filesystem_types_descriptor_drop_own(preopens.ptr[i].f0);
        imports_string_free(&preopens.ptr[i].f1);
    }

    /* Free the first name string */
    imports_string_free(&preopens.ptr[0].f1);

    /* Free the list (but not the items since we've taken ownership of f0) */
    free(preopens.ptr);

    return true;
}

/* ============================================================================
 * Filesystem Error Tests
 * ============================================================================ */

/* Test: Opening non-existent file should fail */
void test_open_nonexistent_file(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    imports_string_t path;
    path.ptr = (uint8_t *)"this_file_definitely_does_not_exist_12345.txt";
    path.len = strlen((char *)path.ptr);

    wasi_filesystem_types_open_flags_t open_flags = 0;
    wasi_filesystem_types_descriptor_flags_t desc_flags =
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_READ;

    wasi_filesystem_types_own_descriptor_t result;
    wasi_filesystem_types_error_code_t err;

    bool success = wasi_filesystem_types_method_descriptor_open_at(
        dir, open_flags, &path, open_flags, desc_flags, &result, &err);

    TEST_ASSERT(!success, "opening non-existent file should fail");
    TEST_ASSERT(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_NO_ENTRY,
                "expected NO_ENTRY error code");

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("open_nonexistent_file");
}

/* Test: Creating file in non-existent directory should fail */
void test_create_in_nonexistent_dir(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    imports_string_t path;
    path.ptr = (uint8_t *)"nonexistent_dir/file.txt";
    path.len = strlen((char *)path.ptr);

    wasi_filesystem_types_open_flags_t open_flags =
        WASI_FILESYSTEM_TYPES_OPEN_FLAGS_CREATE;
    wasi_filesystem_types_descriptor_flags_t desc_flags =
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_WRITE;

    wasi_filesystem_types_own_descriptor_t result;
    wasi_filesystem_types_error_code_t err;

    bool success = wasi_filesystem_types_method_descriptor_open_at(
        dir, open_flags, &path, open_flags, desc_flags, &result, &err);

    TEST_ASSERT(!success, "creating file in non-existent dir should fail");
    TEST_ASSERT(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_NO_ENTRY,
                "expected NO_ENTRY error code");

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("create_in_nonexistent_dir");
}

/* Test: Stat on non-existent file should fail */
void test_stat_nonexistent(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    imports_string_t path;
    path.ptr = (uint8_t *)"nonexistent_file.txt";
    path.len = strlen((char *)path.ptr);

    wasi_filesystem_types_descriptor_stat_t stat_result;
    wasi_filesystem_types_error_code_t err;

    bool success = wasi_filesystem_types_method_descriptor_stat_at(
        dir, 0, &path, &stat_result, &err);

    TEST_ASSERT(!success, "stat on non-existent file should fail");
    TEST_ASSERT(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_NO_ENTRY,
                "expected NO_ENTRY error code");

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("stat_nonexistent");
}

/* Test: Remove non-existent directory should fail */
void test_remove_nonexistent_dir(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    imports_string_t path;
    path.ptr = (uint8_t *)"nonexistent_directory_to_remove";
    path.len = strlen((char *)path.ptr);

    wasi_filesystem_types_error_code_t err;
    bool success = wasi_filesystem_types_method_descriptor_remove_directory_at(
        dir, &path, &err);

    TEST_ASSERT(!success, "removing non-existent directory should fail");
    TEST_ASSERT(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_NO_ENTRY,
                "expected NO_ENTRY error code");

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("remove_nonexistent_dir");
}

/* Test: Unlink non-existent file should fail */
void test_unlink_nonexistent(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    imports_string_t path;
    path.ptr = (uint8_t *)"nonexistent_file_to_unlink.txt";
    path.len = strlen((char *)path.ptr);

    wasi_filesystem_types_error_code_t err;
    bool success = wasi_filesystem_types_method_descriptor_unlink_file_at(
        dir, &path, &err);

    TEST_ASSERT(!success, "unlinking non-existent file should fail");
    TEST_ASSERT(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_NO_ENTRY,
                "expected NO_ENTRY error code");

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("unlink_nonexistent");
}

/* Test: Remove directory that is not empty should fail */
void test_remove_nonempty_dir(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    /* Create a directory */
    imports_string_t dirname;
    dirname.ptr = (uint8_t *)"test_nonempty_dir";
    dirname.len = strlen((char *)dirname.ptr);

    wasi_filesystem_types_error_code_t err;
    wasi_filesystem_types_method_descriptor_create_directory_at(dir, &dirname, &err);

    /* Create a file inside it */
    imports_string_t file_path;
    file_path.ptr = (uint8_t *)"test_nonempty_dir/file_inside.txt";
    file_path.len = strlen((char *)file_path.ptr);

    wasi_filesystem_types_own_descriptor_t file;
    wasi_filesystem_types_open_flags_t file_open_flags =
        WASI_FILESYSTEM_TYPES_OPEN_FLAGS_CREATE;
    wasi_filesystem_types_descriptor_flags_t file_desc_flags =
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_WRITE;

    bool file_created = wasi_filesystem_types_method_descriptor_open_at(
        dir, 0, &file_path, file_open_flags, file_desc_flags, &file, &err);

    if (file_created) {
        wasi_filesystem_types_descriptor_drop_own(file);
    }

    /* Now try to remove the non-empty directory */
    bool success = wasi_filesystem_types_method_descriptor_remove_directory_at(
        dir, &dirname, &err);

    TEST_ASSERT(!success, "removing non-empty directory should fail");
    TEST_ASSERT(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_NOT_EMPTY,
                "expected NOT_EMPTY error code");

    /* Cleanup */
    wasi_filesystem_types_method_descriptor_unlink_file_at(dir, &file_path, &err);
    wasi_filesystem_types_method_descriptor_remove_directory_at(dir, &dirname, &err);

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("remove_nonempty_dir");
}

/* Test: Read from write-only file should fail */
void test_read_writeonly_file(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    /* Create a write-only file */
    imports_string_t path;
    path.ptr = (uint8_t *)"test_writeonly.txt";
    path.len = strlen((char *)path.ptr);

    wasi_filesystem_types_own_descriptor_t file;
    wasi_filesystem_types_open_flags_t open_flags =
        WASI_FILESYSTEM_TYPES_OPEN_FLAGS_CREATE |
        WASI_FILESYSTEM_TYPES_OPEN_FLAGS_TRUNCATE;
    wasi_filesystem_types_descriptor_flags_t desc_flags =
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_WRITE;

    wasi_filesystem_types_error_code_t err;
    bool opened = wasi_filesystem_types_method_descriptor_open_at(
        dir, 0, &path, open_flags, desc_flags, &file, &err);

    TEST_ASSERT(opened, "failed to create write-only file");

    wasi_filesystem_types_borrow_descriptor_t file_borrow =
        wasi_filesystem_types_borrow_descriptor(file);

    /* Try to read from write-only file */
    imports_tuple2_list_u8_bool_t read_result;
    bool read_success = wasi_filesystem_types_method_descriptor_read(
        file_borrow, 100, 0, &read_result, &err);

    TEST_ASSERT(!read_success, "reading from write-only file should fail");
    TEST_ASSERT(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR,
                "expected BAD_DESCRIPTOR error code");

    /* Cleanup */
    wasi_filesystem_types_descriptor_drop_own(file);
    wasi_filesystem_types_method_descriptor_unlink_file_at(dir, &path, &err);

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("read_writeonly_file");
}

/* Test: Write to read-only file should fail */
void test_write_readonly_file(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    /* First create a file */
    imports_string_t path;
    path.ptr = (uint8_t *)"test_readonly.txt";
    path.len = strlen((char *)path.ptr);

    wasi_filesystem_types_own_descriptor_t file_write;
    wasi_filesystem_types_open_flags_t create_flags =
        WASI_FILESYSTEM_TYPES_OPEN_FLAGS_CREATE |
        WASI_FILESYSTEM_TYPES_OPEN_FLAGS_TRUNCATE;
    wasi_filesystem_types_descriptor_flags_t write_desc_flags =
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_WRITE;

    wasi_filesystem_types_error_code_t err;
    bool created = wasi_filesystem_types_method_descriptor_open_at(
        dir, 0, &path, create_flags, write_desc_flags, &file_write, &err);

    if (created) {
        wasi_filesystem_types_descriptor_drop_own(file_write);
    }

    /* Now open read-only */
    wasi_filesystem_types_own_descriptor_t file;
    wasi_filesystem_types_open_flags_t open_flags = 0;
    wasi_filesystem_types_descriptor_flags_t desc_flags =
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_READ;

    bool opened = wasi_filesystem_types_method_descriptor_open_at(
        dir, 0, &path, open_flags, desc_flags, &file, &err);

    TEST_ASSERT(opened, "failed to open file read-only");

    wasi_filesystem_types_borrow_descriptor_t file_borrow =
        wasi_filesystem_types_borrow_descriptor(file);

    /* Try to write to read-only file */
    imports_list_u8_t write_buf;
    write_buf.ptr = (uint8_t *)"test data";
    write_buf.len = 9;

    uint64_t written;
    bool write_success = wasi_filesystem_types_method_descriptor_write(
        file_borrow, &write_buf, 0, &written, &err);

    TEST_ASSERT(!write_success, "writing to read-only file should fail");
    TEST_ASSERT(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR,
                "expected BAD_DESCRIPTOR error code");

    /* Cleanup */
    wasi_filesystem_types_descriptor_drop_own(file);
    wasi_filesystem_types_method_descriptor_unlink_file_at(dir, &path, &err);

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("write_readonly_file");
}

/* Test: Read at offset past end of file */
void test_read_past_eof(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    /* Create a test file */
    imports_string_t path;
    path.ptr = (uint8_t *)"test_read_eof.txt";
    path.len = strlen((char *)path.ptr);

    wasi_filesystem_types_own_descriptor_t file;
    wasi_filesystem_types_open_flags_t open_flags =
        WASI_FILESYSTEM_TYPES_OPEN_FLAGS_CREATE |
        WASI_FILESYSTEM_TYPES_OPEN_FLAGS_TRUNCATE;
    wasi_filesystem_types_descriptor_flags_t desc_flags =
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_READ |
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_WRITE;

    wasi_filesystem_types_error_code_t err;
    bool opened = wasi_filesystem_types_method_descriptor_open_at(
        dir, 0, &path, open_flags, desc_flags, &file, &err);

    TEST_ASSERT(opened, "failed to create test file");

    wasi_filesystem_types_borrow_descriptor_t file_borrow =
        wasi_filesystem_types_borrow_descriptor(file);

    /* Write some data */
    imports_list_u8_t write_buf;
    write_buf.ptr = (uint8_t *)"0123456789";
    write_buf.len = 10;

    uint64_t written;
    wasi_filesystem_types_method_descriptor_write(file_borrow, &write_buf, 0, &written, &err);

    /* Read past end of file - should return empty */
    imports_tuple2_list_u8_bool_t read_result;
    bool read_success = wasi_filesystem_types_method_descriptor_read(
        file_borrow, 100, 1000, &read_result, &err);

    TEST_ASSERT(read_success, "read past EOF should succeed with empty result");
    TEST_ASSERT(read_result.f0.len == 0, "read past EOF should return empty buffer");

    /* Free result if needed */
    if (read_result.f0.ptr != NULL) {
        imports_list_u8_free(&read_result.f0);
    }

    /* Cleanup */
    wasi_filesystem_types_descriptor_drop_own(file);
    wasi_filesystem_types_method_descriptor_unlink_file_at(dir, &path, &err);

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("read_past_eof");
}

/* Test: Try to create directory that already exists */
void test_mkdir_existing(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    imports_string_t dirname;
    dirname.ptr = (uint8_t *)"test_mkdir_existing";
    dirname.len = strlen((char *)dirname.ptr);

    wasi_filesystem_types_error_code_t err;

    /* Create directory first time - should succeed */
    wasi_filesystem_types_method_descriptor_create_directory_at(dir, &dirname, &err);

    /* Create directory second time - should fail with EXIST */
    bool second = wasi_filesystem_types_method_descriptor_create_directory_at(
        dir, &dirname, &err);

    TEST_ASSERT(!second, "creating existing directory should fail");
    TEST_ASSERT(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_EXIST,
                "expected EXIST error code");

    /* Cleanup */
    wasi_filesystem_types_method_descriptor_remove_directory_at(dir, &dirname, &err);

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("mkdir_existing");
}

/* Test: Truncate file to larger size */
void test_truncate_extend(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    imports_string_t path;
    path.ptr = (uint8_t *)"test_truncate_extend.txt";
    path.len = strlen((char *)path.ptr);

    wasi_filesystem_types_own_descriptor_t file;
    wasi_filesystem_types_open_flags_t open_flags =
        WASI_FILESYSTEM_TYPES_OPEN_FLAGS_CREATE |
        WASI_FILESYSTEM_TYPES_OPEN_FLAGS_TRUNCATE;
    wasi_filesystem_types_descriptor_flags_t desc_flags =
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_READ |
        WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_WRITE;

    wasi_filesystem_types_error_code_t err;
    bool opened = wasi_filesystem_types_method_descriptor_open_at(
        dir, 0, &path, open_flags, desc_flags, &file, &err);

    TEST_ASSERT(opened, "failed to create test file");

    wasi_filesystem_types_borrow_descriptor_t file_borrow =
        wasi_filesystem_types_borrow_descriptor(file);

    /* Write some data */
    imports_list_u8_t write_buf;
    write_buf.ptr = (uint8_t *)"hello";
    write_buf.len = 5;

    uint64_t written;
    wasi_filesystem_types_method_descriptor_write(file_borrow, &write_buf, 0, &written, &err);

    /* Truncate to larger size */
    bool truncate_success = wasi_filesystem_types_method_descriptor_set_size(
        file_borrow, 100, &err);

    TEST_ASSERT(truncate_success, "extending file via truncate should succeed");

    /* Verify new size */
    wasi_filesystem_types_descriptor_stat_t stat_result;
    bool stat_success = wasi_filesystem_types_method_descriptor_stat(
        file_borrow, &stat_result, &err);

    TEST_ASSERT(stat_success, "stat after truncate should succeed");
    TEST_ASSERT(stat_result.size == 100, "file should be extended to 100 bytes");

    /* Cleanup */
    wasi_filesystem_types_descriptor_drop_own(file);
    wasi_filesystem_types_method_descriptor_unlink_file_at(dir, &path, &err);

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("truncate_extend");
}

/* Test: Rename non-existent file should fail */
void test_rename_nonexistent(void) {
    wasi_filesystem_types_own_descriptor_t preopen;
    if (!get_preopen_dir(&preopen)) {
        printf("    SKIP: no preopened directories\n");
        return;
    }

    wasi_filesystem_types_borrow_descriptor_t dir =
        wasi_filesystem_types_borrow_descriptor(preopen);

    imports_string_t src_path;
    src_path.ptr = (uint8_t *)"nonexistent_source_file.txt";
    src_path.len = strlen((char *)src_path.ptr);

    imports_string_t dst_path;
    dst_path.ptr = (uint8_t *)"destination_file.txt";
    dst_path.len = strlen((char *)dst_path.ptr);

    wasi_filesystem_types_error_code_t err;
    bool rename_success = wasi_filesystem_types_method_descriptor_rename_at(
        dir, &src_path, dir, &dst_path, &err);

    TEST_ASSERT(!rename_success, "renaming non-existent file should fail");
    TEST_ASSERT(err == WASI_FILESYSTEM_TYPES_ERROR_CODE_NO_ENTRY,
                "expected NO_ENTRY error code");

    wasi_filesystem_types_descriptor_drop_own(preopen);
    TEST_PASS("rename_nonexistent");
}

/* ============================================================================
 * Run All Error Tests
 * ============================================================================ */

void run_error_tests(void) {
    printf("Running WASI error case tests...\n");

    test_open_nonexistent_file();
    test_create_in_nonexistent_dir();
    test_stat_nonexistent();
    test_remove_nonexistent_dir();
    test_unlink_nonexistent();
    test_remove_nonempty_dir();
    test_read_writeonly_file();
    test_write_readonly_file();
    test_read_past_eof();
    test_mkdir_existing();
    test_truncate_extend();
    test_rename_nonexistent();

    printf("\nError tests passed: %d\n", tests_passed);
    printf("Error tests failed: %d\n", tests_failed);
}

int get_error_tests_passed(void) {
    return tests_passed;
}

int get_error_tests_failed(void) {
    return tests_failed;
}

#ifndef TEST_RUNNER_MODE
int main(void) {
    run_error_tests();
    return tests_failed > 0 ? 1 : 0;
}
#endif
