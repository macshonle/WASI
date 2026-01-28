/**
 * WASI Sockets Implementation
 *
 * This file implements the wasi:sockets interfaces for macOS (UNIX) and GNU/Linux.
 *
 * Interfaces implemented:
 *   - wasi:sockets/instance-network@0.2.0  - Network instance
 *   - wasi:sockets/network@0.2.0           - Network resource
 *   - wasi:sockets/tcp@0.2.0               - TCP sockets
 *   - wasi:sockets/tcp-create-socket@0.2.0 - TCP socket creation
 *   - wasi:sockets/udp@0.2.0               - UDP sockets
 *   - wasi:sockets/udp-create-socket@0.2.0 - UDP socket creation
 *   - wasi:sockets/ip-name-lookup@0.2.0    - DNS resolution
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
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#include "platform/platform.h"
#include "common.h"

/* Include the generated bindings header */
#include "../../build/c-bindings/sockets/imports.h"

WASI_ABI_CHECK_PTR_LEN_TYPE(imports_string_t);
WASI_ABI_CHECK_PTR_LEN_TYPE(imports_list_u8_t);
WASI_ABI_CHECK_PTR_LEN_TYPE(imports_list_u32_t);
WASI_ABI_CHECK_PTR_LEN_TYPE(wasi_sockets_udp_list_incoming_datagram_t);
WASI_ABI_CHECK_PTR_LEN_TYPE(wasi_sockets_udp_list_outgoing_datagram_t);

/* ============================================================================
 * macOS Compatibility
 * ============================================================================
 * macOS doesn't have SOCK_CLOEXEC, SOCK_NONBLOCK, or accept4().
 * We provide portable alternatives using fcntl().
 */

#ifndef SOCK_CLOEXEC
    #define SOCK_CLOEXEC 0
    #define NEED_SOCKET_CLOEXEC_WORKAROUND
#endif

#ifndef SOCK_NONBLOCK
    #define SOCK_NONBLOCK 0
#endif

/* Set close-on-exec flag for a file descriptor */
static inline int set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

/* Set non-blocking flag for a file descriptor */
static inline int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Portable accept with CLOEXEC and NONBLOCK flags */
static inline int portable_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
#ifdef __linux__
    return accept4(sockfd, addr, addrlen, flags);
#else
    (void)flags;  /* We'll set flags manually */
    int fd = accept(sockfd, addr, addrlen);
    if (fd < 0) return fd;
    if (set_cloexec(fd) < 0 || set_nonblock(fd) < 0) {
        close(fd);
        return -1;
    }
    return fd;
#endif
}

/* External declarations from io.c */
extern int32_t wasi_io_poll_create_fd_pollable(int fd, bool for_write);
extern int32_t wasi_io_streams_create_input_stream(int fd, bool owns_fd);
extern int32_t wasi_io_streams_create_output_stream(int fd, bool owns_fd);

/* ============================================================================
 * Handle Management
 * ============================================================================
 */

#define MAX_SOCKET_HANDLES 1024

typedef enum {
    SOCKET_STATE_UNBOUND = 0,
    SOCKET_STATE_BOUND,
    SOCKET_STATE_LISTENING,
    SOCKET_STATE_CONNECTING,
    SOCKET_STATE_CONNECTED,
    SOCKET_STATE_CLOSED
} socket_state_t;

typedef struct {
    int fd;
    int family;  /* AF_INET or AF_INET6 */
    int type;    /* SOCK_STREAM or SOCK_DGRAM */
    socket_state_t state;
    uint64_t backlog;
} wasi_socket_resource_t;

typedef struct {
    int fd;
    struct sockaddr_storage remote_addr;
    socklen_t remote_addr_len;
    bool connected;  /* For UDP connected mode */
} wasi_udp_stream_resource_t;

typedef struct {
    struct addrinfo *results;
    struct addrinfo *current;
} wasi_resolve_stream_resource_t;

static wasi_socket_resource_t *tcp_socket_table[MAX_SOCKET_HANDLES];
static wasi_socket_resource_t *udp_socket_table[MAX_SOCKET_HANDLES];
static wasi_udp_stream_resource_t *udp_in_stream_table[MAX_SOCKET_HANDLES];
static wasi_udp_stream_resource_t *udp_out_stream_table[MAX_SOCKET_HANDLES];
static wasi_resolve_stream_resource_t *resolve_stream_table[MAX_SOCKET_HANDLES];

static int32_t next_tcp_handle = 1;
static int32_t next_udp_handle = 1;
static int32_t next_udp_in_stream_handle = 1;
static int32_t next_udp_out_stream_handle = 1;
static int32_t next_resolve_handle = 1;
static int32_t next_network_handle = 1;

/* Network handle - just a dummy for WASI compatibility */
static bool network_initialized = false;

/* ============================================================================
 * Error Code Conversion
 * ============================================================================
 */

static wasi_sockets_network_error_code_t errno_to_socket_error(int err) {
    switch (err) {
        case EACCES:
        case EPERM:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_ACCESS_DENIED;
        case EOPNOTSUPP:
        case EPROTONOSUPPORT:
        case EAFNOSUPPORT:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_SUPPORTED;
        case EINVAL:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_ARGUMENT;
        case ENOMEM:
        case ENOBUFS:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        case ETIMEDOUT:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_TIMEOUT;
        case EALREADY:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_CONCURRENCY_CONFLICT;
        case EAGAIN:
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            return WASI_SOCKETS_NETWORK_ERROR_CODE_WOULD_BLOCK;
        case EADDRINUSE:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_ADDRESS_IN_USE;
        case EADDRNOTAVAIL:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_ADDRESS_NOT_BINDABLE;
        case ENETUNREACH:
        case EHOSTUNREACH:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_REMOTE_UNREACHABLE;
        case ECONNREFUSED:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_CONNECTION_REFUSED;
        case ECONNRESET:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_CONNECTION_RESET;
        case ECONNABORTED:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_CONNECTION_ABORTED;
        case EMSGSIZE:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_DATAGRAM_TOO_LARGE;
        default:
            return WASI_SOCKETS_NETWORK_ERROR_CODE_UNKNOWN;
    }
}

