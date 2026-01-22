/**
 * Tests for WASI Sockets Implementation
 *
 * Run: make test-sockets
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>

/* Test framework macros */
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* Include the generated bindings header */
#include "../build/c-bindings/sockets/imports.h"

/* ============================================================================
 * Test: Instance network returns valid handle
 * ============================================================================
 */
TEST(instance_network) {
    wasi_sockets_instance_network_own_network_t network =
        wasi_sockets_instance_network_instance_network();

    assert(network.__handle > 0);

    wasi_sockets_network_network_drop_own((wasi_sockets_network_own_network_t){ network.__handle });
}

/* ============================================================================
 * Test: Create TCP socket IPv4
 * ============================================================================
 */
TEST(create_tcp_socket_ipv4) {
    wasi_sockets_tcp_create_socket_own_tcp_socket_t sock;
    wasi_sockets_tcp_create_socket_error_code_t err;

    bool ok = wasi_sockets_tcp_create_socket_create_tcp_socket(
        WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4, &sock, &err);

    assert(ok);
    assert(sock.__handle > 0);

    wasi_sockets_tcp_tcp_socket_drop_own((wasi_sockets_tcp_own_tcp_socket_t){ sock.__handle });
}

/* ============================================================================
 * Test: Create TCP socket IPv6
 * ============================================================================
 */
TEST(create_tcp_socket_ipv6) {
    wasi_sockets_tcp_create_socket_own_tcp_socket_t sock;
    wasi_sockets_tcp_create_socket_error_code_t err;

    bool ok = wasi_sockets_tcp_create_socket_create_tcp_socket(
        WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV6, &sock, &err);

    assert(ok);
    assert(sock.__handle > 0);

    /* Verify address family */
    wasi_sockets_tcp_borrow_tcp_socket_t borrow = { sock.__handle };
    wasi_sockets_tcp_ip_address_family_t family =
        wasi_sockets_tcp_method_tcp_socket_address_family(borrow);
    assert(family == WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV6);

    wasi_sockets_tcp_tcp_socket_drop_own((wasi_sockets_tcp_own_tcp_socket_t){ sock.__handle });
}

/* ============================================================================
 * Test: TCP socket bind and listen
 * ============================================================================
 */
