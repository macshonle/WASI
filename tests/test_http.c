/**
 * Unit tests for WASI HTTP implementation
 *
 * Tests the wasi:http/types interfaces, starting with Fields resource.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../build/c-bindings/http/proxy.h"

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

/* Forward declaration for test runner */
int run_http_tests(void);

/* Test macros */
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    test_##name(); \
    printf("  %s: PASS\n", #name); \
    tests_passed++; \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("    ASSERT_TRUE failed: %s\n", #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(cond) do { \
    if (cond) { \
        printf("    ASSERT_FALSE failed: %s\n", #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("    ASSERT_EQ failed: %s != %s\n", #a, #b); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Fields Resource Tests
 * ============================================================================
 */

TEST(fields_constructor) {
    /* Test creating empty fields */
    wasi_http_types_own_fields_t fields = wasi_http_types_constructor_fields();
    ASSERT_TRUE(fields.__handle > 0);

    /* Verify it's empty */
    wasi_http_types_borrow_fields_t borrow = wasi_http_types_borrow_fields(fields);
    proxy_list_tuple2_field_key_field_value_t entries;
    wasi_http_types_method_fields_entries(borrow, &entries);
    ASSERT_EQ(entries.len, 0);

    /* Cleanup */
    wasi_http_types_fields_drop_own(fields);
}

TEST(fields_from_list) {
    /* Create entries list */
    proxy_tuple2_field_key_field_value_t input_entries[2];

    /* Entry 1: Content-Type: text/plain */
    input_entries[0].f0.ptr = (uint8_t *)(uintptr_t)"Content-Type";
    input_entries[0].f0.len = 12;
    input_entries[0].f1.ptr = (uint8_t *)(uintptr_t)"text/plain";
    input_entries[0].f1.len = 10;

    /* Entry 2: X-Custom: value */
    input_entries[1].f0.ptr = (uint8_t *)(uintptr_t)"X-Custom";
    input_entries[1].f0.len = 8;
    input_entries[1].f1.ptr = (uint8_t *)(uintptr_t)"value";
    input_entries[1].f1.len = 5;

    proxy_list_tuple2_field_key_field_value_t list = {
        .ptr = input_entries,
        .len = 2
    };

    wasi_http_types_own_fields_t fields;
    wasi_http_types_header_error_t err;
    bool ok = wasi_http_types_static_fields_from_list(&list, &fields, &err);

    ASSERT_TRUE(ok);
    ASSERT_TRUE(fields.__handle > 0);

    /* Verify entries */
    wasi_http_types_borrow_fields_t borrow = wasi_http_types_borrow_fields(fields);
    proxy_list_tuple2_field_key_field_value_t entries;
    wasi_http_types_method_fields_entries(borrow, &entries);
    ASSERT_EQ(entries.len, 2);

    /* Free returned entries */
    proxy_list_tuple2_field_key_field_value_free(&entries);

    /* Cleanup */
    wasi_http_types_fields_drop_own(fields);
}

TEST(fields_get_set) {
    /* Create empty fields */
    wasi_http_types_own_fields_t fields = wasi_http_types_constructor_fields();
    wasi_http_types_borrow_fields_t borrow = wasi_http_types_borrow_fields(fields);

    /* Append a header */
    wasi_http_types_field_key_t name = { .ptr = (uint8_t *)(uintptr_t)"Content-Type", .len = 12 };
    wasi_http_types_field_value_t value = { .ptr = (uint8_t *)(uintptr_t)"text/html", .len = 9 };
    wasi_http_types_header_error_t err;

    bool ok = wasi_http_types_method_fields_append(borrow, &name, &value, &err);
    ASSERT_TRUE(ok);

    /* Get the value back */
    proxy_list_field_value_t values;
    wasi_http_types_method_fields_get(borrow, &name, &values);
    ASSERT_EQ(values.len, 1);
    ASSERT_EQ(values.ptr[0].len, 9);
    ASSERT_TRUE(memcmp(values.ptr[0].ptr, "text/html", 9) == 0);

    /* Free returned values */
    proxy_list_field_value_free(&values);

    /* Cleanup */
    wasi_http_types_fields_drop_own(fields);
}

TEST(fields_case_insensitivity) {
    /* Create fields with a header */
    wasi_http_types_own_fields_t fields = wasi_http_types_constructor_fields();
    wasi_http_types_borrow_fields_t borrow = wasi_http_types_borrow_fields(fields);

    wasi_http_types_field_key_t name = { .ptr = (uint8_t *)(uintptr_t)"Content-Type", .len = 12 };
    wasi_http_types_field_value_t value = { .ptr = (uint8_t *)(uintptr_t)"text/plain", .len = 10 };
    wasi_http_types_header_error_t err;

    wasi_http_types_method_fields_append(borrow, &name, &value, &err);

    /* Check with different cases */
    wasi_http_types_field_key_t name_lower = { .ptr = (uint8_t *)(uintptr_t)"content-type", .len = 12 };
    wasi_http_types_field_key_t name_upper = { .ptr = (uint8_t *)(uintptr_t)"CONTENT-TYPE", .len = 12 };
    wasi_http_types_field_key_t name_mixed = { .ptr = (uint8_t *)(uintptr_t)"CoNtEnT-TyPe", .len = 12 };

    ASSERT_TRUE(wasi_http_types_method_fields_has(borrow, &name_lower));
    ASSERT_TRUE(wasi_http_types_method_fields_has(borrow, &name_upper));
    ASSERT_TRUE(wasi_http_types_method_fields_has(borrow, &name_mixed));

    /* Get with different case should work */
    proxy_list_field_value_t values;
    wasi_http_types_method_fields_get(borrow, &name_upper, &values);
    ASSERT_EQ(values.len, 1);

    proxy_list_field_value_free(&values);
    wasi_http_types_fields_drop_own(fields);
}

TEST(fields_multiple_values) {
    /* Create fields */
    wasi_http_types_own_fields_t fields = wasi_http_types_constructor_fields();
    wasi_http_types_borrow_fields_t borrow = wasi_http_types_borrow_fields(fields);

    wasi_http_types_field_key_t name = { .ptr = (uint8_t *)(uintptr_t)"Accept", .len = 6 };
    wasi_http_types_header_error_t err;

    /* Append multiple values for same header */
    wasi_http_types_field_value_t value1 = { .ptr = (uint8_t *)(uintptr_t)"text/html", .len = 9 };
    wasi_http_types_field_value_t value2 = { .ptr = (uint8_t *)(uintptr_t)"text/plain", .len = 10 };
    wasi_http_types_field_value_t value3 = { .ptr = (uint8_t *)(uintptr_t)"*/*", .len = 3 };

    wasi_http_types_method_fields_append(borrow, &name, &value1, &err);
    wasi_http_types_method_fields_append(borrow, &name, &value2, &err);
    wasi_http_types_method_fields_append(borrow, &name, &value3, &err);

    /* Should get all three values */
    proxy_list_field_value_t values;
    wasi_http_types_method_fields_get(borrow, &name, &values);
    ASSERT_EQ(values.len, 3);

    proxy_list_field_value_free(&values);
    wasi_http_types_fields_drop_own(fields);
}

TEST(fields_delete) {
    /* Create fields with headers */
    wasi_http_types_own_fields_t fields = wasi_http_types_constructor_fields();
    wasi_http_types_borrow_fields_t borrow = wasi_http_types_borrow_fields(fields);
    wasi_http_types_header_error_t err;

    wasi_http_types_field_key_t name1 = { .ptr = (uint8_t *)(uintptr_t)"Content-Type", .len = 12 };
    wasi_http_types_field_value_t value1 = { .ptr = (uint8_t *)(uintptr_t)"text/html", .len = 9 };
    wasi_http_types_method_fields_append(borrow, &name1, &value1, &err);

    wasi_http_types_field_key_t name2 = { .ptr = (uint8_t *)(uintptr_t)"X-Custom", .len = 8 };
    wasi_http_types_field_value_t value2 = { .ptr = (uint8_t *)(uintptr_t)"value", .len = 5 };
    wasi_http_types_method_fields_append(borrow, &name2, &value2, &err);

    /* Verify both exist */
    ASSERT_TRUE(wasi_http_types_method_fields_has(borrow, &name1));
    ASSERT_TRUE(wasi_http_types_method_fields_has(borrow, &name2));

    /* Delete Content-Type */
    wasi_http_types_method_fields_delete(borrow, &name1, &err);

    /* Verify Content-Type is gone, X-Custom remains */
    ASSERT_FALSE(wasi_http_types_method_fields_has(borrow, &name1));
    ASSERT_TRUE(wasi_http_types_method_fields_has(borrow, &name2));

    wasi_http_types_fields_drop_own(fields);
}

TEST(fields_entries) {
    /* Create fields with multiple headers */
    wasi_http_types_own_fields_t fields = wasi_http_types_constructor_fields();
    wasi_http_types_borrow_fields_t borrow = wasi_http_types_borrow_fields(fields);
    wasi_http_types_header_error_t err;

    wasi_http_types_field_key_t name1 = { .ptr = (uint8_t *)(uintptr_t)"Content-Type", .len = 12 };
    wasi_http_types_field_value_t value1 = { .ptr = (uint8_t *)(uintptr_t)"text/html", .len = 9 };
    wasi_http_types_method_fields_append(borrow, &name1, &value1, &err);

    wasi_http_types_field_key_t name2 = { .ptr = (uint8_t *)(uintptr_t)"Content-Length", .len = 14 };
    wasi_http_types_field_value_t value2 = { .ptr = (uint8_t *)(uintptr_t)"1234", .len = 4 };
    wasi_http_types_method_fields_append(borrow, &name2, &value2, &err);

    /* Get all entries */
    proxy_list_tuple2_field_key_field_value_t entries;
    wasi_http_types_method_fields_entries(borrow, &entries);

    ASSERT_EQ(entries.len, 2);

    proxy_list_tuple2_field_key_field_value_free(&entries);
    wasi_http_types_fields_drop_own(fields);
}

TEST(fields_clone) {
    /* Create fields with a header */
    wasi_http_types_own_fields_t fields = wasi_http_types_constructor_fields();
    wasi_http_types_borrow_fields_t borrow = wasi_http_types_borrow_fields(fields);
    wasi_http_types_header_error_t err;

    wasi_http_types_field_key_t name = { .ptr = (uint8_t *)(uintptr_t)"X-Original", .len = 10 };
    wasi_http_types_field_value_t value = { .ptr = (uint8_t *)(uintptr_t)"original", .len = 8 };
    wasi_http_types_method_fields_append(borrow, &name, &value, &err);

    /* Clone */
    wasi_http_types_own_fields_t cloned = wasi_http_types_method_fields_clone(borrow);
    ASSERT_TRUE(cloned.__handle > 0);
    ASSERT_TRUE(cloned.__handle != fields.__handle);

    /* Verify clone has the same header */
    wasi_http_types_borrow_fields_t clone_borrow = wasi_http_types_borrow_fields(cloned);
    ASSERT_TRUE(wasi_http_types_method_fields_has(clone_borrow, &name));

    /* Modify original */
    wasi_http_types_field_key_t name2 = { .ptr = (uint8_t *)(uintptr_t)"X-New", .len = 5 };
    wasi_http_types_field_value_t value2 = { .ptr = (uint8_t *)(uintptr_t)"new", .len = 3 };
    wasi_http_types_method_fields_append(borrow, &name2, &value2, &err);

    /* Clone should NOT have the new header */
    ASSERT_FALSE(wasi_http_types_method_fields_has(clone_borrow, &name2));

    wasi_http_types_fields_drop_own(fields);
    wasi_http_types_fields_drop_own(cloned);
}

TEST(fields_invalid_name) {
    /* Test that invalid field names are rejected */
    proxy_tuple2_field_key_field_value_t input_entries[1];

    /* Invalid name with space */
    input_entries[0].f0.ptr = (uint8_t *)(uintptr_t)"Invalid Name";
    input_entries[0].f0.len = 12;
    input_entries[0].f1.ptr = (uint8_t *)(uintptr_t)"value";
    input_entries[0].f1.len = 5;

    proxy_list_tuple2_field_key_field_value_t list = {
        .ptr = input_entries,
        .len = 1
    };

    wasi_http_types_own_fields_t fields;
    wasi_http_types_header_error_t err;
    bool ok = wasi_http_types_static_fields_from_list(&list, &fields, &err);

    ASSERT_FALSE(ok);
    ASSERT_EQ(err.tag, WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX);
}

TEST(fields_forbidden_name) {
    /* Test that pseudo-headers are rejected */
    proxy_tuple2_field_key_field_value_t input_entries[1];

    /* Pseudo-header starting with : */
    input_entries[0].f0.ptr = (uint8_t *)(uintptr_t)":path";
    input_entries[0].f0.len = 5;
    input_entries[0].f1.ptr = (uint8_t *)(uintptr_t)"/";
    input_entries[0].f1.len = 1;

    proxy_list_tuple2_field_key_field_value_t list = {
        .ptr = input_entries,
        .len = 1
    };

    wasi_http_types_own_fields_t fields;
    wasi_http_types_header_error_t err;
    bool ok = wasi_http_types_static_fields_from_list(&list, &fields, &err);

    ASSERT_FALSE(ok);
    ASSERT_EQ(err.tag, WASI_HTTP_TYPES_HEADER_ERROR_FORBIDDEN);
}

/* ============================================================================
 * Outgoing Request Tests
 * ============================================================================
 */

TEST(outgoing_request_constructor) {
    /* Create headers first */
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    ASSERT_TRUE(headers.__handle > 0);

    /* Create outgoing request with headers */
    wasi_http_types_own_outgoing_request_t request =
        wasi_http_types_constructor_outgoing_request(headers);
    ASSERT_TRUE(request.__handle > 0);

    /* Cleanup */
    wasi_http_types_outgoing_request_drop_own(request);
}

TEST(outgoing_request_method) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_request_t request =
        wasi_http_types_constructor_outgoing_request(headers);
    wasi_http_types_borrow_outgoing_request_t borrow =
        wasi_http_types_borrow_outgoing_request(request);

    /* Default method should be GET */
    wasi_http_types_method_t method;
    wasi_http_types_method_outgoing_request_method(borrow, &method);
    ASSERT_EQ(method.tag, WASI_HTTP_TYPES_METHOD_GET);

    /* Set to POST */
    wasi_http_types_method_t post_method = { .tag = WASI_HTTP_TYPES_METHOD_POST };
    ASSERT_TRUE(wasi_http_types_method_outgoing_request_set_method(borrow, &post_method));

    /* Verify POST */
    wasi_http_types_method_outgoing_request_method(borrow, &method);
    ASSERT_EQ(method.tag, WASI_HTTP_TYPES_METHOD_POST);

    wasi_http_types_outgoing_request_drop_own(request);
}

TEST(outgoing_request_path) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_request_t request =
        wasi_http_types_constructor_outgoing_request(headers);
    wasi_http_types_borrow_outgoing_request_t borrow =
        wasi_http_types_borrow_outgoing_request(request);

    /* Initially no path */
    proxy_string_t path;
    ASSERT_FALSE(wasi_http_types_method_outgoing_request_path_with_query(borrow, &path));

    /* Set path */
    proxy_string_t new_path = { .ptr = (uint8_t *)(uintptr_t)"/api/test?foo=bar", .len = 17 };
    ASSERT_TRUE(wasi_http_types_method_outgoing_request_set_path_with_query(borrow, &new_path));

    /* Get path back */
    ASSERT_TRUE(wasi_http_types_method_outgoing_request_path_with_query(borrow, &path));
    ASSERT_EQ(path.len, 17);
    ASSERT_TRUE(memcmp(path.ptr, "/api/test?foo=bar", 17) == 0);

    free(path.ptr);
    wasi_http_types_outgoing_request_drop_own(request);
}

TEST(outgoing_request_authority) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_request_t request =
        wasi_http_types_constructor_outgoing_request(headers);
    wasi_http_types_borrow_outgoing_request_t borrow =
        wasi_http_types_borrow_outgoing_request(request);

    /* Set authority */
    proxy_string_t authority = { .ptr = (uint8_t *)(uintptr_t)"example.com:8080", .len = 16 };
    ASSERT_TRUE(wasi_http_types_method_outgoing_request_set_authority(borrow, &authority));

    /* Get authority back */
    proxy_string_t result;
    ASSERT_TRUE(wasi_http_types_method_outgoing_request_authority(borrow, &result));
    ASSERT_EQ(result.len, 16);
    ASSERT_TRUE(memcmp(result.ptr, "example.com:8080", 16) == 0);

    free(result.ptr);
    wasi_http_types_outgoing_request_drop_own(request);
}

TEST(outgoing_request_scheme) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_request_t request =
        wasi_http_types_constructor_outgoing_request(headers);
    wasi_http_types_borrow_outgoing_request_t borrow =
        wasi_http_types_borrow_outgoing_request(request);

    /* Set to HTTPS */
    wasi_http_types_scheme_t https_scheme = { .tag = WASI_HTTP_TYPES_SCHEME_HTTPS };
    ASSERT_TRUE(wasi_http_types_method_outgoing_request_set_scheme(borrow, &https_scheme));

    /* Get scheme back */
    wasi_http_types_scheme_t scheme;
    ASSERT_TRUE(wasi_http_types_method_outgoing_request_scheme(borrow, &scheme));
    ASSERT_EQ(scheme.tag, WASI_HTTP_TYPES_SCHEME_HTTPS);

    wasi_http_types_outgoing_request_drop_own(request);
}

