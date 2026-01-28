/**
 * WASI HTTP Implementation
 *
 * This file implements the wasi:http interfaces for macOS (UNIX) and GNU/Linux.
 *
 * Interfaces implemented:
 *   - wasi:http/types@0.2.0      - HTTP types and resources
 *   - wasi:http/outgoing-handler@0.2.0 - HTTP client
 *   - wasi:http/incoming-handler@0.2.0 - HTTP server/proxy (exported)
 */

/* Feature test macros must come first */
#ifdef __linux__
    #define _GNU_SOURCE  /* Enable GNU extensions on Linux */
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* For strcasecmp */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>    /* For snprintf */

#include "common.h"

/* Include the generated bindings header */
#include "../../build/c-bindings/http/proxy.h"

WASI_ABI_CHECK_PTR_LEN_TYPE(proxy_string_t);
WASI_ABI_CHECK_PTR_LEN_TYPE(proxy_list_u8_t);
WASI_ABI_CHECK_PTR_LEN_TYPE(proxy_list_u32_t);
WASI_ABI_CHECK_PTR_LEN_TYPE(proxy_list_field_value_t);
WASI_ABI_CHECK_PTR_LEN_TYPE(proxy_list_tuple2_field_key_field_value_t);
WASI_ABI_CHECK_PTR_LEN_TYPE(wasi_http_types_field_value_t);

/* ============================================================================
 * Forward declarations for helper functions
 * ============================================================================
 */

/* Helper functions for building incoming handler applications (testing) */
wasi_http_types_own_incoming_request_t http_create_incoming_request(
    wasi_http_types_method_t method,
    const char *path,
    const char *authority,
    wasi_http_types_own_fields_t headers
);
wasi_http_types_own_response_outparam_t http_create_response_outparam(void);
bool http_response_outparam_is_set(wasi_http_types_borrow_response_outparam_t outparam);
bool http_response_outparam_get_response(
    wasi_http_types_borrow_response_outparam_t outparam,
    wasi_http_types_own_outgoing_response_t *response
);

static bool http_copy_utf8_string(uint8_t **dst, size_t *dst_len, const uint8_t *src, size_t len) {
    wasi_utf8_validate_or_abort(src, len);
    *dst_len = len;
    if (!wasi_cabi_alloc_string(len, dst)) {
        *dst = NULL;
        *dst_len = 0;
        return false;
    }
    if (*dst) memcpy(*dst, src, len);
    return true;
}

static bool http_copy_bytes(uint8_t **dst, size_t len) {
    if (!wasi_cabi_alloc_list(len, 1, 1, (void **)dst)) {
        *dst = NULL;
        return false;
    }
    return true;
}

/* ============================================================================
 * Handle Management
 * ============================================================================
 */

#define MAX_HTTP_HANDLES 1024

typedef enum {
    HTTP_HANDLE_NONE = 0,
    HTTP_HANDLE_FIELDS,
    HTTP_HANDLE_INCOMING_REQUEST,
    HTTP_HANDLE_OUTGOING_REQUEST,
    HTTP_HANDLE_INCOMING_RESPONSE,
    HTTP_HANDLE_OUTGOING_RESPONSE,
    HTTP_HANDLE_INCOMING_BODY,
    HTTP_HANDLE_OUTGOING_BODY,
    HTTP_HANDLE_FUTURE_TRAILERS,
    HTTP_HANDLE_FUTURE_INCOMING_RESPONSE,
    HTTP_HANDLE_REQUEST_OPTIONS,
    HTTP_HANDLE_RESPONSE_OUTPARAM,
} http_handle_type_t;

/* ============================================================================
 * Fields Resource
 * ============================================================================
 * HTTP headers/trailers are represented as a list of key-value pairs.
 * Keys are case-insensitive, and multiple values per key are supported.
 */

typedef struct {
    char *name;        /* Header name (lowercase for comparison) */
    size_t name_len;
    uint8_t *value;    /* Header value (raw bytes) */
    size_t value_len;
} http_field_entry_t;

typedef struct {
    http_field_entry_t *entries;
    size_t count;
    size_t capacity;
    bool immutable;    /* true for headers from incoming requests/responses */
} http_fields_resource_t;

/* Handle tables */
static http_fields_resource_t *fields_table[MAX_HTTP_HANDLES];
static int32_t next_fields_handle = 1;

/* ============================================================================
 * Fields Helper Functions
 * ============================================================================
 */

/* Allocate a new fields resource and return its handle */
static int32_t alloc_fields(void) {
    if (next_fields_handle >= MAX_HTTP_HANDLES) {
        return -1;
    }

    http_fields_resource_t *fields = calloc(1, sizeof(http_fields_resource_t));
    if (!fields) {
        return -1;
    }

    fields->entries = NULL;
    fields->count = 0;
    fields->capacity = 0;
    fields->immutable = false;

    int32_t handle = next_fields_handle++;
    fields_table[handle] = fields;
    return handle;
}

/* Get fields resource from handle */
static http_fields_resource_t *get_fields(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return fields_table[handle];
}

/* Free a single field entry */
static void free_field_entry(http_field_entry_t *entry) {
    if (entry->name) {
        free(entry->name);
        entry->name = NULL;
    }
    if (entry->value) {
        free(entry->value);
        entry->value = NULL;
    }
}

/* Free all fields in a resource */
static void free_fields_resource(http_fields_resource_t *fields) {
    if (!fields) return;

    for (size_t i = 0; i < fields->count; i++) {
        free_field_entry(&fields->entries[i]);
    }
    free(fields->entries);
    free(fields);
}

/* Ensure capacity for at least n entries */
static bool fields_ensure_capacity(http_fields_resource_t *fields, size_t n) {
    if (fields->capacity >= n) {
        return true;
    }

    size_t new_capacity = fields->capacity == 0 ? 8 : fields->capacity * 2;
    while (new_capacity < n) {
        new_capacity *= 2;
    }

    http_field_entry_t *new_entries = realloc(fields->entries,
                                               new_capacity * sizeof(http_field_entry_t));
    if (!new_entries) {
        return false;
    }

    fields->entries = new_entries;
    fields->capacity = new_capacity;
    return true;
}

/* Validate a field name (must be valid HTTP token) */
static bool is_valid_field_name(const uint8_t *name, size_t len) {
    if (len == 0) return false;

    /* HTTP token characters: !#$%&'*+-.0-9A-Z^_`a-z|~ */
    for (size_t i = 0; i < len; i++) {
        uint8_t c = name[i];
        if (c < 0x21 || c > 0x7e) return false;
        /* Forbidden characters in tokens */
        if (c == '(' || c == ')' || c == '<' || c == '>' || c == '@' ||
            c == ',' || c == ';' || c == ':' || c == '\\' || c == '"' ||
            c == '/' || c == '[' || c == ']' || c == '?' || c == '=' ||
            c == '{' || c == '}') {
            return false;
        }
    }
    return true;
}

/* Validate a field value (must be valid HTTP field content) */
static bool is_valid_field_value(const uint8_t *value, size_t len) {
    /* Empty values are allowed */
    if (len == 0) return true;

    /* Field values should be printable ASCII or horizontal tab */
    for (size_t i = 0; i < len; i++) {
        uint8_t c = value[i];
        /* Allow printable ASCII (0x20-0x7e) and horizontal tab (0x09) */
        if ((c < 0x20 || c > 0x7e) && c != 0x09) {
            return false;
        }
    }
    return true;
}

/* Check if a field name is forbidden (pseudo-headers, etc.) */
static bool is_forbidden_field_name(const uint8_t *name, size_t len) {
    /* Pseudo-headers starting with ':' are forbidden */
    if (len > 0 && name[0] == ':') {
        return true;
    }
    return false;
}

/* Compare field names case-insensitively */
static bool field_name_equals(const char *a, size_t a_len,
                              const uint8_t *b, size_t b_len) {
    if (a_len != b_len) return false;
    for (size_t i = 0; i < a_len; i++) {
        if (tolower((unsigned char)a[i]) != tolower(b[i])) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * Fields Resource Implementation
 * ============================================================================
 */

/* Construct an empty HTTP Fields */
wasi_http_types_own_fields_t wasi_http_types_constructor_fields(void) {
    int32_t handle = alloc_fields();
    return (wasi_http_types_own_fields_t){ handle };
}

/* Construct HTTP Fields from a list of entries */
bool wasi_http_types_static_fields_from_list(
    proxy_list_tuple2_field_key_field_value_t *entries,
    wasi_http_types_own_fields_t *ret,
    wasi_http_types_header_error_t *err
) {
    int32_t handle = alloc_fields();
    if (handle < 0) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
        return false;
    }

    http_fields_resource_t *fields = get_fields(handle);

    /* Pre-allocate capacity */
    if (!fields_ensure_capacity(fields, entries->len)) {
        free_fields_resource(fields);
        fields_table[handle] = NULL;
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
        return false;
    }

    /* Copy all entries */
    for (size_t i = 0; i < entries->len; i++) {
        proxy_tuple2_field_key_field_value_t *entry = &entries->ptr[i];

        wasi_utf8_validate_or_abort(entry->f0.ptr, entry->f0.len);

        /* Check for forbidden field names first (pseudo-headers starting with ':') */
        if (is_forbidden_field_name(entry->f0.ptr, entry->f0.len)) {
            free_fields_resource(fields);
            fields_table[handle] = NULL;
            err->tag = WASI_HTTP_TYPES_HEADER_ERROR_FORBIDDEN;
            return false;
        }

        /* Validate field name syntax */
        if (!is_valid_field_name(entry->f0.ptr, entry->f0.len)) {
            free_fields_resource(fields);
            fields_table[handle] = NULL;
            err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
            return false;
        }

        /* Validate field value */
        if (!is_valid_field_value(entry->f1.ptr, entry->f1.len)) {
            free_fields_resource(fields);
            fields_table[handle] = NULL;
            err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
            return false;
        }

        /* Allocate and copy name (stored lowercase) */
        char *name = malloc(entry->f0.len + 1);
        if (!name) {
            free_fields_resource(fields);
            fields_table[handle] = NULL;
            err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
            return false;
        }
        for (size_t j = 0; j < entry->f0.len; j++) {
            name[j] = (char)tolower((unsigned char)entry->f0.ptr[j]);
        }
        name[entry->f0.len] = '\0';

        /* Allocate and copy value */
        uint8_t *value = NULL;
        if (entry->f1.len > 0) {
            value = malloc(entry->f1.len);
            if (!value) {
                free(name);
                free_fields_resource(fields);
                fields_table[handle] = NULL;
                err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
                return false;
            }
            memcpy(value, entry->f1.ptr, entry->f1.len);
        }

        fields->entries[fields->count].name = name;
        fields->entries[fields->count].name_len = entry->f0.len;
        fields->entries[fields->count].value = value;
        fields->entries[fields->count].value_len = entry->f1.len;
        fields->count++;
    }

    ret->__handle = handle;
    return true;
}

/* Get all values for a field name */
void wasi_http_types_method_fields_get(
    wasi_http_types_borrow_fields_t self,
    wasi_http_types_field_key_t *name,
    proxy_list_field_value_t *ret
) {
    wasi_utf8_validate_or_abort(name->ptr, name->len);
    http_fields_resource_t *fields = get_fields(self.__handle);
    if (!fields) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    /* Count matching entries */
    size_t count = 0;
    for (size_t i = 0; i < fields->count; i++) {
        if (field_name_equals(fields->entries[i].name, fields->entries[i].name_len,
                              name->ptr, name->len)) {
            count++;
        }
    }

    if (count == 0) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    /* Allocate result array */
    wasi_http_types_field_value_t *values = NULL;
    if (!wasi_cabi_alloc_list(count, sizeof(wasi_http_types_field_value_t),
                              WASI_ALIGNOF(wasi_http_types_field_value_t), (void **)&values)) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    /* Copy matching values */
    size_t idx = 0;
    for (size_t i = 0; i < fields->count && idx < count; i++) {
        if (field_name_equals(fields->entries[i].name, fields->entries[i].name_len,
                              name->ptr, name->len)) {
            if (fields->entries[i].value_len > 0) {
                values[idx].len = fields->entries[i].value_len;
                if (http_copy_bytes(&values[idx].ptr, fields->entries[i].value_len)) {
                    memcpy(values[idx].ptr, fields->entries[i].value,
                           fields->entries[i].value_len);
                } else {
                    values[idx].ptr = NULL;
                    values[idx].len = 0;
                }
            } else {
                values[idx].ptr = NULL;
                values[idx].len = 0;
            }
            idx++;
        }
    }

    ret->ptr = values;
    ret->len = count;
}

/* Check if a field name exists */
bool wasi_http_types_method_fields_has(
    wasi_http_types_borrow_fields_t self,
    wasi_http_types_field_key_t *name
) {
    wasi_utf8_validate_or_abort(name->ptr, name->len);
    http_fields_resource_t *fields = get_fields(self.__handle);
    if (!fields) {
        return false;
    }

    /* Validate field name */
    if (!is_valid_field_name(name->ptr, name->len)) {
        return false;
    }

    for (size_t i = 0; i < fields->count; i++) {
        if (field_name_equals(fields->entries[i].name, fields->entries[i].name_len,
                              name->ptr, name->len)) {
            return true;
        }
    }

    return false;
}

/* Set all values for a field name */
bool wasi_http_types_method_fields_set(
    wasi_http_types_borrow_fields_t self,
    wasi_http_types_field_key_t *name,
    proxy_list_field_value_t *value,
    wasi_http_types_header_error_t *err
) {
    wasi_utf8_validate_or_abort(name->ptr, name->len);
    http_fields_resource_t *fields = get_fields(self.__handle);
    if (!fields) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
        return false;
    }

    if (fields->immutable) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_IMMUTABLE;
        return false;
    }

    /* Validate field name */
    if (!is_valid_field_name(name->ptr, name->len)) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
        return false;
    }

    if (is_forbidden_field_name(name->ptr, name->len)) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_FORBIDDEN;
        return false;
    }

    /* Validate all values */
    for (size_t i = 0; i < value->len; i++) {
        if (!is_valid_field_value(value->ptr[i].ptr, value->ptr[i].len)) {
            err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
            return false;
        }
    }

    /* Delete existing entries for this name */
    wasi_http_types_method_fields_delete(self, name, err);

    /* Add new entries */
    for (size_t i = 0; i < value->len; i++) {
        if (!wasi_http_types_method_fields_append(self, name, &value->ptr[i], err)) {
            return false;
        }
    }

    return true;
}

