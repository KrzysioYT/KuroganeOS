#ifndef KUROGANE_SDK_SYSTEM_H
#define KUROGANE_SDK_SYSTEM_H

#include <stddef.h>
#include <stdint.h>
#include <kurogane/status.h>
#include <kurogane/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_SYSTEM_SNAPSHOT_VERSION UINT32_C(1)

typedef struct ku_system_snapshot {
    uint32_t structure_size;
    uint32_t version;
    uint32_t cpu_percent;
    uint32_t gpu_percent;
    uint32_t ram_percent;
    uint32_t disk_percent;
    uint64_t memory_total_bytes;
    uint64_t memory_free_bytes;
    uint64_t uptime_ticks;
} ku_system_snapshot;

static inline ku_status_t ku_system_get_snapshot(ku_system_snapshot* output) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_SYSTEM_SNAPSHOT,
        (uint64_t)(uintptr_t)output,
        (uint64_t)sizeof(ku_system_snapshot),
        0U);
}

#ifdef __cplusplus
}
#endif
#endif