/* ============================================================================
 * Request Options Tests
 * ============================================================================
 */

TEST(request_options_constructor) {
    wasi_http_types_own_request_options_t options =
        wasi_http_types_constructor_request_options();
    ASSERT_TRUE(options.__handle > 0);

    wasi_http_types_request_options_drop_own(options);
}

TEST(request_options_timeouts) {
    wasi_http_types_own_request_options_t options =
        wasi_http_types_constructor_request_options();
    wasi_http_types_borrow_request_options_t borrow =
        wasi_http_types_borrow_request_options(options);

    /* Initially no timeouts set */
    wasi_http_types_duration_t timeout;
    ASSERT_FALSE(wasi_http_types_method_request_options_connect_timeout(borrow, &timeout));
    ASSERT_FALSE(wasi_http_types_method_request_options_first_byte_timeout(borrow, &timeout));
    ASSERT_FALSE(wasi_http_types_method_request_options_between_bytes_timeout(borrow, &timeout));

    /* Set connect timeout (5 seconds in nanoseconds) */
    wasi_http_types_duration_t connect_timeout = 5000000000ULL;
    ASSERT_TRUE(wasi_http_types_method_request_options_set_connect_timeout(borrow, &connect_timeout));

    /* Get it back */
    ASSERT_TRUE(wasi_http_types_method_request_options_connect_timeout(borrow, &timeout));
    ASSERT_EQ(timeout, 5000000000ULL);

    /* Set first byte timeout */
    wasi_http_types_duration_t first_byte_timeout = 10000000000ULL;
    ASSERT_TRUE(wasi_http_types_method_request_options_set_first_byte_timeout(borrow, &first_byte_timeout));
    ASSERT_TRUE(wasi_http_types_method_request_options_first_byte_timeout(borrow, &timeout));
    ASSERT_EQ(timeout, 10000000000ULL);

    /* Set between bytes timeout */
    wasi_http_types_duration_t between_bytes_timeout = 1000000000ULL;
    ASSERT_TRUE(wasi_http_types_method_request_options_set_between_bytes_timeout(borrow, &between_bytes_timeout));
    ASSERT_TRUE(wasi_http_types_method_request_options_between_bytes_timeout(borrow, &timeout));
    ASSERT_EQ(timeout, 1000000000ULL);

    wasi_http_types_request_options_drop_own(options);
}

