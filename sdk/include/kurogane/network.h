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