/* Delete all values for a field name */
bool wasi_http_types_method_fields_delete(
    wasi_http_types_borrow_fields_t self,
    wasi_http_types_field_key_t *name,
    wasi_http_types_header_error_t *err
) {
    wasi_utf8_validate_or_abort(name->ptr, name->len);
    http_fields_resource_t *fields = get_fields(self.__handle);
    if (!fields) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
        return false;
    }

    if (fields->immutable) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_IMMUTABLE;
        return false;
    }

    /* Remove all entries matching the name */
    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < fields->count; read_idx++) {
        if (!field_name_equals(fields->entries[read_idx].name,
                               fields->entries[read_idx].name_len,
                               name->ptr, name->len)) {
            /* Keep this entry */
            if (write_idx != read_idx) {
                fields->entries[write_idx] = fields->entries[read_idx];
            }
            write_idx++;
        } else {
            /* Free this entry */
            free_field_entry(&fields->entries[read_idx]);
        }
    }

    fields->count = write_idx;
    return true;
}

/* Append a value for a field name */
bool wasi_http_types_method_fields_append(
    wasi_http_types_borrow_fields_t self,
    wasi_http_types_field_key_t *name,
    wasi_http_types_field_value_t *value,
    wasi_http_types_header_error_t *err
) {
    wasi_utf8_validate_or_abort(name->ptr, name->len);
    http_fields_resource_t *fields = get_fields(self.__handle);
    if (!fields) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
        return false;
    }

    if (fields->immutable) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_IMMUTABLE;
        return false;
    }

    /* Validate field name */
    if (!is_valid_field_name(name->ptr, name->len)) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
        return false;
    }

    if (is_forbidden_field_name(name->ptr, name->len)) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_FORBIDDEN;
        return false;
    }

    /* Validate field value */
    if (!is_valid_field_value(value->ptr, value->len)) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
        return false;
    }

    /* Ensure capacity */
    if (!fields_ensure_capacity(fields, fields->count + 1)) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
        return false;
    }

    /* Allocate and copy name (stored lowercase) */
    char *name_copy = malloc(name->len + 1);
    if (!name_copy) {
        err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
        return false;
    }
    for (size_t i = 0; i < name->len; i++) {
        name_copy[i] = (char)tolower((unsigned char)name->ptr[i]);
    }
    name_copy[name->len] = '\0';

    /* Allocate and copy value */
    uint8_t *value_copy = NULL;
    if (value->len > 0) {
        value_copy = malloc(value->len);
        if (!value_copy) {
            free(name_copy);
            err->tag = WASI_HTTP_TYPES_HEADER_ERROR_INVALID_SYNTAX;
            return false;
        }
        memcpy(value_copy, value->ptr, value->len);
    }

    fields->entries[fields->count].name = name_copy;
    fields->entries[fields->count].name_len = name->len;
    fields->entries[fields->count].value = value_copy;
    fields->entries[fields->count].value_len = value->len;
    fields->count++;

    return true;
}

/* Get all entries */
void wasi_http_types_method_fields_entries(
    wasi_http_types_borrow_fields_t self,
    proxy_list_tuple2_field_key_field_value_t *ret
) {
    http_fields_resource_t *fields = get_fields(self.__handle);
    if (!fields || fields->count == 0) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    proxy_tuple2_field_key_field_value_t *entries = NULL;
    if (!wasi_cabi_alloc_list(fields->count, sizeof(proxy_tuple2_field_key_field_value_t),
                              WASI_ALIGNOF(proxy_tuple2_field_key_field_value_t), (void **)&entries)) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    for (size_t i = 0; i < fields->count; i++) {
        /* Copy name */
        (void)http_copy_utf8_string(&entries[i].f0.ptr, &entries[i].f0.len,
                                    (const uint8_t *)fields->entries[i].name,
                                    fields->entries[i].name_len);

        /* Copy value */
        if (fields->entries[i].value_len > 0) {
            entries[i].f1.len = fields->entries[i].value_len;
            if (http_copy_bytes(&entries[i].f1.ptr, fields->entries[i].value_len)) {
                memcpy(entries[i].f1.ptr, fields->entries[i].value,
                       fields->entries[i].value_len);
            } else {
                entries[i].f1.ptr = NULL;
                entries[i].f1.len = 0;
            }
        } else {
            entries[i].f1.ptr = NULL;
            entries[i].f1.len = 0;
        }
    }

    ret->ptr = entries;
    ret->len = fields->count;
}

/* Clone fields (deep copy) */
wasi_http_types_own_fields_t wasi_http_types_method_fields_clone(
    wasi_http_types_borrow_fields_t self
) {
    http_fields_resource_t *src = get_fields(self.__handle);
    if (!src) {
        return (wasi_http_types_own_fields_t){ -1 };
    }

    int32_t handle = alloc_fields();
    if (handle < 0) {
        return (wasi_http_types_own_fields_t){ -1 };
    }

    http_fields_resource_t *dst = get_fields(handle);

    if (src->count > 0) {
        if (!fields_ensure_capacity(dst, src->count)) {
            free_fields_resource(dst);
            fields_table[handle] = NULL;
            return (wasi_http_types_own_fields_t){ -1 };
        }

        for (size_t i = 0; i < src->count; i++) {
            /* Copy name */
            char *name = malloc(src->entries[i].name_len + 1);
            if (!name) {
                free_fields_resource(dst);
                fields_table[handle] = NULL;
                return (wasi_http_types_own_fields_t){ -1 };
            }
            memcpy(name, src->entries[i].name, src->entries[i].name_len);
            name[src->entries[i].name_len] = '\0';

            /* Copy value */
            uint8_t *value = NULL;
            if (src->entries[i].value_len > 0) {
                value = malloc(src->entries[i].value_len);
                if (!value) {
                    free(name);
                    free_fields_resource(dst);
                    fields_table[handle] = NULL;
                    return (wasi_http_types_own_fields_t){ -1 };
                }
                memcpy(value, src->entries[i].value, src->entries[i].value_len);
            }

            dst->entries[dst->count].name = name;
            dst->entries[dst->count].name_len = src->entries[i].name_len;
            dst->entries[dst->count].value = value;
            dst->entries[dst->count].value_len = src->entries[i].value_len;
            dst->count++;
        }
    }

    /* Clone is always mutable */
    dst->immutable = false;

    return (wasi_http_types_own_fields_t){ handle };
}

/* ============================================================================
 * Handle Drop Functions
 * ============================================================================
 */