/* ============================================================================
 * Outgoing Response Tests
 * ============================================================================
 */

TEST(outgoing_response_constructor) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_response_t response =
        wasi_http_types_constructor_outgoing_response(headers);
    ASSERT_TRUE(response.__handle > 0);

    wasi_http_types_outgoing_response_drop_own(response);
}

TEST(outgoing_response_status_code) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_response_t response =
        wasi_http_types_constructor_outgoing_response(headers);
    wasi_http_types_borrow_outgoing_response_t borrow =
        wasi_http_types_borrow_outgoing_response(response);

    /* Default status should be 200 */
    wasi_http_types_status_code_t status =
        wasi_http_types_method_outgoing_response_status_code(borrow);
    ASSERT_EQ(status, 200);

    /* Set to 404 */
    ASSERT_TRUE(wasi_http_types_method_outgoing_response_set_status_code(borrow, 404));
    status = wasi_http_types_method_outgoing_response_status_code(borrow);
    ASSERT_EQ(status, 404);

    /* Set to 201 */
    ASSERT_TRUE(wasi_http_types_method_outgoing_response_set_status_code(borrow, 201));
    status = wasi_http_types_method_outgoing_response_status_code(borrow);
    ASSERT_EQ(status, 201);

    wasi_http_types_outgoing_response_drop_own(response);
}