TEST(tcp_bind_and_listen) {
    wasi_sockets_tcp_create_socket_own_tcp_socket_t sock;
    wasi_sockets_tcp_create_socket_error_code_t err;

    bool ok = wasi_sockets_tcp_create_socket_create_tcp_socket(
        WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    assert(ok);

    wasi_sockets_instance_network_own_network_t network =
        wasi_sockets_instance_network_instance_network();

    /* Bind to localhost:0 (let OS choose port) */
    wasi_sockets_tcp_ip_socket_address_t local_addr;
    local_addr.tag = WASI_SOCKETS_NETWORK_IP_SOCKET_ADDRESS_IPV4;
    local_addr.val.ipv4.port = 0;
    local_addr.val.ipv4.address.f0 = 127;
    local_addr.val.ipv4.address.f1 = 0;
    local_addr.val.ipv4.address.f2 = 0;
    local_addr.val.ipv4.address.f3 = 1;

    wasi_sockets_tcp_borrow_tcp_socket_t borrow = { sock.__handle };
    wasi_sockets_tcp_borrow_network_t net_borrow = { network.__handle };

    ok = wasi_sockets_tcp_method_tcp_socket_start_bind(borrow, net_borrow, &local_addr, &err);
    assert(ok);

    ok = wasi_sockets_tcp_method_tcp_socket_finish_bind(borrow, &err);
    assert(ok);

    /* Start listening */
    ok = wasi_sockets_tcp_method_tcp_socket_start_listen(borrow, &err);
    assert(ok);

    ok = wasi_sockets_tcp_method_tcp_socket_finish_listen(borrow, &err);
    assert(ok);

    /* Verify is_listening */
    assert(wasi_sockets_tcp_method_tcp_socket_is_listening(borrow));

    /* Get local address */
    wasi_sockets_tcp_ip_socket_address_t bound_addr;
    ok = wasi_sockets_tcp_method_tcp_socket_local_address(borrow, &bound_addr, &err);
    assert(ok);
    assert(bound_addr.tag == WASI_SOCKETS_NETWORK_IP_SOCKET_ADDRESS_IPV4);
    assert(bound_addr.val.ipv4.port > 0);  /* Port was assigned */

    wasi_sockets_tcp_tcp_socket_drop_own((wasi_sockets_tcp_own_tcp_socket_t){ sock.__handle });
    wasi_sockets_network_network_drop_own((wasi_sockets_network_own_network_t){ network.__handle });
}

/* ============================================================================
 * Test: TCP socket options
 * ============================================================================
 */
TEST(tcp_socket_options) {
    wasi_sockets_tcp_create_socket_own_tcp_socket_t sock;
    wasi_sockets_tcp_create_socket_error_code_t err;

    bool ok = wasi_sockets_tcp_create_socket_create_tcp_socket(
        WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    assert(ok);

    wasi_sockets_tcp_borrow_tcp_socket_t borrow = { sock.__handle };

    /* Test keep-alive */
    ok = wasi_sockets_tcp_method_tcp_socket_set_keep_alive_enabled(borrow, true, &err);
    assert(ok);

    bool enabled;
    ok = wasi_sockets_tcp_method_tcp_socket_keep_alive_enabled(borrow, &enabled, &err);
    assert(ok);
    assert(enabled);

    /* Test receive buffer size */
    uint64_t size;
    ok = wasi_sockets_tcp_method_tcp_socket_receive_buffer_size(borrow, &size, &err);
    assert(ok);
    assert(size > 0);

    /* Test send buffer size */
    ok = wasi_sockets_tcp_method_tcp_socket_send_buffer_size(borrow, &size, &err);
    assert(ok);
    assert(size > 0);

    wasi_sockets_tcp_tcp_socket_drop_own((wasi_sockets_tcp_own_tcp_socket_t){ sock.__handle });
}

/* ============================================================================
 * Test: Create UDP socket
 * ============================================================================
 */
TEST(create_udp_socket) {
    wasi_sockets_udp_create_socket_own_udp_socket_t sock;
    wasi_sockets_udp_create_socket_error_code_t err;

    bool ok = wasi_sockets_udp_create_socket_create_udp_socket(
        WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4, &sock, &err);

    assert(ok);
    assert(sock.__handle > 0);

    wasi_sockets_udp_udp_socket_drop_own((wasi_sockets_udp_own_udp_socket_t){ sock.__handle });
}

/* ============================================================================
 * Test: UDP socket bind
 * ============================================================================
 */
TEST(udp_bind) {
    wasi_sockets_udp_create_socket_own_udp_socket_t sock;
    wasi_sockets_udp_create_socket_error_code_t err;

    bool ok = wasi_sockets_udp_create_socket_create_udp_socket(
        WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    assert(ok);

    wasi_sockets_instance_network_own_network_t network =
        wasi_sockets_instance_network_instance_network();

    wasi_sockets_udp_ip_socket_address_t local_addr;
    local_addr.tag = WASI_SOCKETS_NETWORK_IP_SOCKET_ADDRESS_IPV4;
    local_addr.val.ipv4.port = 0;
    local_addr.val.ipv4.address.f0 = 127;
    local_addr.val.ipv4.address.f1 = 0;
    local_addr.val.ipv4.address.f2 = 0;
    local_addr.val.ipv4.address.f3 = 1;

    wasi_sockets_udp_borrow_udp_socket_t borrow = { sock.__handle };
    wasi_sockets_udp_borrow_network_t net_borrow = { network.__handle };

    ok = wasi_sockets_udp_method_udp_socket_start_bind(borrow, net_borrow, &local_addr, &err);
    assert(ok);

    ok = wasi_sockets_udp_method_udp_socket_finish_bind(borrow, &err);
    assert(ok);

    /* Get local address */
    wasi_sockets_udp_ip_socket_address_t bound_addr;
    ok = wasi_sockets_udp_method_udp_socket_local_address(borrow, &bound_addr, &err);
    assert(ok);
    assert(bound_addr.tag == WASI_SOCKETS_NETWORK_IP_SOCKET_ADDRESS_IPV4);
    assert(bound_addr.val.ipv4.port > 0);

    wasi_sockets_udp_udp_socket_drop_own((wasi_sockets_udp_own_udp_socket_t){ sock.__handle });
    wasi_sockets_network_network_drop_own((wasi_sockets_network_own_network_t){ network.__handle });
}

/* ============================================================================
 * Test: UDP socket options
 * ============================================================================
 */
TEST(udp_socket_options) {
    wasi_sockets_udp_create_socket_own_udp_socket_t sock;
    wasi_sockets_udp_create_socket_error_code_t err;

    bool ok = wasi_sockets_udp_create_socket_create_udp_socket(
        WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    assert(ok);

    wasi_sockets_udp_borrow_udp_socket_t borrow = { sock.__handle };

    /* Test hop limit */
    uint8_t ttl;
    ok = wasi_sockets_udp_method_udp_socket_unicast_hop_limit(borrow, &ttl, &err);
    assert(ok);
    assert(ttl > 0);

    ok = wasi_sockets_udp_method_udp_socket_set_unicast_hop_limit(borrow, 64, &err);
    assert(ok);

    ok = wasi_sockets_udp_method_udp_socket_unicast_hop_limit(borrow, &ttl, &err);
    assert(ok);
    assert(ttl == 64);

    wasi_sockets_udp_udp_socket_drop_own((wasi_sockets_udp_own_udp_socket_t){ sock.__handle });
}

/* ============================================================================
 * Test: DNS resolution (localhost)
 * ============================================================================
 */
TEST(dns_resolve_localhost) {
    wasi_sockets_instance_network_own_network_t network =
        wasi_sockets_instance_network_instance_network();

    imports_string_t name;
    name.ptr = (uint8_t *)"localhost";
    name.len = 9;

    wasi_sockets_ip_name_lookup_own_resolve_address_stream_t stream;
    wasi_sockets_ip_name_lookup_error_code_t err;

    wasi_sockets_ip_name_lookup_borrow_network_t net_borrow = { network.__handle };

    bool ok = wasi_sockets_ip_name_lookup_resolve_addresses(net_borrow, &name, &stream, &err);
    assert(ok);
    assert(stream.__handle > 0);

    /* Get first address */
    wasi_sockets_ip_name_lookup_borrow_resolve_address_stream_t stream_borrow = { stream.__handle };
    wasi_sockets_ip_name_lookup_option_ip_address_t addr;

    ok = wasi_sockets_ip_name_lookup_method_resolve_address_stream_resolve_next_address(
        stream_borrow, &addr, &err);
    assert(ok);
    assert(addr.is_some);

    /* localhost should resolve to 127.0.0.1 or ::1 */
    if (addr.val.tag == WASI_SOCKETS_NETWORK_IP_ADDRESS_IPV4) {
        assert(addr.val.val.ipv4.f0 == 127);
        assert(addr.val.val.ipv4.f1 == 0);
        assert(addr.val.val.ipv4.f2 == 0);
        assert(addr.val.val.ipv4.f3 == 1);
    }

    wasi_sockets_ip_name_lookup_resolve_address_stream_drop_own(stream);
    wasi_sockets_network_network_drop_own((wasi_sockets_network_own_network_t){ network.__handle });
}

/* ============================================================================
 * Test: TCP socket subscribe returns pollable
 * ============================================================================
 */
TEST(tcp_socket_subscribe) {
    wasi_sockets_tcp_create_socket_own_tcp_socket_t sock;
    wasi_sockets_tcp_create_socket_error_code_t err;

    bool ok = wasi_sockets_tcp_create_socket_create_tcp_socket(
        WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    assert(ok);

    wasi_sockets_tcp_borrow_tcp_socket_t borrow = { sock.__handle };
    wasi_sockets_tcp_own_pollable_t pollable =
        wasi_sockets_tcp_method_tcp_socket_subscribe(borrow);

    assert(pollable.__handle > 0);

    wasi_io_poll_pollable_drop_own((wasi_io_poll_own_pollable_t){ pollable.__handle });
    wasi_sockets_tcp_tcp_socket_drop_own((wasi_sockets_tcp_own_tcp_socket_t){ sock.__handle });
}

/* ============================================================================
 * Main test runner
 * ============================================================================
 */
int run_sockets_tests(void) {
    tests_passed = 0;
    tests_failed = 0;

    printf("Running WASI Sockets tests...\n");

    RUN_TEST(instance_network);
    RUN_TEST(create_tcp_socket_ipv4);
    RUN_TEST(create_tcp_socket_ipv6);
    RUN_TEST(tcp_bind_and_listen);
    RUN_TEST(tcp_socket_options);
    RUN_TEST(create_udp_socket);
    RUN_TEST(udp_bind);
    RUN_TEST(udp_socket_options);
    RUN_TEST(dns_resolve_localhost);
    RUN_TEST(tcp_socket_subscribe);

    printf("\nSockets tests passed: %d\n", tests_passed);
    printf("Sockets tests failed: %d\n", tests_failed);

    return tests_failed;
}

/* Allow standalone execution */
#ifndef TEST_RUNNER_MODE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_sockets_tests();
}
#endif