void wasi_http_types_fields_drop_own(wasi_http_types_own_fields_t handle) {
    http_fields_resource_t *fields = get_fields(handle.__handle);
    if (fields) {
        free_fields_resource(fields);
        fields_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_fields_drop_borrow(wasi_http_types_borrow_fields_t handle) {
    /* Borrow drops are no-ops */
    (void)handle;
}

wasi_http_types_borrow_fields_t wasi_http_types_borrow_fields(
    wasi_http_types_own_fields_t handle
) {
    return (wasi_http_types_borrow_fields_t){ handle.__handle };
}

/* ============================================================================
 * http_error_code function
 * ============================================================================
 */

bool wasi_http_types_http_error_code(
    wasi_http_types_borrow_io_error_t err_,
    wasi_http_types_error_code_t *ret
) {
    /* For now, we don't have HTTP-specific error codes stored in io errors */
    (void)err_;
    (void)ret;
    return false;
}

/* ============================================================================
 * Phase 2: Outgoing Request Resource
 * ============================================================================
 */

typedef struct {
    wasi_http_types_method_t method;
    char *path_with_query;     /* NULL means not set */
    size_t path_len;
    wasi_http_types_scheme_t scheme;
    bool scheme_set;
    char *authority;           /* NULL means not set */
    size_t authority_len;
    int32_t headers_handle;    /* Handle to associated Fields resource */
    int32_t body_handle;       /* Handle to associated OutgoingBody (-1 if not retrieved) */
    bool body_retrieved;       /* Can only get body once */
} http_outgoing_request_resource_t;

/* Handle table for outgoing requests */
static http_outgoing_request_resource_t *outgoing_request_table[MAX_HTTP_HANDLES];
static int32_t next_outgoing_request_handle = 1;

/* Allocate a new outgoing request */
static int32_t alloc_outgoing_request(void) {
    if (next_outgoing_request_handle >= MAX_HTTP_HANDLES) {
        return -1;
    }

    http_outgoing_request_resource_t *req = calloc(1, sizeof(http_outgoing_request_resource_t));
    if (!req) {
        return -1;
    }

    /* Default method is GET */
    req->method.tag = WASI_HTTP_TYPES_METHOD_GET;
    req->path_with_query = NULL;
    req->path_len = 0;
    req->scheme.tag = WASI_HTTP_TYPES_SCHEME_HTTP;
    req->scheme_set = false;
    req->authority = NULL;
    req->authority_len = 0;
    req->headers_handle = -1;
    req->body_handle = -1;
    req->body_retrieved = false;

    int32_t handle = next_outgoing_request_handle++;
    outgoing_request_table[handle] = req;
    return handle;
}

/* Get outgoing request from handle */
static http_outgoing_request_resource_t *get_outgoing_request(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return outgoing_request_table[handle];
}

/* Free outgoing request resource */
static void free_outgoing_request(http_outgoing_request_resource_t *req) {
    if (!req) return;

    if (req->method.tag == WASI_HTTP_TYPES_METHOD_OTHER && req->method.val.other.ptr) {
        free(req->method.val.other.ptr);
    }
    if (req->path_with_query) {
        free(req->path_with_query);
    }
    if (req->scheme.tag == WASI_HTTP_TYPES_SCHEME_OTHER && req->scheme.val.other.ptr) {
        free(req->scheme.val.other.ptr);
    }
    if (req->authority) {
        free(req->authority);
    }
    /* Note: We don't free headers/body here - they have their own lifecycle */
    free(req);
}

/* Validate HTTP method string */
static bool is_valid_method_string(const uint8_t *str, size_t len) {
    if (len == 0) return false;
    /* Method must be token characters */
    for (size_t i = 0; i < len; i++) {
        uint8_t c = str[i];
        if (c < 0x21 || c > 0x7e) return false;
        /* Forbidden in tokens */
        if (c == '(' || c == ')' || c == '<' || c == '>' || c == '@' ||
            c == ',' || c == ';' || c == ':' || c == '\\' || c == '"' ||
            c == '/' || c == '[' || c == ']' || c == '?' || c == '=' ||
            c == '{' || c == '}') {
            return false;
        }
    }
    return true;
}

/* Validate path with query */
static bool is_valid_path_with_query(const uint8_t *str, size_t len) {
    if (len == 0) return true;  /* Empty is valid */
    /* Basic validation - should start with / or be relative */
    for (size_t i = 0; i < len; i++) {
        uint8_t c = str[i];
        /* Allow printable ASCII except space and some control chars */
        if (c < 0x21 || c > 0x7e) return false;
    }
    return true;
}

/* Validate authority (host:port) */
static bool is_valid_authority(const uint8_t *str, size_t len) {
    if (len == 0) return true;  /* Empty is valid (for some schemes) */
    /* Basic validation */
    for (size_t i = 0; i < len; i++) {
        uint8_t c = str[i];
        if (c < 0x21 || c > 0x7e) return false;
    }
    return true;
}

/* Validate scheme string */
static bool is_valid_scheme_string(const uint8_t *str, size_t len) {
    if (len == 0) return false;
    /* Scheme must start with alpha */
    if (!isalpha(str[0])) return false;
    /* Rest can be alpha, digit, +, -, . */
    for (size_t i = 1; i < len; i++) {
        uint8_t c = str[i];
        if (!isalnum(c) && c != '+' && c != '-' && c != '.') return false;
    }
    return true;
}

/* Constructor - creates outgoing request with headers */
wasi_http_types_own_outgoing_request_t wasi_http_types_constructor_outgoing_request(
    wasi_http_types_own_headers_t headers
) {
    int32_t handle = alloc_outgoing_request();
    if (handle < 0) {
        return (wasi_http_types_own_outgoing_request_t){ -1 };
    }

    http_outgoing_request_resource_t *req = get_outgoing_request(handle);
    req->headers_handle = headers.__handle;

    return (wasi_http_types_own_outgoing_request_t){ handle };
}

/* Get method */
void wasi_http_types_method_outgoing_request_method(
    wasi_http_types_borrow_outgoing_request_t self,
    wasi_http_types_method_t *ret
) {
    http_outgoing_request_resource_t *req = get_outgoing_request(self.__handle);
    if (!req) {
        ret->tag = WASI_HTTP_TYPES_METHOD_GET;
        return;
    }

    ret->tag = req->method.tag;
    if (req->method.tag == WASI_HTTP_TYPES_METHOD_OTHER) {
        (void)http_copy_utf8_string(&ret->val.other.ptr, &ret->val.other.len,
                                    req->method.val.other.ptr, req->method.val.other.len);
    }
}

/* Set method */
bool wasi_http_types_method_outgoing_request_set_method(
    wasi_http_types_borrow_outgoing_request_t self,
    wasi_http_types_method_t *method
) {
    http_outgoing_request_resource_t *req = get_outgoing_request(self.__handle);
    if (!req) {
        return false;
    }

    /* Validate OTHER method string */
    if (method->tag == WASI_HTTP_TYPES_METHOD_OTHER) {
        wasi_utf8_validate_or_abort(method->val.other.ptr, method->val.other.len);
        if (!is_valid_method_string(method->val.other.ptr, method->val.other.len)) {
            return false;
        }
    }

    /* Free existing OTHER method string if any */
    if (req->method.tag == WASI_HTTP_TYPES_METHOD_OTHER && req->method.val.other.ptr) {
        free(req->method.val.other.ptr);
    }

    /* Copy the new method */
    req->method.tag = method->tag;
    if (method->tag == WASI_HTTP_TYPES_METHOD_OTHER) {
        req->method.val.other.ptr = malloc(method->val.other.len);
        if (req->method.val.other.ptr) {
            memcpy(req->method.val.other.ptr, method->val.other.ptr, method->val.other.len);
            req->method.val.other.len = method->val.other.len;
        } else {
            return false;
        }
    }

    return true;
}

/* Get path with query */
bool wasi_http_types_method_outgoing_request_path_with_query(
    wasi_http_types_borrow_outgoing_request_t self,
    proxy_string_t *ret
) {
    http_outgoing_request_resource_t *req = get_outgoing_request(self.__handle);
    if (!req || !req->path_with_query) {
        return false;  /* None */
    }

    if (!http_copy_utf8_string(&ret->ptr, &ret->len,
                               (const uint8_t *)req->path_with_query, req->path_len)) {
        return false;
    }
    return true;
}

/* Set path with query */
bool wasi_http_types_method_outgoing_request_set_path_with_query(
    wasi_http_types_borrow_outgoing_request_t self,
    proxy_string_t *maybe_path_with_query
) {
    http_outgoing_request_resource_t *req = get_outgoing_request(self.__handle);
    if (!req) {
        return false;
    }

    /* Free existing path */
    if (req->path_with_query) {
        free(req->path_with_query);
        req->path_with_query = NULL;
        req->path_len = 0;
    }

    /* NULL means clear the path */
    if (!maybe_path_with_query || maybe_path_with_query->len == 0) {
        return true;
    }

    /* Validate */
    wasi_utf8_validate_or_abort(maybe_path_with_query->ptr, maybe_path_with_query->len);
    if (!is_valid_path_with_query(maybe_path_with_query->ptr, maybe_path_with_query->len)) {
        return false;
    }

    /* Copy */
    req->path_with_query = malloc(maybe_path_with_query->len + 1);
    if (!req->path_with_query) {
        return false;
    }
    memcpy(req->path_with_query, maybe_path_with_query->ptr, maybe_path_with_query->len);
    req->path_with_query[maybe_path_with_query->len] = '\0';
    req->path_len = maybe_path_with_query->len;

    return true;
}

/* Get scheme */
bool wasi_http_types_method_outgoing_request_scheme(
    wasi_http_types_borrow_outgoing_request_t self,
    wasi_http_types_scheme_t *ret
) {
    http_outgoing_request_resource_t *req = get_outgoing_request(self.__handle);
    if (!req || !req->scheme_set) {
        return false;  /* None */
    }

    ret->tag = req->scheme.tag;
    if (req->scheme.tag == WASI_HTTP_TYPES_SCHEME_OTHER) {
        (void)http_copy_utf8_string(&ret->val.other.ptr, &ret->val.other.len,
                                    req->scheme.val.other.ptr, req->scheme.val.other.len);
    }
    return true;
}

/* Set scheme */
bool wasi_http_types_method_outgoing_request_set_scheme(
    wasi_http_types_borrow_outgoing_request_t self,
    wasi_http_types_scheme_t *maybe_scheme
) {
    http_outgoing_request_resource_t *req = get_outgoing_request(self.__handle);
    if (!req) {
        return false;
    }

    /* Free existing OTHER scheme string if any */
    if (req->scheme.tag == WASI_HTTP_TYPES_SCHEME_OTHER && req->scheme.val.other.ptr) {
        free(req->scheme.val.other.ptr);
        req->scheme.val.other.ptr = NULL;
    }

    /* NULL means clear the scheme */
    if (!maybe_scheme) {
        req->scheme_set = false;
        req->scheme.tag = WASI_HTTP_TYPES_SCHEME_HTTP;
        return true;
    }

    /* Validate OTHER scheme string */
    if (maybe_scheme->tag == WASI_HTTP_TYPES_SCHEME_OTHER) {
        wasi_utf8_validate_or_abort(maybe_scheme->val.other.ptr, maybe_scheme->val.other.len);
        if (!is_valid_scheme_string(maybe_scheme->val.other.ptr, maybe_scheme->val.other.len)) {
            return false;
        }
    }

    /* Copy */
    req->scheme.tag = maybe_scheme->tag;
    req->scheme_set = true;
    if (maybe_scheme->tag == WASI_HTTP_TYPES_SCHEME_OTHER) {
        req->scheme.val.other.ptr = malloc(maybe_scheme->val.other.len);
        if (req->scheme.val.other.ptr) {
            memcpy(req->scheme.val.other.ptr, maybe_scheme->val.other.ptr, maybe_scheme->val.other.len);
            req->scheme.val.other.len = maybe_scheme->val.other.len;
        } else {
            return false;
        }
    }

    return true;
}

/* Get authority */
bool wasi_http_types_method_outgoing_request_authority(
    wasi_http_types_borrow_outgoing_request_t self,
    proxy_string_t *ret
) {
    http_outgoing_request_resource_t *req = get_outgoing_request(self.__handle);
    if (!req || !req->authority) {
        return false;  /* None */
    }

    if (!http_copy_utf8_string(&ret->ptr, &ret->len,
                               (const uint8_t *)req->authority, req->authority_len)) {
        return false;
    }
    return true;
}

/* Set authority */
bool wasi_http_types_method_outgoing_request_set_authority(
    wasi_http_types_borrow_outgoing_request_t self,
    proxy_string_t *maybe_authority
) {
    http_outgoing_request_resource_t *req = get_outgoing_request(self.__handle);
    if (!req) {
        return false;
    }

    /* Free existing authority */
    if (req->authority) {
        free(req->authority);
        req->authority = NULL;
        req->authority_len = 0;
    }

    /* NULL means clear the authority */
    if (!maybe_authority || maybe_authority->len == 0) {
        return true;
    }

    /* Validate */
    wasi_utf8_validate_or_abort(maybe_authority->ptr, maybe_authority->len);
    if (!is_valid_authority(maybe_authority->ptr, maybe_authority->len)) {
        return false;
    }

    /* Copy */
    req->authority = malloc(maybe_authority->len + 1);
    if (!req->authority) {
        return false;
    }
    memcpy(req->authority, maybe_authority->ptr, maybe_authority->len);
    req->authority[maybe_authority->len] = '\0';
    req->authority_len = maybe_authority->len;

    return true;
}

/* Get headers */
wasi_http_types_own_headers_t wasi_http_types_method_outgoing_request_headers(
    wasi_http_types_borrow_outgoing_request_t self
) {
    http_outgoing_request_resource_t *req = get_outgoing_request(self.__handle);
    if (!req) {
        return (wasi_http_types_own_headers_t){ -1 };
    }
    return (wasi_http_types_own_headers_t){ req->headers_handle };
}

/* Forward declaration for outgoing body allocation */
static int32_t alloc_outgoing_body(void);

/* Get body - can only be called once */
bool wasi_http_types_method_outgoing_request_body(
    wasi_http_types_borrow_outgoing_request_t self,
    wasi_http_types_own_outgoing_body_t *ret
) {
    http_outgoing_request_resource_t *req = get_outgoing_request(self.__handle);
    if (!req || req->body_retrieved) {
        return false;
    }

    /* Allocate outgoing body resource */
    int32_t body_handle = alloc_outgoing_body();
    if (body_handle <= 0) {
        return false;
    }

    req->body_retrieved = true;
    req->body_handle = body_handle;
    ret->__handle = body_handle;
    return true;
}

/* Drop outgoing request */
void wasi_http_types_outgoing_request_drop_own(wasi_http_types_own_outgoing_request_t handle) {
    http_outgoing_request_resource_t *req = get_outgoing_request(handle.__handle);
    if (req) {
        free_outgoing_request(req);
        outgoing_request_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_outgoing_request_drop_borrow(wasi_http_types_borrow_outgoing_request_t handle) {
    (void)handle;  /* Borrow drops are no-ops */
}

wasi_http_types_borrow_outgoing_request_t wasi_http_types_borrow_outgoing_request(
    wasi_http_types_own_outgoing_request_t handle
) {
    return (wasi_http_types_borrow_outgoing_request_t){ handle.__handle };
}

/* ============================================================================
 * Phase 2: Request Options Resource
 * ============================================================================
 */

typedef struct {
    uint64_t connect_timeout_ns;
    bool connect_timeout_set;
    uint64_t first_byte_timeout_ns;
    bool first_byte_timeout_set;
    uint64_t between_bytes_timeout_ns;
    bool between_bytes_timeout_set;
} http_request_options_resource_t;

/* Handle table for request options */
static http_request_options_resource_t *request_options_table[MAX_HTTP_HANDLES];
static int32_t next_request_options_handle = 1;

/* Allocate a new request options */
static int32_t alloc_request_options(void) {
    if (next_request_options_handle >= MAX_HTTP_HANDLES) {
        return -1;
    }

    http_request_options_resource_t *opts = calloc(1, sizeof(http_request_options_resource_t));
    if (!opts) {
        return -1;
    }

    opts->connect_timeout_set = false;
    opts->first_byte_timeout_set = false;
    opts->between_bytes_timeout_set = false;

    int32_t handle = next_request_options_handle++;
    request_options_table[handle] = opts;
    return handle;
}

/* Get request options from handle */
static http_request_options_resource_t *get_request_options(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return request_options_table[handle];
}

/* Constructor */
wasi_http_types_own_request_options_t wasi_http_types_constructor_request_options(void) {
    int32_t handle = alloc_request_options();
    return (wasi_http_types_own_request_options_t){ handle };
}

/* Get connect timeout */
bool wasi_http_types_method_request_options_connect_timeout(
    wasi_http_types_borrow_request_options_t self,
    wasi_http_types_duration_t *ret
) {
    http_request_options_resource_t *opts = get_request_options(self.__handle);
    if (!opts || !opts->connect_timeout_set) {
        return false;
    }
    *ret = opts->connect_timeout_ns;
    return true;
}

/* Set connect timeout */
bool wasi_http_types_method_request_options_set_connect_timeout(
    wasi_http_types_borrow_request_options_t self,
    wasi_http_types_duration_t *maybe_duration
) {
    http_request_options_resource_t *opts = get_request_options(self.__handle);
    if (!opts) {
        return false;
    }

    if (!maybe_duration) {
        opts->connect_timeout_set = false;
        return true;
    }

    opts->connect_timeout_ns = *maybe_duration;
    opts->connect_timeout_set = true;
    return true;
}

/* Get first byte timeout */
bool wasi_http_types_method_request_options_first_byte_timeout(
    wasi_http_types_borrow_request_options_t self,
    wasi_http_types_duration_t *ret
) {
    http_request_options_resource_t *opts = get_request_options(self.__handle);
    if (!opts || !opts->first_byte_timeout_set) {
        return false;
    }
    *ret = opts->first_byte_timeout_ns;
    return true;
}

/* Set first byte timeout */
bool wasi_http_types_method_request_options_set_first_byte_timeout(
    wasi_http_types_borrow_request_options_t self,
    wasi_http_types_duration_t *maybe_duration
) {
    http_request_options_resource_t *opts = get_request_options(self.__handle);
    if (!opts) {
        return false;
    }

    if (!maybe_duration) {
        opts->first_byte_timeout_set = false;
        return true;
    }

    opts->first_byte_timeout_ns = *maybe_duration;
    opts->first_byte_timeout_set = true;
    return true;
}

/* Get between bytes timeout */
bool wasi_http_types_method_request_options_between_bytes_timeout(
    wasi_http_types_borrow_request_options_t self,
    wasi_http_types_duration_t *ret
) {
    http_request_options_resource_t *opts = get_request_options(self.__handle);
    if (!opts || !opts->between_bytes_timeout_set) {
        return false;
    }
    *ret = opts->between_bytes_timeout_ns;
    return true;
}

/* Set between bytes timeout */
bool wasi_http_types_method_request_options_set_between_bytes_timeout(
    wasi_http_types_borrow_request_options_t self,
    wasi_http_types_duration_t *maybe_duration
) {
    http_request_options_resource_t *opts = get_request_options(self.__handle);
    if (!opts) {
        return false;
    }

    if (!maybe_duration) {
        opts->between_bytes_timeout_set = false;
        return true;
    }

    opts->between_bytes_timeout_ns = *maybe_duration;
    opts->between_bytes_timeout_set = true;
    return true;
}

/* Drop request options */
void wasi_http_types_request_options_drop_own(wasi_http_types_own_request_options_t handle) {
    http_request_options_resource_t *opts = get_request_options(handle.__handle);
    if (opts) {
        free(opts);
        request_options_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_request_options_drop_borrow(wasi_http_types_borrow_request_options_t handle) {
    (void)handle;  /* Borrow drops are no-ops */
}

wasi_http_types_borrow_request_options_t wasi_http_types_borrow_request_options(
    wasi_http_types_own_request_options_t handle
) {
    return (wasi_http_types_borrow_request_options_t){ handle.__handle };
}

/* ============================================================================
 * Phase 2: Outgoing Response Resource
 * ============================================================================
 */

typedef struct {
    wasi_http_types_status_code_t status_code;
    int32_t headers_handle;
    int32_t body_handle;
    bool body_retrieved;
} http_outgoing_response_resource_t;

/* Handle table for outgoing responses */
static http_outgoing_response_resource_t *outgoing_response_table[MAX_HTTP_HANDLES];
static int32_t next_outgoing_response_handle = 1;

/* Allocate a new outgoing response */
static int32_t alloc_outgoing_response(void) {
    if (next_outgoing_response_handle >= MAX_HTTP_HANDLES) {
        return -1;
    }

    http_outgoing_response_resource_t *resp = calloc(1, sizeof(http_outgoing_response_resource_t));
    if (!resp) {
        return -1;
    }

    resp->status_code = 200;  /* Default status */
    resp->headers_handle = -1;
    resp->body_handle = -1;
    resp->body_retrieved = false;

    int32_t handle = next_outgoing_response_handle++;
    outgoing_response_table[handle] = resp;
    return handle;
}

/* Get outgoing response from handle */
static http_outgoing_response_resource_t *get_outgoing_response(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return outgoing_response_table[handle];
}

/* Constructor */
wasi_http_types_own_outgoing_response_t wasi_http_types_constructor_outgoing_response(
    wasi_http_types_own_headers_t headers
) {
    int32_t handle = alloc_outgoing_response();
    if (handle < 0) {
        return (wasi_http_types_own_outgoing_response_t){ -1 };
    }

    http_outgoing_response_resource_t *resp = get_outgoing_response(handle);
    resp->headers_handle = headers.__handle;

    return (wasi_http_types_own_outgoing_response_t){ handle };
}

/* Get status code */
wasi_http_types_status_code_t wasi_http_types_method_outgoing_response_status_code(
    wasi_http_types_borrow_outgoing_response_t self
) {
    http_outgoing_response_resource_t *resp = get_outgoing_response(self.__handle);
    if (!resp) {
        return 500;  /* Internal error default */
    }
    return resp->status_code;
}

/* Set status code */
bool wasi_http_types_method_outgoing_response_set_status_code(
    wasi_http_types_borrow_outgoing_response_t self,
    wasi_http_types_status_code_t status_code
) {
    http_outgoing_response_resource_t *resp = get_outgoing_response(self.__handle);
    if (!resp) {
        return false;
    }

    /* Validate status code (100-999 range) */
    if (status_code < 100 || status_code > 999) {
        return false;
    }

    resp->status_code = status_code;
    return true;
}

/* Get headers */
wasi_http_types_own_headers_t wasi_http_types_method_outgoing_response_headers(
    wasi_http_types_borrow_outgoing_response_t self
) {
    http_outgoing_response_resource_t *resp = get_outgoing_response(self.__handle);
    if (!resp) {
        return (wasi_http_types_own_headers_t){ -1 };
    }
    return (wasi_http_types_own_headers_t){ resp->headers_handle };
}

/* Get body - can only be called once */
bool wasi_http_types_method_outgoing_response_body(
    wasi_http_types_borrow_outgoing_response_t self,
    wasi_http_types_own_outgoing_body_t *ret
) {
    http_outgoing_response_resource_t *resp = get_outgoing_response(self.__handle);
    if (!resp || resp->body_retrieved) {
        return false;
    }

    /* Allocate outgoing body resource */
    int32_t body_handle = alloc_outgoing_body();
    if (body_handle <= 0) {
        return false;
    }

    resp->body_retrieved = true;
    resp->body_handle = body_handle;
    ret->__handle = body_handle;
    return true;
}

/* Drop outgoing response */
void wasi_http_types_outgoing_response_drop_own(wasi_http_types_own_outgoing_response_t handle) {
    http_outgoing_response_resource_t *resp = get_outgoing_response(handle.__handle);
    if (resp) {
        free(resp);
        outgoing_response_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_outgoing_response_drop_borrow(wasi_http_types_borrow_outgoing_response_t handle) {
    (void)handle;  /* Borrow drops are no-ops */
}

wasi_http_types_borrow_outgoing_response_t wasi_http_types_borrow_outgoing_response(
    wasi_http_types_own_outgoing_response_t handle
) {
    return (wasi_http_types_borrow_outgoing_response_t){ handle.__handle };
}

/* ============================================================================
 * Phase 2: Incoming Request/Response Resources (stubs)
 * These are primarily used for server-side/proxy handling
 * ============================================================================
 */

/* Incoming request - received by server */
typedef struct {
    wasi_http_types_method_t method;
    char *path_with_query;
    size_t path_len;
    wasi_http_types_scheme_t scheme;
    bool scheme_set;
    char *authority;
    size_t authority_len;
    int32_t headers_handle;
    int32_t body_handle;
    bool body_consumed;
} http_incoming_request_resource_t;

static http_incoming_request_resource_t *incoming_request_table[MAX_HTTP_HANDLES];
static int32_t next_incoming_request_handle = 1;

static int32_t alloc_incoming_request(void) {
    if (next_incoming_request_handle >= MAX_HTTP_HANDLES) {
        return -1;
    }
    http_incoming_request_resource_t *req = calloc(1, sizeof(http_incoming_request_resource_t));
    if (!req) return -1;
    req->method.tag = WASI_HTTP_TYPES_METHOD_GET;
    req->path_with_query = NULL;
    req->path_len = 0;
    req->scheme.tag = WASI_HTTP_TYPES_SCHEME_HTTP;
    req->scheme_set = false;
    req->authority = NULL;
    req->authority_len = 0;
    req->headers_handle = -1;
    req->body_handle = -1;
    req->body_consumed = false;
    int32_t handle = next_incoming_request_handle++;
    incoming_request_table[handle] = req;
    return handle;
}

static http_incoming_request_resource_t *get_incoming_request(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return incoming_request_table[handle];
}

/* Incoming request methods */
void wasi_http_types_method_incoming_request_method(
    wasi_http_types_borrow_incoming_request_t self,
    wasi_http_types_method_t *ret
) {
    http_incoming_request_resource_t *req = get_incoming_request(self.__handle);
    if (!req) {
        ret->tag = WASI_HTTP_TYPES_METHOD_GET;
        return;
    }
    ret->tag = req->method.tag;
    if (req->method.tag == WASI_HTTP_TYPES_METHOD_OTHER) {
        (void)http_copy_utf8_string(&ret->val.other.ptr, &ret->val.other.len,
                                    req->method.val.other.ptr, req->method.val.other.len);
    }
}

bool wasi_http_types_method_incoming_request_path_with_query(
    wasi_http_types_borrow_incoming_request_t self,
    proxy_string_t *ret
) {
    http_incoming_request_resource_t *req = get_incoming_request(self.__handle);
    if (!req || !req->path_with_query) {
        return false;
    }
    (void)http_copy_utf8_string(&ret->ptr, &ret->len,
                                (const uint8_t *)req->path_with_query, req->path_len);
    return true;
}

bool wasi_http_types_method_incoming_request_scheme(
    wasi_http_types_borrow_incoming_request_t self,
    wasi_http_types_scheme_t *ret
) {
    http_incoming_request_resource_t *req = get_incoming_request(self.__handle);
    if (!req || !req->scheme_set) {
        return false;
    }
    ret->tag = req->scheme.tag;
    if (req->scheme.tag == WASI_HTTP_TYPES_SCHEME_OTHER) {
        (void)http_copy_utf8_string(&ret->val.other.ptr, &ret->val.other.len,
                                    req->scheme.val.other.ptr, req->scheme.val.other.len);
    }
    return true;
}

bool wasi_http_types_method_incoming_request_authority(
    wasi_http_types_borrow_incoming_request_t self,
    proxy_string_t *ret
) {
    http_incoming_request_resource_t *req = get_incoming_request(self.__handle);
    if (!req || !req->authority) {
        return false;
    }
    (void)http_copy_utf8_string(&ret->ptr, &ret->len,
                                (const uint8_t *)req->authority, req->authority_len);
    return true;
}

wasi_http_types_own_headers_t wasi_http_types_method_incoming_request_headers(
    wasi_http_types_borrow_incoming_request_t self
) {
    http_incoming_request_resource_t *req = get_incoming_request(self.__handle);
    if (!req) {
        return (wasi_http_types_own_headers_t){ -1 };
    }
    return (wasi_http_types_own_headers_t){ req->headers_handle };
}

bool wasi_http_types_method_incoming_request_consume(
    wasi_http_types_borrow_incoming_request_t self,
    wasi_http_types_own_incoming_body_t *ret
) {
    http_incoming_request_resource_t *req = get_incoming_request(self.__handle);
    if (!req || req->body_consumed) {
        return false;
    }
    req->body_consumed = true;
    ret->__handle = req->body_handle;
    return true;
}

void wasi_http_types_incoming_request_drop_own(wasi_http_types_own_incoming_request_t handle) {
    http_incoming_request_resource_t *req = get_incoming_request(handle.__handle);
    if (req) {
        if (req->method.tag == WASI_HTTP_TYPES_METHOD_OTHER && req->method.val.other.ptr) {
            free(req->method.val.other.ptr);
        }
        if (req->path_with_query) free(req->path_with_query);
        if (req->scheme.tag == WASI_HTTP_TYPES_SCHEME_OTHER && req->scheme.val.other.ptr) {
            free(req->scheme.val.other.ptr);
        }
        if (req->authority) free(req->authority);
        free(req);
        incoming_request_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_incoming_request_drop_borrow(wasi_http_types_borrow_incoming_request_t handle) {
    (void)handle;
}

wasi_http_types_borrow_incoming_request_t wasi_http_types_borrow_incoming_request(
    wasi_http_types_own_incoming_request_t handle
) {
    return (wasi_http_types_borrow_incoming_request_t){ handle.__handle };
}

/* Incoming response - received by client */
typedef struct {
    wasi_http_types_status_code_t status_code;
    int32_t headers_handle;
    int32_t body_handle;
    bool body_consumed;
} http_incoming_response_resource_t;

static http_incoming_response_resource_t *incoming_response_table[MAX_HTTP_HANDLES];
#ifndef WASI_SAFE_MODE
static int32_t next_incoming_response_handle = 1;
#endif

static http_incoming_response_resource_t *get_incoming_response(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return incoming_response_table[handle];
}

wasi_http_types_status_code_t wasi_http_types_method_incoming_response_status(
    wasi_http_types_borrow_incoming_response_t self
) {
    http_incoming_response_resource_t *resp = get_incoming_response(self.__handle);
    if (!resp) {
        return 500;
    }
    return resp->status_code;
}

wasi_http_types_own_headers_t wasi_http_types_method_incoming_response_headers(
    wasi_http_types_borrow_incoming_response_t self
) {
    http_incoming_response_resource_t *resp = get_incoming_response(self.__handle);
    if (!resp) {
        return (wasi_http_types_own_headers_t){ -1 };
    }
    return (wasi_http_types_own_headers_t){ resp->headers_handle };
}

bool wasi_http_types_method_incoming_response_consume(
    wasi_http_types_borrow_incoming_response_t self,
    wasi_http_types_own_incoming_body_t *ret
) {
    http_incoming_response_resource_t *resp = get_incoming_response(self.__handle);
    if (!resp || resp->body_consumed) {
        return false;
    }
    resp->body_consumed = true;
    ret->__handle = resp->body_handle;
    return true;
}

void wasi_http_types_incoming_response_drop_own(wasi_http_types_own_incoming_response_t handle) {
    http_incoming_response_resource_t *resp = get_incoming_response(handle.__handle);
    if (resp) {
        free(resp);
        incoming_response_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_incoming_response_drop_borrow(wasi_http_types_borrow_incoming_response_t handle) {
    (void)handle;
}

wasi_http_types_borrow_incoming_response_t wasi_http_types_borrow_incoming_response(
    wasi_http_types_own_incoming_response_t handle
) {
    return (wasi_http_types_borrow_incoming_response_t){ handle.__handle };
}

/* ============================================================================
 * Phase 2: Response Outparam (for incoming handler)
 * ============================================================================
 */

typedef struct {
    bool set;
    wasi_http_types_own_outgoing_response_t response;
    wasi_http_types_error_code_t error;
    bool is_error;
} http_response_outparam_resource_t;

static http_response_outparam_resource_t *response_outparam_table[MAX_HTTP_HANDLES];
static int32_t next_response_outparam_handle = 1;

static int32_t alloc_response_outparam(void) {
    if (next_response_outparam_handle >= MAX_HTTP_HANDLES) {
        return -1;
    }
    http_response_outparam_resource_t *outparam = calloc(1, sizeof(http_response_outparam_resource_t));
    if (!outparam) return -1;
    outparam->set = false;
    outparam->is_error = false;
    int32_t handle = next_response_outparam_handle++;
    response_outparam_table[handle] = outparam;
    return handle;
}

static http_response_outparam_resource_t *get_response_outparam(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return response_outparam_table[handle];
}

void wasi_http_types_static_response_outparam_set(
    wasi_http_types_own_response_outparam_t param,
    wasi_http_types_result_own_outgoing_response_error_code_t *response
) {
    http_response_outparam_resource_t *outparam = get_response_outparam(param.__handle);
    if (!outparam) {
        return;
    }

    outparam->set = true;
    if (response->is_err) {
        outparam->is_error = true;
        outparam->error = response->val.err;
    } else {
        outparam->is_error = false;
        outparam->response = response->val.ok;
    }
}

void wasi_http_types_response_outparam_drop_own(wasi_http_types_own_response_outparam_t handle) {
    http_response_outparam_resource_t *outparam = get_response_outparam(handle.__handle);
    if (outparam) {
        free(outparam);
        response_outparam_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_response_outparam_drop_borrow(wasi_http_types_borrow_response_outparam_t handle) {
    (void)handle;
}

wasi_http_types_borrow_response_outparam_t wasi_http_types_borrow_response_outparam(
    wasi_http_types_own_response_outparam_t handle
) {
    return (wasi_http_types_borrow_response_outparam_t){ handle.__handle };
}

/* ============================================================================
 * Phase 3: Body Streaming Resources
 * ============================================================================
 */

/* Outgoing body resource - for writing request/response bodies */
typedef struct {
    uint8_t *buffer;
    size_t buffer_len;
    size_t buffer_capacity;
    int32_t stream_handle;  /* Associated output stream */
    bool stream_retrieved;
    bool finished;
} http_outgoing_body_resource_t;

static http_outgoing_body_resource_t *outgoing_body_table[MAX_HTTP_HANDLES];
static int32_t next_outgoing_body_handle = 1;

static int32_t alloc_outgoing_body(void) {
    if (next_outgoing_body_handle >= MAX_HTTP_HANDLES) {
        return -1;
    }
    http_outgoing_body_resource_t *body = calloc(1, sizeof(http_outgoing_body_resource_t));
    if (!body) {
        return -1;
    }
    body->buffer = NULL;
    body->buffer_len = 0;
    body->buffer_capacity = 0;
    body->stream_handle = -1;
    body->stream_retrieved = false;
    body->finished = false;
    int32_t handle = next_outgoing_body_handle++;
    outgoing_body_table[handle] = body;
    return handle;
}

static http_outgoing_body_resource_t *get_outgoing_body(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return outgoing_body_table[handle];
}

/* Get output stream for writing */
bool wasi_http_types_method_outgoing_body_write(
    wasi_http_types_borrow_outgoing_body_t self,
    wasi_http_types_own_output_stream_t *ret
) {
    http_outgoing_body_resource_t *body = get_outgoing_body(self.__handle);
    if (!body || body->stream_retrieved) {
        return false;
    }
    body->stream_retrieved = true;
    /* Return a placeholder stream handle - actual stream implementation
       would use the io.c output stream infrastructure */
    ret->__handle = self.__handle;  /* Use same handle for simplicity */
    return true;
}

/* Finish outgoing body */
bool wasi_http_types_static_outgoing_body_finish(
    wasi_http_types_own_outgoing_body_t this_,
    wasi_http_types_own_trailers_t *maybe_trailers,
    wasi_http_types_error_code_t *err
) {
    http_outgoing_body_resource_t *body = get_outgoing_body(this_.__handle);
    if (!body) {
        err->tag = WASI_HTTP_TYPES_ERROR_CODE_INTERNAL_ERROR;
        return false;
    }
    body->finished = true;
    (void)maybe_trailers;  /* Trailers ignored for now */
    return true;
}

void wasi_http_types_outgoing_body_drop_own(wasi_http_types_own_outgoing_body_t handle) {
    http_outgoing_body_resource_t *body = get_outgoing_body(handle.__handle);
    if (body) {
        if (body->buffer) free(body->buffer);
        free(body);
        outgoing_body_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_outgoing_body_drop_borrow(wasi_http_types_borrow_outgoing_body_t handle) {
    (void)handle;
}

wasi_http_types_borrow_outgoing_body_t wasi_http_types_borrow_outgoing_body(
    wasi_http_types_own_outgoing_body_t handle
) {
    return (wasi_http_types_borrow_outgoing_body_t){ handle.__handle };
}

/* Incoming body resource - for reading request/response bodies */
typedef struct {
    uint8_t *buffer;
    size_t buffer_len;
    size_t read_pos;
    int32_t stream_handle;
    bool stream_retrieved;
    bool finished;
    int32_t trailers_handle;  /* Associated trailers (-1 if none) */
} http_incoming_body_resource_t;

static http_incoming_body_resource_t *incoming_body_table[MAX_HTTP_HANDLES];
#ifndef WASI_SAFE_MODE
static int32_t next_incoming_body_handle = 1;

static int32_t alloc_incoming_body(void) {
    if (next_incoming_body_handle >= MAX_HTTP_HANDLES) {
        return -1;
    }
    http_incoming_body_resource_t *body = calloc(1, sizeof(http_incoming_body_resource_t));
    if (!body) {
        return -1;
    }
    body->buffer = NULL;
    body->buffer_len = 0;
    body->read_pos = 0;
    body->stream_handle = -1;
    body->stream_retrieved = false;
    body->finished = false;
    body->trailers_handle = -1;
    int32_t handle = next_incoming_body_handle++;
    incoming_body_table[handle] = body;
    return handle;
}
#endif /* !WASI_SAFE_MODE */

static http_incoming_body_resource_t *get_incoming_body(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return incoming_body_table[handle];
}

#ifndef WASI_SAFE_MODE
/* Set incoming body data (called during response parsing) */
static void incoming_body_set_data(http_incoming_body_resource_t *body,
                                   uint8_t *data, size_t len) {
    body->buffer = data;
    body->buffer_len = len;
    body->read_pos = 0;
}
#endif /* !WASI_SAFE_MODE */

/* Get input stream for reading */
bool wasi_http_types_method_incoming_body_stream(
    wasi_http_types_borrow_incoming_body_t self,
    wasi_http_types_own_input_stream_t *ret
) {
    http_incoming_body_resource_t *body = get_incoming_body(self.__handle);
    if (!body || body->stream_retrieved) {
        return false;
    }
    body->stream_retrieved = true;
    ret->__handle = self.__handle;
    return true;
}

/* Finish incoming body and get future trailers */
wasi_http_types_own_future_trailers_t wasi_http_types_static_incoming_body_finish(
    wasi_http_types_own_incoming_body_t this_
) {
    http_incoming_body_resource_t *body = get_incoming_body(this_.__handle);
    if (!body) {
        return (wasi_http_types_own_future_trailers_t){ -1 };
    }
    body->finished = true;
    /* Return trailers handle if we have one */
    return (wasi_http_types_own_future_trailers_t){ body->trailers_handle };
}

void wasi_http_types_incoming_body_drop_own(wasi_http_types_own_incoming_body_t handle) {
    http_incoming_body_resource_t *body = get_incoming_body(handle.__handle);
    if (body) {
        if (body->buffer) free(body->buffer);
        free(body);
        incoming_body_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_incoming_body_drop_borrow(wasi_http_types_borrow_incoming_body_t handle) {
    (void)handle;
}

wasi_http_types_borrow_incoming_body_t wasi_http_types_borrow_incoming_body(
    wasi_http_types_own_incoming_body_t handle
) {
    return (wasi_http_types_borrow_incoming_body_t){ handle.__handle };
}

/* ============================================================================
 * Phase 3: Future Resources
 * ============================================================================
 */

/* Future trailers resource */
typedef struct {
    bool ready;
    int32_t trailers_handle;  /* Handle to Fields resource */
    bool has_error;
    wasi_http_types_error_code_t error;
} http_future_trailers_resource_t;

static http_future_trailers_resource_t *future_trailers_table[MAX_HTTP_HANDLES];

static http_future_trailers_resource_t *get_future_trailers(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return future_trailers_table[handle];
}

wasi_http_types_own_pollable_t wasi_http_types_method_future_trailers_subscribe(
    wasi_http_types_borrow_future_trailers_t self
) {
    /* Return a pollable that's always ready since we parse trailers synchronously */
    (void)self;
    return (wasi_http_types_own_pollable_t){ 1 };  /* Placeholder ready pollable */
}

bool wasi_http_types_method_future_trailers_get(
    wasi_http_types_borrow_future_trailers_t self,
    wasi_http_types_result_result_option_own_trailers_error_code_void_t *ret
) {
    http_future_trailers_resource_t *ft = get_future_trailers(self.__handle);
    if (!ft || !ft->ready) {
        return false;  /* Not ready yet */
    }

    /*
     * Structure: result<result<option<trailers>, error-code>, void>
     * - ret->is_err: outer result (void on error case)
     * - ret->val.ok: inner result<option<trailers>, error-code>
     *   - ret->val.ok.is_err: inner error flag
     *   - ret->val.ok.val.ok: option<trailers>
     *   - ret->val.ok.val.err: error-code
     */
    ret->is_err = false;  /* Outer result always succeeds */
    if (ft->has_error) {
        /* Inner result is error */
        ret->val.ok.is_err = true;
        ret->val.ok.val.err = ft->error;
    } else if (ft->trailers_handle > 0) {
        /* Have trailers */
        ret->val.ok.is_err = false;
        ret->val.ok.val.ok.is_some = true;
        ret->val.ok.val.ok.val.__handle = ft->trailers_handle;
    } else {
        /* No trailers */
        ret->val.ok.is_err = false;
        ret->val.ok.val.ok.is_some = false;
    }
    return true;
}

void wasi_http_types_future_trailers_drop_own(wasi_http_types_own_future_trailers_t handle) {
    http_future_trailers_resource_t *ft = get_future_trailers(handle.__handle);
    if (ft) {
        free(ft);
        future_trailers_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_future_trailers_drop_borrow(wasi_http_types_borrow_future_trailers_t handle) {
    (void)handle;
}

wasi_http_types_borrow_future_trailers_t wasi_http_types_borrow_future_trailers(
    wasi_http_types_own_future_trailers_t handle
) {
    return (wasi_http_types_borrow_future_trailers_t){ handle.__handle };
}

/* Future incoming response resource */
typedef struct {
    bool ready;
    bool has_error;
    wasi_http_types_error_code_t error;
    int32_t response_handle;  /* Handle to incoming response */
} http_future_incoming_response_resource_t;

static http_future_incoming_response_resource_t *future_response_table[MAX_HTTP_HANDLES];

#ifndef WASI_SAFE_MODE
static int32_t next_future_response_handle = 1;

static int32_t alloc_future_response(void) {
    if (next_future_response_handle >= MAX_HTTP_HANDLES) {
        return -1;
    }
    http_future_incoming_response_resource_t *fr = calloc(1, sizeof(http_future_incoming_response_resource_t));
    if (!fr) return -1;
    fr->ready = false;
    fr->has_error = false;
    fr->response_handle = -1;
    int32_t handle = next_future_response_handle++;
    future_response_table[handle] = fr;
    return handle;
}
#endif /* !WASI_SAFE_MODE */

static http_future_incoming_response_resource_t *get_future_response(int32_t handle) {
    if (handle <= 0 || handle >= MAX_HTTP_HANDLES) {
        return NULL;
    }
    return future_response_table[handle];
}

wasi_http_types_own_pollable_t wasi_http_types_method_future_incoming_response_subscribe(
    wasi_http_types_borrow_future_incoming_response_t self
) {
    (void)self;
    return (wasi_http_types_own_pollable_t){ 1 };  /* Placeholder ready pollable */
}

bool wasi_http_types_method_future_incoming_response_get(
    wasi_http_types_borrow_future_incoming_response_t self,
    wasi_http_types_result_result_own_incoming_response_error_code_void_t *ret
) {
    http_future_incoming_response_resource_t *fr = get_future_response(self.__handle);
    if (!fr || !fr->ready) {
        return false;  /* Not ready yet */
    }

    /*
     * Structure: result<result<incoming-response, error-code>, void>
     * - ret->is_err: outer result (void on error case)
     * - ret->val.ok: inner result<incoming-response, error-code>
     *   - ret->val.ok.is_err: inner error flag
     *   - ret->val.ok.val.ok: incoming-response handle
     *   - ret->val.ok.val.err: error-code
     */
    ret->is_err = false;  /* Outer result always succeeds */
    if (fr->has_error) {
        /* Inner result is error */
        ret->val.ok.is_err = true;
        ret->val.ok.val.err = fr->error;
    } else {
        /* Inner result is response */
        ret->val.ok.is_err = false;
        ret->val.ok.val.ok.__handle = fr->response_handle;
    }
    return true;
}

void wasi_http_types_future_incoming_response_drop_own(
    wasi_http_types_own_future_incoming_response_t handle
) {
    http_future_incoming_response_resource_t *fr = get_future_response(handle.__handle);
    if (fr) {
        free(fr);
        future_response_table[handle.__handle] = NULL;
    }
}

void wasi_http_types_future_incoming_response_drop_borrow(
    wasi_http_types_borrow_future_incoming_response_t handle
) {
    (void)handle;
}

wasi_http_types_borrow_future_incoming_response_t wasi_http_types_borrow_future_incoming_response(
    wasi_http_types_own_future_incoming_response_t handle
) {
    return (wasi_http_types_borrow_future_incoming_response_t){ handle.__handle };
}

/* ============================================================================
 * Phase 3: HTTP/1.1 Client Implementation
 * ============================================================================
 */

#ifndef WASI_SAFE_MODE
/* Network code only compiled when not in safe mode */

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

/* HTTP connection state */
typedef struct {
    int fd;
    bool connected;
    char *host;
    uint16_t port;
} http_connection_t;

/* Parse authority into host and port */
static bool parse_authority(const char *authority, size_t len,
                           char **host_out, uint16_t *port_out) {
    if (!authority || len == 0) {
        return false;
    }

    /* Find port separator (last colon, accounting for IPv6) */
    const char *port_sep = NULL;
    bool in_ipv6 = false;
    for (size_t i = 0; i < len; i++) {
        if (authority[i] == '[') in_ipv6 = true;
        else if (authority[i] == ']') in_ipv6 = false;
        else if (authority[i] == ':' && !in_ipv6) {
            port_sep = &authority[i];
        }
    }

    size_t host_len;
    uint16_t port = 80;  /* Default HTTP port */

    if (port_sep) {
        host_len = (size_t)(port_sep - authority);
        port = (uint16_t)atoi(port_sep + 1);
        if (port == 0) port = 80;
    } else {
        host_len = len;
    }

    char *host = malloc(host_len + 1);
    if (!host) return false;
    memcpy(host, authority, host_len);
    host[host_len] = '\0';

    *host_out = host;
    *port_out = port;
    return true;
}

/* Connect to HTTP server */
static int http_connect(const char *host, uint16_t port, uint64_t timeout_ns) {
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    struct addrinfo *res;
    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) {
        return -1;  /* DNS error */
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    /* Set non-blocking for timeout support */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int connect_result = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (connect_result < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    if (connect_result < 0) {
        /* Wait for connection with timeout */
        int timeout_ms = timeout_ns > 0 ? (int)(timeout_ns / 1000000) : 30000;
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        int poll_result = poll(&pfd, 1, timeout_ms);

        if (poll_result <= 0) {
            close(fd);
            return -1;  /* Timeout or error */
        }

        /* Check for connection error */
        int so_error;
        socklen_t so_len = sizeof(so_error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len);
        if (so_error != 0) {
            close(fd);
            return -1;
        }
    }

    /* Set back to blocking */
    fcntl(fd, F_SETFL, flags);

    return fd;
}

/* Get method string */
static const char *method_to_string(wasi_http_types_method_t *method) {
    switch (method->tag) {
        case WASI_HTTP_TYPES_METHOD_GET: return "GET";
        case WASI_HTTP_TYPES_METHOD_HEAD: return "HEAD";
        case WASI_HTTP_TYPES_METHOD_POST: return "POST";
        case WASI_HTTP_TYPES_METHOD_PUT: return "PUT";
        case WASI_HTTP_TYPES_METHOD_DELETE: return "DELETE";
        case WASI_HTTP_TYPES_METHOD_CONNECT: return "CONNECT";
        case WASI_HTTP_TYPES_METHOD_OPTIONS: return "OPTIONS";
        case WASI_HTTP_TYPES_METHOD_TRACE: return "TRACE";
        case WASI_HTTP_TYPES_METHOD_PATCH: return "PATCH";
        default: return "GET";
    }
}

/* Send HTTP request */
static bool http_send_request(int fd, http_outgoing_request_resource_t *req,
                             http_outgoing_body_resource_t *body) {
    /* Build request line */
    const char *method = method_to_string(&req->method);
    const char *path = req->path_with_query ? req->path_with_query : "/";

    char request_line[2048];
    int len = snprintf(request_line, sizeof(request_line),
                      "%s %s HTTP/1.1\r\n", method, path);

    /* Send request line */
    if (len < 0 || send(fd, request_line, (size_t)len, 0) != (ssize_t)len) {
        return false;
    }

    /* Send Host header */
    if (req->authority) {
        char host_header[512];
        len = snprintf(host_header, sizeof(host_header),
                      "Host: %s\r\n", req->authority);
        if (len < 0 || send(fd, host_header, (size_t)len, 0) != (ssize_t)len) {
            return false;
        }
    }

    /* Send Content-Length if we have a body */
    if (body && body->buffer_len > 0) {
        char cl_header[64];
        len = snprintf(cl_header, sizeof(cl_header),
                      "Content-Length: %zu\r\n", body->buffer_len);
        if (len < 0 || send(fd, cl_header, (size_t)len, 0) != (ssize_t)len) {
            return false;
        }
    }

    /* Send headers from Fields resource */
    http_fields_resource_t *headers = get_fields(req->headers_handle);
    if (headers) {
        for (size_t i = 0; i < headers->count; i++) {
            char header_line[4096];
            len = snprintf(header_line, sizeof(header_line),
                          "%s: %.*s\r\n",
                          headers->entries[i].name,
                          (int)headers->entries[i].value_len,
                          headers->entries[i].value);
            if (len < 0 || send(fd, header_line, (size_t)len, 0) != (ssize_t)len) {
                return false;
            }
        }
    }

    /* End headers */
    if (send(fd, "\r\n", 2, 0) != 2) {
        return false;
    }

    /* Send body if present */
    if (body && body->buffer_len > 0) {
        size_t sent = 0;
        while (sent < body->buffer_len) {
            ssize_t n = send(fd, body->buffer + sent, body->buffer_len - sent, 0);
            if (n <= 0) return false;
            sent += (size_t)n;  /* Safe: n > 0 checked above */
        }
    }

    return true;
}

/* Read a line from socket (up to \r\n) */
static ssize_t http_read_line(int fd, char *buf, size_t buf_size, int timeout_ms) {
    size_t pos = 0;

    while (pos < buf_size - 1) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int poll_result = poll(&pfd, 1, timeout_ms);
        if (poll_result <= 0) {
            return -1;  /* Timeout or error */
        }

        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0) {
            return -1;
        }

        buf[pos++] = c;

        /* Check for end of line */
        if (pos >= 2 && buf[pos-2] == '\r' && buf[pos-1] == '\n') {
            buf[pos-2] = '\0';
            return (ssize_t)(pos - 2);
        }
    }

    return -1;  /* Line too long */
}

/* Parse HTTP response */
static bool http_parse_response(int fd, int timeout_ms,
                               uint16_t *status_code,
                               http_fields_resource_t *headers,
                               uint8_t **body_out, size_t *body_len_out) {
    char line[8192];

    /* Read status line */
    if (http_read_line(fd, line, sizeof(line), timeout_ms) < 0) {
        return false;
    }

    /* Parse status line: HTTP/1.x NNN Reason */
    if (strncmp(line, "HTTP/1.", 7) != 0) {
        return false;
    }

    char *status_start = strchr(line, ' ');
    if (!status_start) return false;
    *status_code = (uint16_t)atoi(status_start + 1);

    /* Read headers */
    size_t content_length = 0;
    bool chunked = false;

    while (1) {
        ssize_t len = http_read_line(fd, line, sizeof(line), timeout_ms);
        if (len < 0) return false;
        if (len == 0) break;  /* Empty line = end of headers */

        /* Parse header */
        char *colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        char *value = colon + 1;
        while (*value == ' ') value++;

        /* Add to headers */
        if (headers) {
            size_t name_len = strlen(line);
            size_t value_len = strlen(value);

            if (!fields_ensure_capacity(headers, headers->count + 1)) {
                continue;
            }

            char *name = malloc(name_len + 1);
            if (name) {
                for (size_t i = 0; i < name_len; i++) {
                    name[i] = (char)tolower((unsigned char)line[i]);
                }
                name[name_len] = '\0';

                uint8_t *val = malloc(value_len);
                if (val) {
                    memcpy(val, value, value_len);
                    headers->entries[headers->count].name = name;
                    headers->entries[headers->count].name_len = name_len;
                    headers->entries[headers->count].value = val;
                    headers->entries[headers->count].value_len = value_len;
                    headers->count++;
                } else {
                    free(name);
                }
            }
        }

        /* Check for Content-Length */
        if (strcasecmp(line, "content-length") == 0) {
            content_length = (size_t)atol(value);
        }
        /* Check for Transfer-Encoding: chunked */
        else if (strcasecmp(line, "transfer-encoding") == 0 &&
                 strcasestr(value, "chunked") != NULL) {
            chunked = true;
        }
    }

    /* Read body */
    *body_out = NULL;
    *body_len_out = 0;

    if (chunked) {
        /* Parse chunked encoding */
        uint8_t *body = NULL;
        size_t body_len = 0;
        size_t body_cap = 0;

        while (1) {
            /* Read chunk size */
            if (http_read_line(fd, line, sizeof(line), timeout_ms) < 0) {
                free(body);
                return false;
            }

            size_t chunk_size = (size_t)strtoul(line, NULL, 16);
            if (chunk_size == 0) break;  /* Final chunk */

            /* Ensure buffer space */
            if (body_len + chunk_size > body_cap) {
                size_t new_cap = body_cap == 0 ? 4096 : body_cap * 2;
                while (new_cap < body_len + chunk_size) new_cap *= 2;
                uint8_t *new_body = realloc(body, new_cap);
                if (!new_body) {
                    free(body);
                    return false;
                }
                body = new_body;
                body_cap = new_cap;
            }

            /* Read chunk data */
            size_t read = 0;
            while (read < chunk_size) {
                struct pollfd pfd = { .fd = fd, .events = POLLIN };
                if (poll(&pfd, 1, timeout_ms) <= 0) {
                    free(body);
                    return false;
                }
                ssize_t n = recv(fd, body + body_len + read, chunk_size - read, 0);
                if (n <= 0) {
                    free(body);
                    return false;
                }
                read += (size_t)n;  /* Safe: n > 0 checked above */
            }
            body_len += chunk_size;

            /* Read trailing \r\n */
            if (http_read_line(fd, line, sizeof(line), timeout_ms) < 0) {
                free(body);
                return false;
            }
        }

        /* Read trailing headers (if any) and final \r\n */
        while (1) {
            ssize_t len = http_read_line(fd, line, sizeof(line), timeout_ms);
            if (len <= 0) break;
        }

        *body_out = body;
        *body_len_out = body_len;
    }
    else if (content_length > 0) {
        /* Read fixed-length body */
        uint8_t *body = malloc(content_length);
        if (!body) return false;

        size_t read = 0;
        while (read < content_length) {
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            if (poll(&pfd, 1, timeout_ms) <= 0) {
                free(body);
                return false;
            }
            ssize_t n = recv(fd, body + read, content_length - read, 0);
            if (n <= 0) {
                free(body);
                return false;
            }
            read += (size_t)n;  /* Safe: n > 0 checked above */
        }

        *body_out = body;
        *body_len_out = content_length;
    }

    return true;
}

/* Create incoming response from parsed data */
static int32_t create_incoming_response(uint16_t status_code,
                                        http_fields_resource_t *headers,
                                        uint8_t *body, size_t body_len) {
    /* Allocate response */
    if (next_incoming_response_handle >= MAX_HTTP_HANDLES) {
        return -1;
    }

    http_incoming_response_resource_t *resp = calloc(1, sizeof(http_incoming_response_resource_t));
    if (!resp) return -1;

    resp->status_code = status_code;
    resp->body_consumed = false;

    /* Create headers handle */
    int32_t headers_handle = alloc_fields();
    if (headers_handle > 0 && headers) {
        http_fields_resource_t *resp_headers = get_fields(headers_handle);
        if (resp_headers) {
            /* Copy headers */
            for (size_t i = 0; i < headers->count; i++) {
                fields_ensure_capacity(resp_headers, resp_headers->count + 1);
                char *name = malloc(headers->entries[i].name_len + 1);
                uint8_t *value = malloc(headers->entries[i].value_len);
                if (name && value) {
                    memcpy(name, headers->entries[i].name, headers->entries[i].name_len);
                    name[headers->entries[i].name_len] = '\0';
                    memcpy(value, headers->entries[i].value, headers->entries[i].value_len);
                    resp_headers->entries[resp_headers->count].name = name;
                    resp_headers->entries[resp_headers->count].name_len = headers->entries[i].name_len;
                    resp_headers->entries[resp_headers->count].value = value;
                    resp_headers->entries[resp_headers->count].value_len = headers->entries[i].value_len;
                    resp_headers->count++;
                } else {
                    free(name);
                    free(value);
                }
            }
            resp_headers->immutable = true;
        }
    }
    resp->headers_handle = headers_handle;

    /* Create body handle */
    int32_t body_handle = alloc_incoming_body();
    if (body_handle > 0) {
        http_incoming_body_resource_t *resp_body = get_incoming_body(body_handle);
        if (resp_body) {
            incoming_body_set_data(resp_body, body, body_len);
        }
    }
    resp->body_handle = body_handle;

    int32_t handle = next_incoming_response_handle++;
    incoming_response_table[handle] = resp;
    return handle;
}

#endif /* !WASI_SAFE_MODE - end of network code */

/* Main HTTP outgoing handler */
bool wasi_http_outgoing_handler_handle(
    wasi_http_outgoing_handler_own_outgoing_request_t request,
    wasi_http_outgoing_handler_own_request_options_t *maybe_options,
    wasi_http_outgoing_handler_own_future_incoming_response_t *ret,
    wasi_http_outgoing_handler_error_code_t *err
) {
#ifdef WASI_SAFE_MODE
    /* In safe mode, return internal error - no network access */
    err->tag = WASI_HTTP_TYPES_ERROR_CODE_INTERNAL_ERROR;
    return false;
#else
    http_outgoing_request_resource_t *req = get_outgoing_request(request.__handle);
    if (!req) {
        err->tag = WASI_HTTP_TYPES_ERROR_CODE_INTERNAL_ERROR;
        return false;
    }

    /* Get timeouts */
    uint64_t connect_timeout = 30000000000ULL;  /* 30s default */
    uint64_t read_timeout = 60000000000ULL;     /* 60s default */

    if (maybe_options) {
        http_request_options_resource_t *opts = get_request_options(maybe_options->__handle);
        if (opts) {
            if (opts->connect_timeout_set) {
                connect_timeout = opts->connect_timeout_ns;
            }
            if (opts->first_byte_timeout_set) {
                read_timeout = opts->first_byte_timeout_ns;
            }
        }
    }

    /* Parse authority */
    if (!req->authority) {
        err->tag = WASI_HTTP_TYPES_ERROR_CODE_HTTP_REQUEST_URI_INVALID;
        return false;
    }

    char *host = NULL;
    uint16_t port = 80;
    if (!parse_authority(req->authority, req->authority_len, &host, &port)) {
        err->tag = WASI_HTTP_TYPES_ERROR_CODE_HTTP_REQUEST_URI_INVALID;
        return false;
    }

    /* Adjust port for scheme */
    if (req->scheme_set && req->scheme.tag == WASI_HTTP_TYPES_SCHEME_HTTPS) {
        if (port == 80) port = 443;
        /* Note: HTTPS not implemented yet - would need TLS */
        free(host);
        err->tag = WASI_HTTP_TYPES_ERROR_CODE_HTTP_PROTOCOL_ERROR;
        return false;
    }

    /* Connect */
    int fd = http_connect(host, port, connect_timeout);
    free(host);

    if (fd < 0) {
        err->tag = WASI_HTTP_TYPES_ERROR_CODE_CONNECTION_REFUSED;
        return false;
    }

    /* Get body if any */
    http_outgoing_body_resource_t *body = NULL;
    if (req->body_handle > 0) {
        body = get_outgoing_body(req->body_handle);
    }

    /* Send request */
    if (!http_send_request(fd, req, body)) {
        close(fd);
        err->tag = WASI_HTTP_TYPES_ERROR_CODE_HTTP_REQUEST_BODY_SIZE;
        return false;
    }

    /* Parse response */
    uint16_t status_code;
    http_fields_resource_t temp_headers = {0};
    uint8_t *resp_body = NULL;
    size_t resp_body_len = 0;

    int timeout_ms = (int)(read_timeout / 1000000);
    if (!http_parse_response(fd, timeout_ms, &status_code, &temp_headers,
                            &resp_body, &resp_body_len)) {
        close(fd);
        /* Free temp headers */
        for (size_t i = 0; i < temp_headers.count; i++) {
            free(temp_headers.entries[i].name);
            free(temp_headers.entries[i].value);
        }
        free(temp_headers.entries);
        err->tag = WASI_HTTP_TYPES_ERROR_CODE_HTTP_RESPONSE_INCOMPLETE;
        return false;
    }

    close(fd);

    /* Create incoming response */
    int32_t response_handle = create_incoming_response(status_code, &temp_headers,
                                                       resp_body, resp_body_len);

    /* Free temp headers (data was copied) */
    for (size_t i = 0; i < temp_headers.count; i++) {
        free(temp_headers.entries[i].name);
        free(temp_headers.entries[i].value);
    }
    free(temp_headers.entries);

    if (response_handle < 0) {
        free(resp_body);
        err->tag = WASI_HTTP_TYPES_ERROR_CODE_INTERNAL_ERROR;
        return false;
    }

    /* Create future response */
    int32_t future_handle = alloc_future_response();
    if (future_handle < 0) {
        err->tag = WASI_HTTP_TYPES_ERROR_CODE_INTERNAL_ERROR;
        return false;
    }

    http_future_incoming_response_resource_t *future = get_future_response(future_handle);
    future->ready = true;
    future->has_error = false;
    future->response_handle = response_handle;

    ret->__handle = future_handle;
    return true;
#endif  /* WASI_SAFE_MODE */
}

/* TODO: Phase 4 - Incoming Handler */

/* ============================================================================
 * Memory Management Helper Functions
 * ============================================================================
 * These functions free memory allocated by the Fields API when returning
 * values to callers. They mirror the functions generated in proxy.c but
 * are implemented here for native compilation.
 */

/* Free a field value (byte array) */
void wasi_http_types_field_value_free(wasi_http_types_field_value_t *ptr) {
    if (ptr && ptr->ptr) {
        wasi_cabi_free(ptr->ptr, 1);
        ptr->ptr = NULL;
        ptr->len = 0;
    }
}

/* Free a field key (string) */
void wasi_http_types_field_key_free(wasi_http_types_field_key_t *ptr) {
    if (ptr && ptr->ptr) {
        wasi_cabi_free(ptr->ptr, 1);
        ptr->ptr = NULL;
        ptr->len = 0;
    }
}

/* Free a (key, value) tuple */
void proxy_tuple2_field_key_field_value_free(proxy_tuple2_field_key_field_value_t *ptr) {
    if (ptr) {
        wasi_http_types_field_key_free(&ptr->f0);
        wasi_http_types_field_value_free(&ptr->f1);
    }
}

/* Free a list of field values */
void proxy_list_field_value_free(proxy_list_field_value_t *ptr) {
    if (ptr && ptr->len > 0 && ptr->ptr) {
        for (size_t i = 0; i < ptr->len; i++) {
            wasi_http_types_field_value_free(&ptr->ptr[i]);
        }
        wasi_cabi_free(ptr->ptr, WASI_ALIGNOF(wasi_http_types_field_value_t));
        ptr->ptr = NULL;
        ptr->len = 0;
    }
}

/* Free a list of (key, value) tuples */
void proxy_list_tuple2_field_key_field_value_free(proxy_list_tuple2_field_key_field_value_t *ptr) {
    if (ptr && ptr->len > 0 && ptr->ptr) {
        for (size_t i = 0; i < ptr->len; i++) {
            proxy_tuple2_field_key_field_value_free(&ptr->ptr[i]);
        }
        wasi_cabi_free(ptr->ptr, WASI_ALIGNOF(proxy_tuple2_field_key_field_value_t));
        ptr->ptr = NULL;
        ptr->len = 0;
    }
}

/* ============================================================================
 * Phase 4: Incoming Handler (Server/Proxy)
 * ============================================================================
 * This is the exported handler that gets called when the WASI runtime receives
 * an HTTP request for this component in the proxy world.
 *
 * The handler receives an incoming request and a response outparam, and must
 * set a response (or error) on the outparam before returning.
 */

/* Default handler implementation - returns 501 Not Implemented */
void exports_wasi_http_incoming_handler_handle(
    exports_wasi_http_incoming_handler_own_incoming_request_t request,
    exports_wasi_http_incoming_handler_own_response_outparam_t response_out
) {
    /*
     * This is a default implementation that returns 501 Not Implemented.
     * Real applications would override this to handle HTTP requests.
     *
     * In a real proxy/server scenario:
     * 1. Read the incoming request (method, path, headers, body)
     * 2. Process the request (route, handle, generate response)
     * 3. Create an outgoing response with status, headers, body
     * 4. Set the response on response_out
     */

    /* Get the request to extract some info (for logging/debugging) */
    http_incoming_request_resource_t *req = get_incoming_request(request.__handle);
    (void)req;  /* Unused in default implementation */

    /* Create a simple 501 Not Implemented response */
    wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    wasi_http_types_own_outgoing_response_t response =
        wasi_http_types_constructor_outgoing_response(headers);

    /* Set status to 501 Not Implemented */
    wasi_http_types_borrow_outgoing_response_t response_borrow =
        wasi_http_types_borrow_outgoing_response(response);
    wasi_http_types_method_outgoing_response_set_status_code(response_borrow, 501);

    /* Set response on outparam */
    wasi_http_types_result_own_outgoing_response_error_code_t result;
    result.is_err = false;
    result.val.ok = response;
    wasi_http_types_static_response_outparam_set(response_out, &result);

    /* Clean up the request */
    wasi_http_types_incoming_request_drop_own(request);
}

/* ============================================================================
 * Helper functions for building incoming handler applications
 * ============================================================================
 */

/* Create an incoming request programmatically (useful for testing) */
wasi_http_types_own_incoming_request_t http_create_incoming_request(
    wasi_http_types_method_t method,
    const char *path,
    const char *authority,
    wasi_http_types_own_fields_t headers
) {
    int32_t handle = alloc_incoming_request();
    if (handle <= 0) {
        return (wasi_http_types_own_incoming_request_t){ -1 };
    }

    http_incoming_request_resource_t *req = get_incoming_request(handle);
    if (!req) {
        return (wasi_http_types_own_incoming_request_t){ -1 };
    }

    /* Set method */
    req->method = method;

    /* Set path */
    if (path) {
        size_t path_len = strlen(path);
        req->path_with_query = malloc(path_len + 1);
        if (req->path_with_query) {
            memcpy(req->path_with_query, path, path_len);
            req->path_with_query[path_len] = '\0';
            req->path_len = path_len;
        }
    }

    /* Set authority */
    if (authority) {
        size_t auth_len = strlen(authority);
        req->authority = malloc(auth_len + 1);
        if (req->authority) {
            memcpy(req->authority, authority, auth_len);
            req->authority[auth_len] = '\0';
            req->authority_len = auth_len;
        }
    }

    /* Set headers */
    req->headers_handle = headers.__handle;

    /* Default scheme is HTTP */
    req->scheme.tag = WASI_HTTP_TYPES_SCHEME_HTTP;
    req->scheme_set = true;

    return (wasi_http_types_own_incoming_request_t){ handle };
}

/* Create a response outparam (useful for testing the handler) */
wasi_http_types_own_response_outparam_t http_create_response_outparam(void) {
    int32_t handle = alloc_response_outparam();
    return (wasi_http_types_own_response_outparam_t){ handle };
}

/* Check if response was set on outparam */
bool http_response_outparam_is_set(wasi_http_types_borrow_response_outparam_t outparam) {
    http_response_outparam_resource_t *op = get_response_outparam(outparam.__handle);
    return op ? op->set : false;
}

/* Get the response from outparam (if set and not error) */
bool http_response_outparam_get_response(
    wasi_http_types_borrow_response_outparam_t outparam,
    wasi_http_types_own_outgoing_response_t *response
) {
    http_response_outparam_resource_t *op = get_response_outparam(outparam.__handle);
    if (!op || !op->set || op->is_error) {
        return false;
    }
    *response = op->response;
    return true;
}
