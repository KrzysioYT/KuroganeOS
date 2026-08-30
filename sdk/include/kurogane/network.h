#ifndef KUROGANE_SDK_NETWORK_H
#define KUROGANE_SDK_NETWORK_H

#include <stddef.h>
#include <stdint.h>
#include <kurogane/status.h>
#include <kurogane/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_NET_HOST_CAPACITY 64U
#define KU_NET_PATH_CAPACITY 160U
#define KU_HTTP_RESPONSE_CAPACITY_LIMIT 4096U
#define KU_HTTP_FLAG_NONE UINT32_C(0)
#define KU_DNS_NAME_CAPACITY KU_NET_HOST_CAPACITY
#define KU_DNS_FLAG_NONE UINT32_C(0)

#define KU_SOCKET_FLAG_NONE UINT32_C(0)
#define KU_SOCKET_INVALID UINT64_C(0)

typedef uint64_t ku_socket_t;

enum ku_socket_type {
    KU_SOCKET_DATAGRAM = 1,
    KU_SOCKET_STREAM = 2
};

enum ku_socket_protocol {
    KU_SOCKET_PROTOCOL_TCP = 6,
    KU_SOCKET_PROTOCOL_UDP = 17
};

enum ku_socket_ready_flags {
    KU_SOCKET_READY_NONE = 0,
    KU_SOCKET_READY_READ = UINT32_C(1) << 0,
    KU_SOCKET_READY_WRITE = UINT32_C(1) << 1,
    KU_SOCKET_READY_CONNECTED = UINT32_C(1) << 2,
    KU_SOCKET_READY_HANGUP = UINT32_C(1) << 3,
    KU_SOCKET_READY_ERROR = UINT32_C(1) << 4
};

#define KU_SOCKET_READY_ALL (     KU_SOCKET_READY_READ | KU_SOCKET_READY_WRITE |     KU_SOCKET_READY_CONNECTED | KU_SOCKET_READY_HANGUP | KU_SOCKET_READY_ERROR)

typedef struct ku_ipv4_endpoint {
    uint8_t address[4];
    uint16_t port;
    uint16_t reserved;
} ku_ipv4_endpoint;

typedef struct ku_socket_receive_request {
    uint32_t structure_size;
    uint32_t flags;
    ku_socket_t socket;
    void* buffer;
    uint64_t buffer_capacity;
    uint64_t bytes_received;
    ku_ipv4_endpoint source;
} ku_socket_receive_request;

/* Internal transport tag used only across the current 3.3.3 syscall boundary. */
#define KU_HTTPS_TRANSPORT_TAG "~tls~"
#define KU_HTTPS_TRANSPORT_TAG_SIZE 5U

typedef struct ku_network_status {
    uint32_t structure_size;
    uint32_t ready;
    uint32_t physical;
    uint32_t dhcp;
    uint8_t address[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint8_t reserved0[4];
    uint64_t bytes_received;
    uint64_t bytes_transmitted;
} ku_network_status;

typedef struct ku_dns_a_request {
    uint32_t structure_size;
    uint32_t flags;
    char host[KU_DNS_NAME_CAPACITY];
    uint8_t address[4];
    uint32_t reserved;
} ku_dns_a_request;

typedef struct ku_http_request {
    uint32_t structure_size;
    uint32_t flags;
    char host[KU_NET_HOST_CAPACITY];
    char path[KU_NET_PATH_CAPACITY];
    void* output;
    uint64_t output_capacity;
    uint64_t bytes_received;
    uint32_t http_status;
    uint32_t reserved;
} ku_http_request;

static inline ku_status_t ku_network_get_status(ku_network_status* output) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_NET_STATUS,
        (uint64_t)(uintptr_t)output,
        (uint64_t)sizeof(ku_network_status),
        0U);
}

static inline ku_status_t ku_dns_resolve_a(ku_dns_a_request* request) {
    if (request == NULL) return KU_STATUS_INVALID_ARGUMENT;
    request->flags = KU_DNS_FLAG_NONE;
    request->reserved = 0U;
    return (ku_status_t)ku_syscall3(
        KU_SYS_DNS_RESOLVE_A,
        (uint64_t)(uintptr_t)request,
        (uint64_t)sizeof(ku_dns_a_request),
        0U);
}

static inline ku_result_t ku_socket_create(
    uint32_t type,
    uint32_t protocol) {
    return ku_syscall3(KU_SYS_SOCKET_CREATE, type, protocol, KU_SOCKET_FLAG_NONE);
}

static inline ku_status_t ku_socket_bind(
    ku_socket_t socket,
    const ku_ipv4_endpoint* endpoint) {
    if (endpoint == NULL) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(
        KU_SYS_SOCKET_BIND, socket, (uint64_t)(uintptr_t)endpoint, sizeof(*endpoint));
}

static inline ku_status_t ku_socket_connect(
    ku_socket_t socket,
    const ku_ipv4_endpoint* endpoint) {
    if (endpoint == NULL) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(
        KU_SYS_SOCKET_CONNECT, socket, (uint64_t)(uintptr_t)endpoint, sizeof(*endpoint));
}