TEST(outgoing_response_invalid_status) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_response_t response =
        wasi_http_types_constructor_outgoing_response(headers);
    wasi_http_types_borrow_outgoing_response_t borrow =
        wasi_http_types_borrow_outgoing_response(response);

    /* Status below 100 should fail */
    ASSERT_FALSE(wasi_http_types_method_outgoing_response_set_status_code(borrow, 99));

    /* Status above 999 should fail */
    ASSERT_FALSE(wasi_http_types_method_outgoing_response_set_status_code(borrow, 1000));

    /* Status should remain at default */
    wasi_http_types_status_code_t status =
        wasi_http_types_method_outgoing_response_status_code(borrow);
    ASSERT_EQ(status, 200);

    wasi_http_types_outgoing_response_drop_own(response);
}

/* ============================================================================
 * Outgoing Body Tests (Phase 3)
 * ============================================================================
 */

TEST(outgoing_body_single_retrieval) {
    /* Create a request with body */
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_request_t request =
        wasi_http_types_constructor_outgoing_request(headers);
    wasi_http_types_borrow_outgoing_request_t borrow =
        wasi_http_types_borrow_outgoing_request(request);

    /* Get body - should succeed first time */
    wasi_http_types_own_outgoing_body_t body;
    ASSERT_TRUE(wasi_http_types_method_outgoing_request_body(borrow, &body));

    /* Get body again - should fail (single retrieval rule) */
    wasi_http_types_own_outgoing_body_t body2;
    ASSERT_FALSE(wasi_http_types_method_outgoing_request_body(borrow, &body2));

    wasi_http_types_outgoing_request_drop_own(request);
}