/* ============================================================================
 * Address Conversion Helpers
 * ============================================================================
 */

static void wasi_addr_to_sockaddr(
    wasi_sockets_network_ip_socket_address_t *addr,
    struct sockaddr_storage *ss,
    socklen_t *len
) {
    memset(ss, 0, sizeof(*ss));

    if (addr->tag == WASI_SOCKETS_NETWORK_IP_SOCKET_ADDRESS_IPV4) {
        struct sockaddr_in *sin = (struct sockaddr_in *)ss;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(addr->val.ipv4.port);
        sin->sin_addr.s_addr = htonl(
            ((uint32_t)addr->val.ipv4.address.f0 << 24) |
            ((uint32_t)addr->val.ipv4.address.f1 << 16) |
            ((uint32_t)addr->val.ipv4.address.f2 << 8) |
            (uint32_t)addr->val.ipv4.address.f3
        );
        *len = sizeof(struct sockaddr_in);
    } else {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons(addr->val.ipv6.port);
        sin6->sin6_flowinfo = htonl(addr->val.ipv6.flow_info);
        sin6->sin6_scope_id = htonl(addr->val.ipv6.scope_id);

        uint16_t *a = (uint16_t *)&sin6->sin6_addr;
        a[0] = htons(addr->val.ipv6.address.f0);
        a[1] = htons(addr->val.ipv6.address.f1);
        a[2] = htons(addr->val.ipv6.address.f2);
        a[3] = htons(addr->val.ipv6.address.f3);
        a[4] = htons(addr->val.ipv6.address.f4);
        a[5] = htons(addr->val.ipv6.address.f5);
        a[6] = htons(addr->val.ipv6.address.f6);
        a[7] = htons(addr->val.ipv6.address.f7);

        *len = sizeof(struct sockaddr_in6);
    }
}

static void sockaddr_to_wasi_addr(
    struct sockaddr_storage *ss,
    wasi_sockets_network_ip_socket_address_t *addr
) {
    if (ss->ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)ss;
        addr->tag = WASI_SOCKETS_NETWORK_IP_SOCKET_ADDRESS_IPV4;
        addr->val.ipv4.port = ntohs(sin->sin_port);
        uint32_t a = ntohl(sin->sin_addr.s_addr);
        addr->val.ipv4.address.f0 = (a >> 24) & 0xff;
        addr->val.ipv4.address.f1 = (a >> 16) & 0xff;
        addr->val.ipv4.address.f2 = (a >> 8) & 0xff;
        addr->val.ipv4.address.f3 = a & 0xff;
    } else {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
        addr->tag = WASI_SOCKETS_NETWORK_IP_SOCKET_ADDRESS_IPV6;
        addr->val.ipv6.port = ntohs(sin6->sin6_port);
        addr->val.ipv6.flow_info = ntohl(sin6->sin6_flowinfo);
        addr->val.ipv6.scope_id = ntohl(sin6->sin6_scope_id);

        uint16_t *a = (uint16_t *)&sin6->sin6_addr;
        addr->val.ipv6.address.f0 = ntohs(a[0]);
        addr->val.ipv6.address.f1 = ntohs(a[1]);
        addr->val.ipv6.address.f2 = ntohs(a[2]);
        addr->val.ipv6.address.f3 = ntohs(a[3]);
        addr->val.ipv6.address.f4 = ntohs(a[4]);
        addr->val.ipv6.address.f5 = ntohs(a[5]);
        addr->val.ipv6.address.f6 = ntohs(a[6]);
        addr->val.ipv6.address.f7 = ntohs(a[7]);
    }
}

/* ============================================================================
 * Socket Handle Management
 * ============================================================================
 */

static int32_t alloc_tcp_socket(int fd, int family) {
    if (next_tcp_handle >= MAX_SOCKET_HANDLES) return -1;

    wasi_socket_resource_t *sock = (wasi_socket_resource_t *)malloc(sizeof(wasi_socket_resource_t));
    if (!sock) return -1;

    sock->fd = fd;
    sock->family = family;
    sock->type = SOCK_STREAM;
    sock->state = SOCKET_STATE_UNBOUND;
    sock->backlog = 128;

    int32_t handle = next_tcp_handle++;
    tcp_socket_table[handle] = sock;
    return handle;
}

static wasi_socket_resource_t *get_tcp_socket(int32_t handle) {
    if (handle <= 0 || handle >= MAX_SOCKET_HANDLES) return NULL;
    return tcp_socket_table[handle];
}

static int32_t alloc_udp_socket(int fd, int family) {
    if (next_udp_handle >= MAX_SOCKET_HANDLES) return -1;

    wasi_socket_resource_t *sock = (wasi_socket_resource_t *)malloc(sizeof(wasi_socket_resource_t));
    if (!sock) return -1;

    sock->fd = fd;
    sock->family = family;
    sock->type = SOCK_DGRAM;
    sock->state = SOCKET_STATE_UNBOUND;
    sock->backlog = 0;

    int32_t handle = next_udp_handle++;
    udp_socket_table[handle] = sock;
    return handle;
}

static wasi_socket_resource_t *get_udp_socket(int32_t handle) {
    if (handle <= 0 || handle >= MAX_SOCKET_HANDLES) return NULL;
    return udp_socket_table[handle];
}

/* ============================================================================
 * Instance Network
 * ============================================================================
 */

wasi_sockets_instance_network_own_network_t wasi_sockets_instance_network_instance_network(void) {
    if (!network_initialized) {
        network_initialized = true;
    }
    return (wasi_sockets_instance_network_own_network_t){ next_network_handle++ };
}

/* ============================================================================
 * Network Resource Management
 * ============================================================================
 */

void wasi_sockets_network_network_drop_own(wasi_sockets_network_own_network_t handle) {
    (void)handle;
    /* Network handle is a dummy, nothing to free */
}

void wasi_sockets_network_network_drop_borrow(wasi_sockets_network_borrow_network_t handle) {
    (void)handle;
}