static inline ku_result_t ku_socket_send(
    ku_socket_t socket,
    const void* buffer,
    size_t size) {
    return ku_syscall3(
        KU_SYS_SOCKET_SEND, socket, (uint64_t)(uintptr_t)buffer, (uint64_t)size);
}

static inline ku_result_t ku_socket_receive(
    ku_socket_t socket,
    void* buffer,
    size_t capacity,
    ku_ipv4_endpoint* source) {
    ku_socket_receive_request request;
    request.bytes_received = 0U;
    request.source.address[0] = 0U;
    request.source.address[1] = 0U;
    request.source.address[2] = 0U;
    request.source.address[3] = 0U;
    request.source.port = 0U;
    request.source.reserved = 0U;
    request.structure_size = sizeof(request);
    request.flags = KU_SOCKET_FLAG_NONE;
    request.socket = socket;
    request.buffer = buffer;
    request.buffer_capacity = capacity;
    const ku_status_t status = (ku_status_t)ku_syscall3(
        KU_SYS_SOCKET_RECEIVE,
        (uint64_t)(uintptr_t)&request,
        sizeof(request),
        0U);
    if (status != KU_STATUS_OK) return status;
    if (source != NULL) *source = request.source;
    return (ku_result_t)request.bytes_received;
}

static inline ku_status_t ku_socket_close(ku_socket_t socket) {
    return (ku_status_t)ku_syscall3(KU_SYS_SOCKET_CLOSE, socket, 0U, 0U);
}

static inline ku_status_t ku_socket_poll(
    ku_socket_t socket,
    uint32_t requested,
    uint32_t* ready) {
    if (socket == KU_SOCKET_INVALID || ready == NULL || requested == 0U ||
        (requested & ~KU_SOCKET_READY_ALL) != 0U) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    *ready = KU_SOCKET_READY_NONE;
    return (ku_status_t)ku_syscall3(
        KU_SYS_SOCKET_POLL,
        socket,
        requested,
        (uint64_t)(uintptr_t)ready);
}

/*
 * Scheduler-friendly readiness wait. Socket operations remain non-blocking;
 * callers that want to wait sleep between readiness probes instead of spinning.
 * timeout_ticks == 0 performs a single probe. UINT64_MAX waits indefinitely.
 */
static inline ku_status_t ku_socket_wait(
    ku_socket_t socket,
    uint32_t requested,
    uint64_t timeout_ticks,
    uint32_t* ready) {
    uint64_t elapsed = 0U;
    for (;;) {
        ku_status_t status = ku_socket_poll(socket, requested, ready);
        if (status != KU_STATUS_OK) return status;
        if ((*ready & requested) != 0U) return KU_STATUS_OK;
        if (timeout_ticks == 0U || elapsed >= timeout_ticks) {
            return KU_STATUS_TIMED_OUT;
        }
        status = ku_sleep(1U);
        if (status != KU_STATUS_OK) return status;
        if (elapsed != UINT64_MAX) ++elapsed;
    }
}

static inline ku_status_t ku_http_get(ku_http_request* request) {
    if (request == NULL) return KU_STATUS_INVALID_ARGUMENT;
    request->flags = KU_HTTP_FLAG_NONE;
    return (ku_status_t)ku_syscall3(
        KU_SYS_HTTP_GET,
        (uint64_t)(uintptr_t)request,
        (uint64_t)sizeof(ku_http_request),
        0U);
}

/*
 * Source-stable HTTPS entry point. The 3.3.3 kernel syscall validates flags=0,
 * so the SDK temporarily prefixes an impossible DNS label marker and restores
 * the caller-visible host before returning. The network service strips the
 * marker before DNS/SNI/certificate verification. This lets the public API stay
 * ku_https_get() while the transport selector can later move into request flags
 * without changing applications.
 */
static inline ku_status_t ku_https_get(ku_http_request* request) {
    size_t length = 0U;
    size_t index;
    ku_status_t status;
    if (request == NULL) return KU_STATUS_INVALID_ARGUMENT;
    while (length < KU_NET_HOST_CAPACITY && request->host[length] != '\0') ++length;
    if (length == 0U || length >= KU_NET_HOST_CAPACITY ||
        length + KU_HTTPS_TRANSPORT_TAG_SIZE >= KU_NET_HOST_CAPACITY) {
        return KU_STATUS_OUT_OF_RANGE;
    }

    for (index = length + 1U; index != 0U; --index) {
        request->host[index - 1U + KU_HTTPS_TRANSPORT_TAG_SIZE] =
            request->host[index - 1U];
    }
    request->host[0] = '~';
    request->host[1] = 't';
    request->host[2] = 'l';
    request->host[3] = 's';
    request->host[4] = '~';
    request->flags = KU_HTTP_FLAG_NONE;

    status = (ku_status_t)ku_syscall3(
        KU_SYS_HTTP_GET,
        (uint64_t)(uintptr_t)request,
        (uint64_t)sizeof(ku_http_request),
        0U);

    for (index = 0U; index <= length; ++index) {
        request->host[index] = request->host[index + KU_HTTPS_TRANSPORT_TAG_SIZE];
    }
    return status;
}

#ifdef __cplusplus
}
#endif
#endif