TEST(outgoing_body_write_stream) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_request_t request =
        wasi_http_types_constructor_outgoing_request(headers);
    wasi_http_types_borrow_outgoing_request_t req_borrow =
        wasi_http_types_borrow_outgoing_request(request);

    wasi_http_types_own_outgoing_body_t body;
    ASSERT_TRUE(wasi_http_types_method_outgoing_request_body(req_borrow, &body));

    wasi_http_types_borrow_outgoing_body_t body_borrow =
        wasi_http_types_borrow_outgoing_body(body);

    /* Get write stream - should succeed first time */
    wasi_http_types_own_output_stream_t stream;
    ASSERT_TRUE(wasi_http_types_method_outgoing_body_write(body_borrow, &stream));

    /* Get write stream again - should fail */
    wasi_http_types_own_output_stream_t stream2;
    ASSERT_FALSE(wasi_http_types_method_outgoing_body_write(body_borrow, &stream2));

    wasi_http_types_outgoing_request_drop_own(request);
}

TEST(outgoing_body_finish) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_request_t request =
        wasi_http_types_constructor_outgoing_request(headers);
    wasi_http_types_borrow_outgoing_request_t req_borrow =
        wasi_http_types_borrow_outgoing_request(request);

    wasi_http_types_own_outgoing_body_t body;
    ASSERT_TRUE(wasi_http_types_method_outgoing_request_body(req_borrow, &body));

    /* Finish body without trailers */
    wasi_http_types_error_code_t err;
    ASSERT_TRUE(wasi_http_types_static_outgoing_body_finish(body, NULL, &err));

    wasi_http_types_outgoing_request_drop_own(request);
}

