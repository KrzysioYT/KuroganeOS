#ifndef KUROGANE_SDK_NETWORK_H
#define KUROGANE_SDK_NETWORK_H

#include <kurogane/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_NETWORK_ABI_VERSION UINT32_C(1)

enum ku_network_flags {
    KU_NETWORK_READY = UINT32_C(1) << 0,
    KU_NETWORK_PHYSICAL_ACTIVE = UINT32_C(1) << 1,
    KU_NETWORK_DHCP_CONFIGURED = UINT32_C(1) << 2,
    KU_NETWORK_PHYSICAL_DETECTED = UINT32_C(1) << 3
};

typedef struct ku_ipv4_address {
    uint8_t bytes[4];
} ku_ipv4_address;

typedef struct ku_network_info {
    uint32_t structure_size;
    uint32_t flags;
    ku_ipv4_address address;
    ku_ipv4_address netmask;
    ku_ipv4_address gateway;
    ku_ipv4_address dns_server;
    uint32_t lease_seconds;
    uint32_t reserved;
} ku_network_info;

static inline ku_status_t ku_network_get_info(ku_network_info* info) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_NETWORK_INFO,
        (uint64_t)(uintptr_t)info,
        sizeof(ku_network_info),
        0);
}

static inline ku_status_t ku_network_resolve_a(
    const char* host,
    size_t host_length,
    ku_ipv4_address* address) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_NETWORK_RESOLVE_A,
        (uint64_t)(uintptr_t)host,
        (uint64_t)host_length,
        (uint64_t)(uintptr_t)address);
}

static inline ku_status_t ku_network_ping(
    const ku_ipv4_address* address,
    uint16_t sequence) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_NETWORK_PING,
        (uint64_t)(uintptr_t)address,
        sizeof(ku_ipv4_address),
        (uint64_t)sequence);
}

#if defined(__cplusplus)
static_assert(sizeof(ku_ipv4_address) == 4, "IPv4 ABI mismatch");
static_assert(sizeof(ku_network_info) == 32, "network info ABI mismatch");
#else
_Static_assert(sizeof(ku_ipv4_address) == 4, "IPv4 ABI mismatch");
_Static_assert(sizeof(ku_network_info) == 32, "network info ABI mismatch");
#endif

#ifdef __cplusplus
}
#endif

#endif
