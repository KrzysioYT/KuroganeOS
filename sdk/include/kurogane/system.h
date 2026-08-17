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

/*
 * The current scheduler/PIT monotonic timebase is 100 Hz. It never represents
 * wall-clock/calendar time. Keep the frequency explicit so applications can
 * convert deadlines without assuming a host-specific timer rate.
 */
#define KU_SYSTEM_TICKS_PER_SECOND UINT64_C(100)
#define KU_SYSTEM_MILLISECONDS_PER_SECOND UINT64_C(1000)

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

static inline uint64_t ku_system_uptime_seconds(
    const ku_system_snapshot* snapshot) {
    return snapshot == NULL
        ? UINT64_C(0)
        : snapshot->uptime_ticks / KU_SYSTEM_TICKS_PER_SECOND;
}

/* Read the monotonic scheduler clock through the stable system snapshot ABI. */
static inline ku_status_t ku_system_monotonic_ticks(uint64_t* ticks) {
#ifdef __cplusplus
    ku_system_snapshot snapshot{};
#else
    ku_system_snapshot snapshot = {0};
#endif
    ku_status_t status;
    if (ticks == NULL) return KU_STATUS_INVALID_ARGUMENT;
    snapshot.structure_size = sizeof(snapshot);
    status = ku_system_get_snapshot(&snapshot);
    if (status != KU_STATUS_OK) return status;
    *ticks = snapshot.uptime_ticks;
    return KU_STATUS_OK;
}

static inline uint64_t ku_system_ticks_to_milliseconds(uint64_t ticks) {
    const uint64_t whole = ticks / KU_SYSTEM_TICKS_PER_SECOND;
    const uint64_t remainder = ticks % KU_SYSTEM_TICKS_PER_SECOND;
    if (whole > UINT64_MAX / KU_SYSTEM_MILLISECONDS_PER_SECOND) {
        return UINT64_MAX;
    }
    return whole * KU_SYSTEM_MILLISECONDS_PER_SECOND +
        (remainder * KU_SYSTEM_MILLISECONDS_PER_SECOND) /
            KU_SYSTEM_TICKS_PER_SECOND;
}

static inline ku_status_t ku_system_monotonic_milliseconds(uint64_t* milliseconds) {
    uint64_t ticks = 0U;
    ku_status_t status;
    if (milliseconds == NULL) return KU_STATUS_INVALID_ARGUMENT;
    status = ku_system_monotonic_ticks(&ticks);
    if (status != KU_STATUS_OK) return status;
    *milliseconds = ku_system_ticks_to_milliseconds(ticks);
    return KU_STATUS_OK;
}

#ifdef __cplusplus
}
#endif
#endif