/* ============================================================================
 * Request Options Extended Tests (Phase 3)
 * ============================================================================
 */

TEST(request_options_clear_timeout) {
    wasi_http_types_own_request_options_t options =
        wasi_http_types_constructor_request_options();
    wasi_http_types_borrow_request_options_t borrow =
        wasi_http_types_borrow_request_options(options);

    /* Set connect timeout */
    wasi_http_types_duration_t timeout = 5000000000ULL;
    ASSERT_TRUE(wasi_http_types_method_request_options_set_connect_timeout(borrow, &timeout));

    /* Verify it's set */
    wasi_http_types_duration_t result;
    ASSERT_TRUE(wasi_http_types_method_request_options_connect_timeout(borrow, &result));

    /* Clear it by passing NULL */
    ASSERT_TRUE(wasi_http_types_method_request_options_set_connect_timeout(borrow, NULL));

    /* Verify it's cleared */
    ASSERT_FALSE(wasi_http_types_method_request_options_connect_timeout(borrow, &result));

    wasi_http_types_request_options_drop_own(options);
}

/* ============================================================================
 * Future Response Tests (Phase 3)
 * ============================================================================
 */

TEST(future_response_subscribe) {
    /* Test the future response subscription API */
    wasi_http_types_own_future_incoming_response_t future = { .__handle = 0 };
    wasi_http_types_borrow_future_incoming_response_t borrow =
        wasi_http_types_borrow_future_incoming_response(future);

    /* Subscribe returns a pollable */
    wasi_http_types_own_pollable_t pollable =
        wasi_http_types_method_future_incoming_response_subscribe(borrow);
    /* Placeholder pollable should be valid */
    ASSERT_TRUE(pollable.__handle >= 0);
}

/* ============================================================================
 * Incoming Request Tests (Phase 4)
 * ============================================================================
 */

/* External helper from http.c for creating incoming requests */
extern wasi_http_types_own_incoming_request_t http_create_incoming_request(
    wasi_http_types_method_t method,
    const char *path,
    const char *authority,
    wasi_http_types_own_fields_t headers
);

/* External helper from http.c for creating response outparam */
extern wasi_http_types_own_response_outparam_t http_create_response_outparam(void);

/* External helper to check if response was set */
extern bool http_response_outparam_is_set(wasi_http_types_borrow_response_outparam_t outparam);

/* External helper to get response from outparam */
extern bool http_response_outparam_get_response(
    wasi_http_types_borrow_response_outparam_t outparam,
    wasi_http_types_own_outgoing_response_t *response
);

