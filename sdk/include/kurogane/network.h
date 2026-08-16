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

static inline ku_status_t ku_http_get(ku_http_request* request) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_HTTP_GET,
        (uint64_t)(uintptr_t)request,
        (uint64_t)sizeof(ku_http_request),
        0U);
}

#ifdef __cplusplus
}
#endif
#endif