wasi_sockets_network_borrow_network_t wasi_sockets_network_borrow_network(
    wasi_sockets_network_own_network_t handle
) {
    return (wasi_sockets_network_borrow_network_t){ handle.__handle };
}

/* ============================================================================
 * TCP Socket Creation
 * ============================================================================
 */

bool wasi_sockets_tcp_create_socket_create_tcp_socket(
    wasi_sockets_tcp_create_socket_ip_address_family_t address_family,
    wasi_sockets_tcp_create_socket_own_tcp_socket_t *ret,
    wasi_sockets_tcp_create_socket_error_code_t *err
) {
    int family = (address_family == WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4) ? AF_INET : AF_INET6;
    int fd = socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

#ifdef NEED_SOCKET_CLOEXEC_WORKAROUND
    set_cloexec(fd);
#endif

    /* Set non-blocking */
    set_nonblock(fd);

    int32_t handle = alloc_tcp_socket(fd, family);
    if (handle < 0) {
        close(fd);
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    ret->__handle = handle;
    return true;
}

/* ============================================================================
 * TCP Socket Resource Management
 * ============================================================================
 */

void wasi_sockets_tcp_tcp_socket_drop_own(wasi_sockets_tcp_own_tcp_socket_t handle) {
    wasi_socket_resource_t *sock = get_tcp_socket(handle.__handle);
    if (sock) {
        if (sock->fd >= 0) close(sock->fd);
        free(sock);
        tcp_socket_table[handle.__handle] = NULL;
    }
}

void wasi_sockets_tcp_tcp_socket_drop_borrow(wasi_sockets_tcp_borrow_tcp_socket_t handle) {
    (void)handle;
}

wasi_sockets_tcp_borrow_tcp_socket_t wasi_sockets_tcp_borrow_tcp_socket(
    wasi_sockets_tcp_own_tcp_socket_t handle
) {
    return (wasi_sockets_tcp_borrow_tcp_socket_t){ handle.__handle };
}

/* ============================================================================
 * TCP Socket Methods
 * ============================================================================
 */

bool wasi_sockets_tcp_method_tcp_socket_start_bind(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_borrow_network_t network,
    wasi_sockets_tcp_ip_socket_address_t *local_address,
    wasi_sockets_tcp_error_code_t *err
) {
    (void)network;
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (sock->state != SOCKET_STATE_UNBOUND) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    struct sockaddr_storage ss;
    socklen_t len;
    wasi_addr_to_sockaddr(local_address, &ss, &len);

    /* Allow address reuse */
    int opt = 1;
    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sock->fd, (struct sockaddr *)&ss, len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    sock->state = SOCKET_STATE_BOUND;
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_finish_bind(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (sock->state != SOCKET_STATE_BOUND) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_IN_PROGRESS;
        return false;
    }

    /* Bind is synchronous, already done */
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_start_connect(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_borrow_network_t network,
    wasi_sockets_tcp_ip_socket_address_t *remote_address,
    wasi_sockets_tcp_error_code_t *err
) {
    (void)network;
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (sock->state != SOCKET_STATE_UNBOUND && sock->state != SOCKET_STATE_BOUND) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    struct sockaddr_storage ss;
    socklen_t len;
    wasi_addr_to_sockaddr(remote_address, &ss, &len);

    int ret = connect(sock->fd, (struct sockaddr *)&ss, len);
    if (ret < 0) {
        if (errno == EINPROGRESS) {
            sock->state = SOCKET_STATE_CONNECTING;
            return true;
        }
        *err = errno_to_socket_error(errno);
        return false;
    }

    sock->state = SOCKET_STATE_CONNECTED;
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_finish_connect(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_tuple2_own_input_stream_own_output_stream_t *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (sock->state == SOCKET_STATE_CONNECTING) {
        /* Check if connection completed */
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            *err = errno_to_socket_error(errno);
            return false;
        }
        if (error != 0) {
            *err = errno_to_socket_error(error);
            return false;
        }
        sock->state = SOCKET_STATE_CONNECTED;
    }

    if (sock->state != SOCKET_STATE_CONNECTED) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_IN_PROGRESS;
        return false;
    }

    /* Create input and output streams */
    int read_fd = dup(sock->fd);
    int write_fd = dup(sock->fd);

    if (read_fd < 0 || write_fd < 0) {
        if (read_fd >= 0) close(read_fd);
        if (write_fd >= 0) close(write_fd);
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    int32_t in_handle = wasi_io_streams_create_input_stream(read_fd, true);
    int32_t out_handle = wasi_io_streams_create_output_stream(write_fd, true);

    if (in_handle < 0 || out_handle < 0) {
        if (in_handle >= 0) close(read_fd);
        if (out_handle >= 0) close(write_fd);
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    ret->f0.__handle = in_handle;
    ret->f1.__handle = out_handle;
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_start_listen(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (sock->state != SOCKET_STATE_BOUND) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (listen(sock->fd, (int)sock->backlog) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    sock->state = SOCKET_STATE_LISTENING;
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_finish_listen(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (sock->state != SOCKET_STATE_LISTENING) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_IN_PROGRESS;
        return false;
    }

    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_accept(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_tuple3_own_tcp_socket_own_input_stream_own_output_stream_t *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (sock->state != SOCKET_STATE_LISTENING) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = portable_accept(sock->fd, (struct sockaddr *)&client_addr, &addr_len, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (client_fd < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    /* Create new socket handle for accepted connection */
    int32_t socket_handle = alloc_tcp_socket(client_fd, sock->family);
    if (socket_handle < 0) {
        close(client_fd);
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    wasi_socket_resource_t *client_sock = get_tcp_socket(socket_handle);
    client_sock->state = SOCKET_STATE_CONNECTED;

    /* Create streams */
    int read_fd = dup(client_fd);
    int write_fd = dup(client_fd);

    if (read_fd < 0 || write_fd < 0) {
        if (read_fd >= 0) close(read_fd);
        if (write_fd >= 0) close(write_fd);
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    int32_t in_handle = wasi_io_streams_create_input_stream(read_fd, true);
    int32_t out_handle = wasi_io_streams_create_output_stream(write_fd, true);

    if (in_handle < 0 || out_handle < 0) {
        if (in_handle >= 0) close(read_fd);
        if (out_handle >= 0) close(write_fd);
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    ret->f0.__handle = socket_handle;
    ret->f1.__handle = in_handle;
    ret->f2.__handle = out_handle;
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_local_address(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_ip_socket_address_t *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);

    if (getsockname(sock->fd, (struct sockaddr *)&ss, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    sockaddr_to_wasi_addr(&ss, ret);
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_remote_address(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_ip_socket_address_t *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);

    if (getpeername(sock->fd, (struct sockaddr *)&ss, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    sockaddr_to_wasi_addr(&ss, ret);
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_is_listening(wasi_sockets_tcp_borrow_tcp_socket_t self) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    return sock && sock->state == SOCKET_STATE_LISTENING;
}

wasi_sockets_tcp_ip_address_family_t wasi_sockets_tcp_method_tcp_socket_address_family(
    wasi_sockets_tcp_borrow_tcp_socket_t self
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (sock && sock->family == AF_INET6) {
        return WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV6;
    }
    return WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4;
}

bool wasi_sockets_tcp_method_tcp_socket_set_listen_backlog_size(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    uint64_t value,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    sock->backlog = value;

    /* If already listening, update via listen() */
    if (sock->state == SOCKET_STATE_LISTENING) {
        if (listen(sock->fd, (int)value) < 0) {
            *err = errno_to_socket_error(errno);
            return false;
        }
    }

    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_keep_alive_enabled(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    bool *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val;
    socklen_t len = sizeof(val);
    if (getsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, &val, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    *ret = (val != 0);
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_set_keep_alive_enabled(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    bool value,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val = value ? 1 : 0;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_keep_alive_idle_time(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_duration_t *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

#ifdef TCP_KEEPIDLE
    int val;
    socklen_t len = sizeof(val);
    if (getsockopt(sock->fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }
    *ret = (uint64_t)val * 1000000000ULL;  /* seconds to nanoseconds */
    return true;
#else
    (void)ret;  /* unused on platforms without TCP_KEEPIDLE */
    *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_SUPPORTED;
    return false;
#endif
}

bool wasi_sockets_tcp_method_tcp_socket_set_keep_alive_idle_time(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_duration_t value,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

#ifdef TCP_KEEPIDLE
    int val = (int)(value / 1000000000ULL);  /* nanoseconds to seconds */
    if (val < 1) val = 1;
    if (setsockopt(sock->fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }
    return true;
#else
    (void)value;
    *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_SUPPORTED;
    return false;
#endif
}

bool wasi_sockets_tcp_method_tcp_socket_keep_alive_interval(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_duration_t *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

#ifdef TCP_KEEPINTVL
    int val;
    socklen_t len = sizeof(val);
    if (getsockopt(sock->fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }
    *ret = (uint64_t)val * 1000000000ULL;
    return true;
#else
    *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_SUPPORTED;
    return false;
#endif
}

bool wasi_sockets_tcp_method_tcp_socket_set_keep_alive_interval(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_duration_t value,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

#ifdef TCP_KEEPINTVL
    int val = (int)(value / 1000000000ULL);
    if (val < 1) val = 1;
    if (setsockopt(sock->fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }
    return true;
#else
    (void)value;
    *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_SUPPORTED;
    return false;
#endif
}

bool wasi_sockets_tcp_method_tcp_socket_keep_alive_count(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    uint32_t *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

#ifdef TCP_KEEPCNT
    int val;
    socklen_t len = sizeof(val);
    if (getsockopt(sock->fd, IPPROTO_TCP, TCP_KEEPCNT, &val, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }
    *ret = (uint32_t)val;
    return true;
#else
    *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_SUPPORTED;
    return false;
#endif
}

bool wasi_sockets_tcp_method_tcp_socket_set_keep_alive_count(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    uint32_t value,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

#ifdef TCP_KEEPCNT
    int val = (int)value;
    if (setsockopt(sock->fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }
    return true;
#else
    (void)value;
    *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_SUPPORTED;
    return false;
#endif
}

bool wasi_sockets_tcp_method_tcp_socket_hop_limit(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    uint8_t *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val;
    socklen_t len = sizeof(val);
    int level = (sock->family == AF_INET6) ? IPPROTO_IPV6 : IPPROTO_IP;
    int opt = (sock->family == AF_INET6) ? IPV6_UNICAST_HOPS : IP_TTL;

    if (getsockopt(sock->fd, level, opt, &val, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    *ret = (uint8_t)val;
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_set_hop_limit(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    uint8_t value,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val = value;
    int level = (sock->family == AF_INET6) ? IPPROTO_IPV6 : IPPROTO_IP;
    int opt = (sock->family == AF_INET6) ? IPV6_UNICAST_HOPS : IP_TTL;

    if (setsockopt(sock->fd, level, opt, &val, sizeof(val)) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_receive_buffer_size(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    uint64_t *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val;
    socklen_t len = sizeof(val);
    if (getsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF, &val, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    *ret = (uint64_t)val;
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_set_receive_buffer_size(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    uint64_t value,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val = (int)value;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_send_buffer_size(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    uint64_t *ret,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val;
    socklen_t len = sizeof(val);
    if (getsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF, &val, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    *ret = (uint64_t)val;
    return true;
}

bool wasi_sockets_tcp_method_tcp_socket_set_send_buffer_size(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    uint64_t value,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val = (int)value;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    return true;
}

wasi_sockets_tcp_own_pollable_t wasi_sockets_tcp_method_tcp_socket_subscribe(
    wasi_sockets_tcp_borrow_tcp_socket_t self
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        return (wasi_sockets_tcp_own_pollable_t){ 0 };
    }

    int32_t pollable = wasi_io_poll_create_fd_pollable(sock->fd, true);
    return (wasi_sockets_tcp_own_pollable_t){ pollable };
}

bool wasi_sockets_tcp_method_tcp_socket_shutdown(
    wasi_sockets_tcp_borrow_tcp_socket_t self,
    wasi_sockets_tcp_shutdown_type_t shutdown_type,
    wasi_sockets_tcp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_tcp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int how;
    switch (shutdown_type) {
        case WASI_SOCKETS_TCP_SHUTDOWN_TYPE_RECEIVE:
            how = SHUT_RD;
            break;
        case WASI_SOCKETS_TCP_SHUTDOWN_TYPE_SEND:
            how = SHUT_WR;
            break;
        case WASI_SOCKETS_TCP_SHUTDOWN_TYPE_BOTH:
            how = SHUT_RDWR;
            break;
        default:
            *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_ARGUMENT;
            return false;
    }

    if (shutdown(sock->fd, how) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    return true;
}

/* ============================================================================
 * UDP Socket Creation
 * ============================================================================
 */

bool wasi_sockets_udp_create_socket_create_udp_socket(
    wasi_sockets_udp_create_socket_ip_address_family_t address_family,
    wasi_sockets_udp_create_socket_own_udp_socket_t *ret,
    wasi_sockets_udp_create_socket_error_code_t *err
) {
    int family = (address_family == WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4) ? AF_INET : AF_INET6;
    int fd = socket(family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

#ifdef NEED_SOCKET_CLOEXEC_WORKAROUND
    set_cloexec(fd);
#endif

    set_nonblock(fd);

    int32_t handle = alloc_udp_socket(fd, family);
    if (handle < 0) {
        close(fd);
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    ret->__handle = handle;
    return true;
}

/* ============================================================================
 * UDP Socket Resource Management
 * ============================================================================
 */

void wasi_sockets_udp_udp_socket_drop_own(wasi_sockets_udp_own_udp_socket_t handle) {
    wasi_socket_resource_t *sock = get_udp_socket(handle.__handle);
    if (sock) {
        if (sock->fd >= 0) close(sock->fd);
        free(sock);
        udp_socket_table[handle.__handle] = NULL;
    }
}

void wasi_sockets_udp_udp_socket_drop_borrow(wasi_sockets_udp_borrow_udp_socket_t handle) {
    (void)handle;
}

wasi_sockets_udp_borrow_udp_socket_t wasi_sockets_udp_borrow_udp_socket(
    wasi_sockets_udp_own_udp_socket_t handle
) {
    return (wasi_sockets_udp_borrow_udp_socket_t){ handle.__handle };
}

/* ============================================================================
 * UDP Socket Methods
 * ============================================================================
 */

bool wasi_sockets_udp_method_udp_socket_start_bind(
    wasi_sockets_udp_borrow_udp_socket_t self,
    wasi_sockets_udp_borrow_network_t network,
    wasi_sockets_udp_ip_socket_address_t *local_address,
    wasi_sockets_udp_error_code_t *err
) {
    (void)network;
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (sock->state != SOCKET_STATE_UNBOUND) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    struct sockaddr_storage ss;
    socklen_t len;
    wasi_addr_to_sockaddr(local_address, &ss, &len);

    int opt = 1;
    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sock->fd, (struct sockaddr *)&ss, len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    sock->state = SOCKET_STATE_BOUND;
    return true;
}

bool wasi_sockets_udp_method_udp_socket_finish_bind(
    wasi_sockets_udp_borrow_udp_socket_t self,
    wasi_sockets_udp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (sock->state != SOCKET_STATE_BOUND) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NOT_IN_PROGRESS;
        return false;
    }

    return true;
}

bool wasi_sockets_udp_method_udp_socket_stream(
    wasi_sockets_udp_borrow_udp_socket_t self,
    wasi_sockets_udp_ip_socket_address_t *maybe_remote_address,
    wasi_sockets_udp_tuple2_own_incoming_datagram_stream_own_outgoing_datagram_stream_t *ret,
    wasi_sockets_udp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    /* Connect to remote if provided */
    if (maybe_remote_address != NULL) {
        struct sockaddr_storage ss;
        socklen_t len;
        wasi_addr_to_sockaddr(maybe_remote_address, &ss, &len);

        if (connect(sock->fd, (struct sockaddr *)&ss, len) < 0) {
            *err = errno_to_socket_error(errno);
            return false;
        }
        sock->state = SOCKET_STATE_CONNECTED;
    }

    /* Create incoming datagram stream */
    if (next_udp_in_stream_handle >= MAX_SOCKET_HANDLES ||
        next_udp_out_stream_handle >= MAX_SOCKET_HANDLES) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    wasi_udp_stream_resource_t *in_stream = (wasi_udp_stream_resource_t *)malloc(sizeof(wasi_udp_stream_resource_t));
    wasi_udp_stream_resource_t *out_stream = (wasi_udp_stream_resource_t *)malloc(sizeof(wasi_udp_stream_resource_t));

    if (!in_stream || !out_stream) {
        free(in_stream);
        free(out_stream);
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    in_stream->fd = sock->fd;
    in_stream->connected = (sock->state == SOCKET_STATE_CONNECTED);
    memset(&in_stream->remote_addr, 0, sizeof(in_stream->remote_addr));
    in_stream->remote_addr_len = 0;

    out_stream->fd = sock->fd;
    out_stream->connected = (sock->state == SOCKET_STATE_CONNECTED);
    if (maybe_remote_address != NULL) {
        wasi_addr_to_sockaddr(maybe_remote_address, &out_stream->remote_addr, &out_stream->remote_addr_len);
    }

    int32_t in_handle = next_udp_in_stream_handle++;
    int32_t out_handle = next_udp_out_stream_handle++;

    udp_in_stream_table[in_handle] = in_stream;
    udp_out_stream_table[out_handle] = out_stream;

    ret->f0.__handle = in_handle;
    ret->f1.__handle = out_handle;
    return true;
}

bool wasi_sockets_udp_method_udp_socket_local_address(
    wasi_sockets_udp_borrow_udp_socket_t self,
    wasi_sockets_udp_ip_socket_address_t *ret,
    wasi_sockets_udp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);

    if (getsockname(sock->fd, (struct sockaddr *)&ss, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    sockaddr_to_wasi_addr(&ss, ret);
    return true;
}

bool wasi_sockets_udp_method_udp_socket_remote_address(
    wasi_sockets_udp_borrow_udp_socket_t self,
    wasi_sockets_udp_ip_socket_address_t *ret,
    wasi_sockets_udp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);

    if (getpeername(sock->fd, (struct sockaddr *)&ss, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    sockaddr_to_wasi_addr(&ss, ret);
    return true;
}

wasi_sockets_udp_ip_address_family_t wasi_sockets_udp_method_udp_socket_address_family(
    wasi_sockets_udp_borrow_udp_socket_t self
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (sock && sock->family == AF_INET6) {
        return WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV6;
    }
    return WASI_SOCKETS_NETWORK_IP_ADDRESS_FAMILY_IPV4;
}

bool wasi_sockets_udp_method_udp_socket_unicast_hop_limit(
    wasi_sockets_udp_borrow_udp_socket_t self,
    uint8_t *ret,
    wasi_sockets_udp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val;
    socklen_t len = sizeof(val);
    int level = (sock->family == AF_INET6) ? IPPROTO_IPV6 : IPPROTO_IP;
    int opt = (sock->family == AF_INET6) ? IPV6_UNICAST_HOPS : IP_TTL;

    if (getsockopt(sock->fd, level, opt, &val, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    *ret = (uint8_t)val;
    return true;
}

bool wasi_sockets_udp_method_udp_socket_set_unicast_hop_limit(
    wasi_sockets_udp_borrow_udp_socket_t self,
    uint8_t value,
    wasi_sockets_udp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val = value;
    int level = (sock->family == AF_INET6) ? IPPROTO_IPV6 : IPPROTO_IP;
    int opt = (sock->family == AF_INET6) ? IPV6_UNICAST_HOPS : IP_TTL;

    if (setsockopt(sock->fd, level, opt, &val, sizeof(val)) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    return true;
}

bool wasi_sockets_udp_method_udp_socket_receive_buffer_size(
    wasi_sockets_udp_borrow_udp_socket_t self,
    uint64_t *ret,
    wasi_sockets_udp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val;
    socklen_t len = sizeof(val);
    if (getsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF, &val, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    *ret = (uint64_t)val;
    return true;
}

bool wasi_sockets_udp_method_udp_socket_set_receive_buffer_size(
    wasi_sockets_udp_borrow_udp_socket_t self,
    uint64_t value,
    wasi_sockets_udp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val = (int)value;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    return true;
}

bool wasi_sockets_udp_method_udp_socket_send_buffer_size(
    wasi_sockets_udp_borrow_udp_socket_t self,
    uint64_t *ret,
    wasi_sockets_udp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val;
    socklen_t len = sizeof(val);
    if (getsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF, &val, &len) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    *ret = (uint64_t)val;
    return true;
}

bool wasi_sockets_udp_method_udp_socket_set_send_buffer_size(
    wasi_sockets_udp_borrow_udp_socket_t self,
    uint64_t value,
    wasi_sockets_udp_error_code_t *err
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    int val = (int)value;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)) < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    return true;
}

wasi_sockets_udp_own_pollable_t wasi_sockets_udp_method_udp_socket_subscribe(
    wasi_sockets_udp_borrow_udp_socket_t self
) {
    wasi_socket_resource_t *sock = get_udp_socket(self.__handle);
    if (!sock) {
        return (wasi_sockets_udp_own_pollable_t){ 0 };
    }

    int32_t pollable = wasi_io_poll_create_fd_pollable(sock->fd, true);
    return (wasi_sockets_udp_own_pollable_t){ pollable };
}

/* ============================================================================
 * UDP Datagram Stream Methods
 * ============================================================================
 */

void wasi_sockets_udp_incoming_datagram_stream_drop_own(
    wasi_sockets_udp_own_incoming_datagram_stream_t handle
) {
    if (handle.__handle > 0 && handle.__handle < MAX_SOCKET_HANDLES) {
        wasi_udp_stream_resource_t *stream = udp_in_stream_table[handle.__handle];
        if (stream) {
            free(stream);
            udp_in_stream_table[handle.__handle] = NULL;
        }
    }
}

void wasi_sockets_udp_incoming_datagram_stream_drop_borrow(
    wasi_sockets_udp_borrow_incoming_datagram_stream_t handle
) {
    (void)handle;
}

wasi_sockets_udp_borrow_incoming_datagram_stream_t wasi_sockets_udp_borrow_incoming_datagram_stream(
    wasi_sockets_udp_own_incoming_datagram_stream_t handle
) {
    return (wasi_sockets_udp_borrow_incoming_datagram_stream_t){ handle.__handle };
}

void wasi_sockets_udp_outgoing_datagram_stream_drop_own(
    wasi_sockets_udp_own_outgoing_datagram_stream_t handle
) {
    if (handle.__handle > 0 && handle.__handle < MAX_SOCKET_HANDLES) {
        wasi_udp_stream_resource_t *stream = udp_out_stream_table[handle.__handle];
        if (stream) {
            free(stream);
            udp_out_stream_table[handle.__handle] = NULL;
        }
    }
}

void wasi_sockets_udp_outgoing_datagram_stream_drop_borrow(
    wasi_sockets_udp_borrow_outgoing_datagram_stream_t handle
) {
    (void)handle;
}

wasi_sockets_udp_borrow_outgoing_datagram_stream_t wasi_sockets_udp_borrow_outgoing_datagram_stream(
    wasi_sockets_udp_own_outgoing_datagram_stream_t handle
) {
    return (wasi_sockets_udp_borrow_outgoing_datagram_stream_t){ handle.__handle };
}

bool wasi_sockets_udp_method_incoming_datagram_stream_receive(
    wasi_sockets_udp_borrow_incoming_datagram_stream_t self,
    uint64_t max_results,
    wasi_sockets_udp_list_incoming_datagram_t *ret,
    wasi_sockets_udp_error_code_t *err
) {
    if (self.__handle <= 0 || self.__handle >= MAX_SOCKET_HANDLES) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    wasi_udp_stream_resource_t *stream = udp_in_stream_table[self.__handle];
    if (!stream) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    /* Allocate space for datagrams */
    size_t count = (max_results > 64) ? 64 : (size_t)max_results;
    wasi_sockets_udp_incoming_datagram_t *datagrams = NULL;
    if (!wasi_cabi_alloc_list(count, sizeof(wasi_sockets_udp_incoming_datagram_t),
                              WASI_ALIGNOF(wasi_sockets_udp_incoming_datagram_t), (void **)&datagrams)) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    size_t received = 0;
    uint8_t buf[65536];

    while (received < count) {
        struct sockaddr_storage from;
        socklen_t from_len = sizeof(from);

        ssize_t n = recvfrom(stream->fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  /* No more data available */
            }
            wasi_cabi_free(datagrams, WASI_ALIGNOF(wasi_sockets_udp_incoming_datagram_t));
            *err = errno_to_socket_error(errno);
            return false;
        }

        if (!wasi_cabi_alloc_list((size_t)n, 1, 1, (void **)&datagrams[received].data.ptr)) {
            /* Free previously allocated data */
            for (size_t i = 0; i < received; i++) {
                wasi_cabi_free(datagrams[i].data.ptr, 1);
            }
            wasi_cabi_free(datagrams, WASI_ALIGNOF(wasi_sockets_udp_incoming_datagram_t));
            *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
            return false;
        }

        memcpy(datagrams[received].data.ptr, buf, (size_t)n);
        datagrams[received].data.len = (size_t)n;
        sockaddr_to_wasi_addr(&from, &datagrams[received].remote_address);

        received++;
    }

    ret->ptr = datagrams;
    ret->len = received;
    return true;
}

wasi_sockets_udp_own_pollable_t wasi_sockets_udp_method_incoming_datagram_stream_subscribe(
    wasi_sockets_udp_borrow_incoming_datagram_stream_t self
) {
    if (self.__handle <= 0 || self.__handle >= MAX_SOCKET_HANDLES) {
        return (wasi_sockets_udp_own_pollable_t){ 0 };
    }

    wasi_udp_stream_resource_t *stream = udp_in_stream_table[self.__handle];
    if (!stream) {
        return (wasi_sockets_udp_own_pollable_t){ 0 };
    }

    int32_t pollable = wasi_io_poll_create_fd_pollable(stream->fd, false);
    return (wasi_sockets_udp_own_pollable_t){ pollable };
}

bool wasi_sockets_udp_method_outgoing_datagram_stream_check_send(
    wasi_sockets_udp_borrow_outgoing_datagram_stream_t self,
    uint64_t *ret,
    wasi_sockets_udp_error_code_t *err
) {
    if (self.__handle <= 0 || self.__handle >= MAX_SOCKET_HANDLES) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    wasi_udp_stream_resource_t *stream = udp_out_stream_table[self.__handle];
    if (!stream) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    /* Check if socket is writable */
    struct pollfd pfd = { stream->fd, POLLOUT, 0 };
    int r = poll(&pfd, 1, 0);
    if (r < 0) {
        *err = errno_to_socket_error(errno);
        return false;
    }

    if (pfd.revents & POLLOUT) {
        *ret = 65536;  /* Max UDP payload size */
    } else {
        *ret = 0;
    }

    return true;
}

bool wasi_sockets_udp_method_outgoing_datagram_stream_send(
    wasi_sockets_udp_borrow_outgoing_datagram_stream_t self,
    wasi_sockets_udp_list_outgoing_datagram_t *datagrams,
    uint64_t *ret,
    wasi_sockets_udp_error_code_t *err
) {
    if (self.__handle <= 0 || self.__handle >= MAX_SOCKET_HANDLES) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    wasi_udp_stream_resource_t *stream = udp_out_stream_table[self.__handle];
    if (!stream) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    uint64_t sent = 0;

    for (size_t i = 0; i < datagrams->len; i++) {
        wasi_sockets_udp_outgoing_datagram_t *dg = &datagrams->ptr[i];

        struct sockaddr_storage to;
        socklen_t to_len = 0;
        struct sockaddr *to_ptr = NULL;

        if (dg->remote_address.is_some) {
            wasi_addr_to_sockaddr(&dg->remote_address.val, &to, &to_len);
            to_ptr = (struct sockaddr *)&to;
        } else if (stream->connected) {
            /* Use connected address */
            to_ptr = NULL;  /* sendto with NULL uses connected address */
        } else {
            *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_ARGUMENT;
            return false;
        }

        ssize_t n = sendto(stream->fd, dg->data.ptr, dg->data.len, 0, to_ptr, to_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  /* Buffer full */
            }
            *err = errno_to_socket_error(errno);
            return false;
        }

        sent++;
    }

    *ret = sent;
    return true;
}

wasi_sockets_udp_own_pollable_t wasi_sockets_udp_method_outgoing_datagram_stream_subscribe(
    wasi_sockets_udp_borrow_outgoing_datagram_stream_t self
) {
    if (self.__handle <= 0 || self.__handle >= MAX_SOCKET_HANDLES) {
        return (wasi_sockets_udp_own_pollable_t){ 0 };
    }

    wasi_udp_stream_resource_t *stream = udp_out_stream_table[self.__handle];
    if (!stream) {
        return (wasi_sockets_udp_own_pollable_t){ 0 };
    }

    int32_t pollable = wasi_io_poll_create_fd_pollable(stream->fd, true);
    return (wasi_sockets_udp_own_pollable_t){ pollable };
}

/* ============================================================================
 * IP Name Lookup (DNS Resolution)
 * ============================================================================
 */

bool wasi_sockets_ip_name_lookup_resolve_addresses(
    wasi_sockets_ip_name_lookup_borrow_network_t network,
    imports_string_t *name,
    wasi_sockets_ip_name_lookup_own_resolve_address_stream_t *ret,
    wasi_sockets_ip_name_lookup_error_code_t *err
) {
    (void)network;

    if (next_resolve_handle >= MAX_SOCKET_HANDLES) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    /* Convert name to C string */
    wasi_utf8_validate_or_abort(name->ptr, name->len);
    char *hostname = (char *)malloc(name->len + 1);
    if (!hostname) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }
    memcpy(hostname, name->ptr, name->len);
    hostname[name->len] = '\0';

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *results = NULL;
    int r = getaddrinfo(hostname, NULL, &hints, &results);
    free(hostname);

    if (r != 0) {
        if (r == EAI_NONAME || r == EAI_NODATA) {
            *err = WASI_SOCKETS_NETWORK_ERROR_CODE_NAME_UNRESOLVABLE;
        } else if (r == EAI_AGAIN) {
            *err = WASI_SOCKETS_NETWORK_ERROR_CODE_TEMPORARY_RESOLVER_FAILURE;
        } else if (r == EAI_FAIL) {
            *err = WASI_SOCKETS_NETWORK_ERROR_CODE_PERMANENT_RESOLVER_FAILURE;
        } else if (r == EAI_MEMORY) {
            *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        } else {
            *err = WASI_SOCKETS_NETWORK_ERROR_CODE_UNKNOWN;
        }
        return false;
    }

    wasi_resolve_stream_resource_t *stream = (wasi_resolve_stream_resource_t *)
        malloc(sizeof(wasi_resolve_stream_resource_t));
    if (!stream) {
        freeaddrinfo(results);
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_OUT_OF_MEMORY;
        return false;
    }

    stream->results = results;
    stream->current = results;

    int32_t handle = next_resolve_handle++;
    resolve_stream_table[handle] = stream;

    ret->__handle = handle;
    return true;
}

void wasi_sockets_ip_name_lookup_resolve_address_stream_drop_own(
    wasi_sockets_ip_name_lookup_own_resolve_address_stream_t handle
) {
    if (handle.__handle > 0 && handle.__handle < MAX_SOCKET_HANDLES) {
        wasi_resolve_stream_resource_t *stream = resolve_stream_table[handle.__handle];
        if (stream) {
            if (stream->results) freeaddrinfo(stream->results);
            free(stream);
            resolve_stream_table[handle.__handle] = NULL;
        }
    }
}

void wasi_sockets_ip_name_lookup_resolve_address_stream_drop_borrow(
    wasi_sockets_ip_name_lookup_borrow_resolve_address_stream_t handle
) {
    (void)handle;
}

wasi_sockets_ip_name_lookup_borrow_resolve_address_stream_t
wasi_sockets_ip_name_lookup_borrow_resolve_address_stream(
    wasi_sockets_ip_name_lookup_own_resolve_address_stream_t handle
) {
    return (wasi_sockets_ip_name_lookup_borrow_resolve_address_stream_t){ handle.__handle };
}

bool wasi_sockets_ip_name_lookup_method_resolve_address_stream_resolve_next_address(
    wasi_sockets_ip_name_lookup_borrow_resolve_address_stream_t self,
    wasi_sockets_ip_name_lookup_option_ip_address_t *ret,
    wasi_sockets_ip_name_lookup_error_code_t *err
) {
    if (self.__handle <= 0 || self.__handle >= MAX_SOCKET_HANDLES) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    wasi_resolve_stream_resource_t *stream = resolve_stream_table[self.__handle];
    if (!stream) {
        *err = WASI_SOCKETS_NETWORK_ERROR_CODE_INVALID_STATE;
        return false;
    }

    if (stream->current == NULL) {
        ret->is_some = false;
        return true;
    }

    struct addrinfo *ai = stream->current;
    stream->current = ai->ai_next;

    ret->is_some = true;

    if (ai->ai_family == AF_INET) {
        /* Use memcpy to avoid alignment issues with sockaddr cast */
        struct sockaddr_in sin;
        memcpy(&sin, ai->ai_addr, sizeof(sin));
        ret->val.tag = WASI_SOCKETS_NETWORK_IP_ADDRESS_IPV4;
        uint32_t a = ntohl(sin.sin_addr.s_addr);
        ret->val.val.ipv4.f0 = (uint8_t)((a >> 24) & 0xff);
        ret->val.val.ipv4.f1 = (uint8_t)((a >> 16) & 0xff);
        ret->val.val.ipv4.f2 = (uint8_t)((a >> 8) & 0xff);
        ret->val.val.ipv4.f3 = (uint8_t)(a & 0xff);
    } else {
        /* Use memcpy to avoid alignment issues with sockaddr cast */
        struct sockaddr_in6 sin6;
        memcpy(&sin6, ai->ai_addr, sizeof(sin6));
        ret->val.tag = WASI_SOCKETS_NETWORK_IP_ADDRESS_IPV6;
        const uint8_t *bytes = sin6.sin6_addr.s6_addr;
        ret->val.val.ipv6.f0 = (uint16_t)((bytes[0] << 8) | bytes[1]);
        ret->val.val.ipv6.f1 = (uint16_t)((bytes[2] << 8) | bytes[3]);
        ret->val.val.ipv6.f2 = (uint16_t)((bytes[4] << 8) | bytes[5]);
        ret->val.val.ipv6.f3 = (uint16_t)((bytes[6] << 8) | bytes[7]);
        ret->val.val.ipv6.f4 = (uint16_t)((bytes[8] << 8) | bytes[9]);
        ret->val.val.ipv6.f5 = (uint16_t)((bytes[10] << 8) | bytes[11]);
        ret->val.val.ipv6.f6 = (uint16_t)((bytes[12] << 8) | bytes[13]);
        ret->val.val.ipv6.f7 = (uint16_t)((bytes[14] << 8) | bytes[15]);
    }

    return true;
}

wasi_sockets_ip_name_lookup_own_pollable_t
wasi_sockets_ip_name_lookup_method_resolve_address_stream_subscribe(
    wasi_sockets_ip_name_lookup_borrow_resolve_address_stream_t self
) {
    /* DNS resolution is synchronous in this implementation */
    /* Return an always-ready pollable */
    (void)self;

    extern int32_t wasi_io_poll_create_ready_pollable(void);
    int32_t pollable = wasi_io_poll_create_ready_pollable();
    return (wasi_sockets_ip_name_lookup_own_pollable_t){ pollable };
}