TEST(incoming_request_create) {
    /* Create headers */
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();

    /* Create incoming request */
    wasi_http_types_method_t method = { .tag = WASI_HTTP_TYPES_METHOD_POST };
    wasi_http_types_own_incoming_request_t request =
        http_create_incoming_request(method, "/api/data", "example.com:8080", headers);
    ASSERT_TRUE(request.__handle > 0);

    wasi_http_types_incoming_request_drop_own(request);
}

TEST(incoming_request_method) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_method_t method = { .tag = WASI_HTTP_TYPES_METHOD_DELETE };
    wasi_http_types_own_incoming_request_t request =
        http_create_incoming_request(method, "/resource/123", "api.example.com", headers);

    wasi_http_types_borrow_incoming_request_t borrow =
        wasi_http_types_borrow_incoming_request(request);

    /* Get method */
    wasi_http_types_method_t result;
    wasi_http_types_method_incoming_request_method(borrow, &result);
    ASSERT_EQ(result.tag, WASI_HTTP_TYPES_METHOD_DELETE);

    wasi_http_types_incoming_request_drop_own(request);
}

TEST(incoming_request_path) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_method_t method = { .tag = WASI_HTTP_TYPES_METHOD_GET };
    wasi_http_types_own_incoming_request_t request =
        http_create_incoming_request(method, "/users?page=1&limit=10", "example.com", headers);

    wasi_http_types_borrow_incoming_request_t borrow =
        wasi_http_types_borrow_incoming_request(request);

    /* Get path */
    proxy_string_t path;
    ASSERT_TRUE(wasi_http_types_method_incoming_request_path_with_query(borrow, &path));
    ASSERT_EQ(path.len, 22);
    ASSERT_TRUE(memcmp(path.ptr, "/users?page=1&limit=10", 22) == 0);

    free(path.ptr);
    wasi_http_types_incoming_request_drop_own(request);
}

TEST(incoming_request_authority) {
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_method_t method = { .tag = WASI_HTTP_TYPES_METHOD_GET };
    wasi_http_types_own_incoming_request_t request =
        http_create_incoming_request(method, "/", "api.example.com:443", headers);

    wasi_http_types_borrow_incoming_request_t borrow =
        wasi_http_types_borrow_incoming_request(request);

    /* Get authority */
    proxy_string_t authority;
    ASSERT_TRUE(wasi_http_types_method_incoming_request_authority(borrow, &authority));
    ASSERT_EQ(authority.len, 19);
    ASSERT_TRUE(memcmp(authority.ptr, "api.example.com:443", 19) == 0);

    free(authority.ptr);
    wasi_http_types_incoming_request_drop_own(request);
}

TEST(incoming_request_headers) {
    /* Create headers with some values */
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_borrow_fields_t headers_borrow = wasi_http_types_borrow_fields(headers);

    proxy_string_t name = { .ptr = (uint8_t *)(uintptr_t)"content-type", .len = 12 };
    wasi_http_types_field_value_t value = { .ptr = (uint8_t *)(uintptr_t)"application/json", .len = 16 };
    proxy_list_field_value_t values = { .ptr = &value, .len = 1 };
    wasi_http_types_header_error_t err;
    wasi_http_types_method_fields_set(headers_borrow, &name, &values, &err);

    /* Create request */
    wasi_http_types_method_t method = { .tag = WASI_HTTP_TYPES_METHOD_POST };
    wasi_http_types_own_incoming_request_t request =
        http_create_incoming_request(method, "/api", "example.com", headers);

    wasi_http_types_borrow_incoming_request_t borrow =
        wasi_http_types_borrow_incoming_request(request);

    /* Get headers from request */
    wasi_http_types_own_headers_t req_headers =
        wasi_http_types_method_incoming_request_headers(borrow);
    ASSERT_TRUE(req_headers.__handle > 0);

    wasi_http_types_incoming_request_drop_own(request);
}

TEST(response_outparam_create) {
    wasi_http_types_own_response_outparam_t outparam = http_create_response_outparam();
    ASSERT_TRUE(outparam.__handle > 0);

    /* Initially not set */
    wasi_http_types_borrow_response_outparam_t borrow =
        wasi_http_types_borrow_response_outparam(outparam);
    ASSERT_FALSE(http_response_outparam_is_set(borrow));

    wasi_http_types_response_outparam_drop_own(outparam);
}

TEST(response_outparam_set) {
    wasi_http_types_own_response_outparam_t outparam = http_create_response_outparam();

    /* Create a response */
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_response_t response =
        wasi_http_types_constructor_outgoing_response(headers);

    /* Set status code */
    wasi_http_types_borrow_outgoing_response_t response_borrow =
        wasi_http_types_borrow_outgoing_response(response);
    wasi_http_types_method_outgoing_response_set_status_code(response_borrow, 200);

    /* Set response on outparam */
    wasi_http_types_result_own_outgoing_response_error_code_t result;
    result.is_err = false;
    result.val.ok = response;
    wasi_http_types_static_response_outparam_set(outparam, &result);

    /* Verify it's set */
    wasi_http_types_borrow_response_outparam_t borrow =
        wasi_http_types_borrow_response_outparam(outparam);
    ASSERT_TRUE(http_response_outparam_is_set(borrow));

    /* Get response back */
    wasi_http_types_own_outgoing_response_t got_response;
    ASSERT_TRUE(http_response_outparam_get_response(borrow, &got_response));

    wasi_http_types_response_outparam_drop_own(outparam);
}

TEST(incoming_handler_default) {
    /* Test the default incoming handler returns 501 */
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_method_t method = { .tag = WASI_HTTP_TYPES_METHOD_GET };
    wasi_http_types_own_incoming_request_t request =
        http_create_incoming_request(method, "/test", "example.com", headers);

    wasi_http_types_own_response_outparam_t outparam = http_create_response_outparam();

    /* Call the handler */
    exports_wasi_http_incoming_handler_handle(request, outparam);

    /* Response should be set */
    wasi_http_types_borrow_response_outparam_t borrow =
        wasi_http_types_borrow_response_outparam(outparam);
    ASSERT_TRUE(http_response_outparam_is_set(borrow));

    /* Get response and verify status is 501 */
    wasi_http_types_own_outgoing_response_t response;
    ASSERT_TRUE(http_response_outparam_get_response(borrow, &response));

    wasi_http_types_borrow_outgoing_response_t resp_borrow =
        wasi_http_types_borrow_outgoing_response(response);
    wasi_http_types_status_code_t status =
        wasi_http_types_method_outgoing_response_status_code(resp_borrow);
    ASSERT_EQ(status, 501);

    wasi_http_types_response_outparam_drop_own(outparam);
}

/* ============================================================================
 * Test Runner
 * ============================================================================
 */

int run_http_tests(void) {
    /* Reset counters */
    tests_passed = 0;
    tests_failed = 0;

    printf("=== HTTP Tests ===\n");

    /* Phase 1: Fields resource tests */
    RUN_TEST(fields_constructor);
    RUN_TEST(fields_from_list);
    RUN_TEST(fields_get_set);
    RUN_TEST(fields_case_insensitivity);
    RUN_TEST(fields_multiple_values);
    RUN_TEST(fields_delete);
    RUN_TEST(fields_entries);
    RUN_TEST(fields_clone);
    RUN_TEST(fields_invalid_name);
    RUN_TEST(fields_forbidden_name);

    /* Phase 2: Outgoing request tests */
    RUN_TEST(outgoing_request_constructor);
    RUN_TEST(outgoing_request_method);
    RUN_TEST(outgoing_request_path);
    RUN_TEST(outgoing_request_authority);
    RUN_TEST(outgoing_request_scheme);

    /* Phase 2: Request options tests */
    RUN_TEST(request_options_constructor);
    RUN_TEST(request_options_timeouts);

    /* Phase 2: Outgoing response tests */
    RUN_TEST(outgoing_response_constructor);
    RUN_TEST(outgoing_response_status_code);
    RUN_TEST(outgoing_response_invalid_status);

    /* Phase 3: Outgoing body tests */
    RUN_TEST(outgoing_body_single_retrieval);
    RUN_TEST(outgoing_body_write_stream);
    RUN_TEST(outgoing_body_finish);

    /* Phase 3: Extended request options tests */
    RUN_TEST(request_options_clear_timeout);

    /* Phase 3: Future response tests */
    RUN_TEST(future_response_subscribe);

    /* Phase 4: Incoming request tests */
    RUN_TEST(incoming_request_create);
    RUN_TEST(incoming_request_method);
    RUN_TEST(incoming_request_path);
    RUN_TEST(incoming_request_authority);
    RUN_TEST(incoming_request_headers);

    /* Phase 4: Response outparam tests */
    RUN_TEST(response_outparam_create);
    RUN_TEST(response_outparam_set);

    /* Phase 4: Incoming handler tests */
    RUN_TEST(incoming_handler_default);

    printf("\n");
    printf("HTTP Tests: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed;
}

#ifndef TEST_RUNNER_MODE
int main(void) {
    return run_http_tests() > 0 ? 1 : 0;
}
#endif
