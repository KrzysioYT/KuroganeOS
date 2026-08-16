#include "system_metrics.hpp"

namespace system_metrics {
namespace {
volatile uint64_t g_total_loops = 0U;
volatile uint64_t g_busy_loops = 0U;
volatile uint64_t g_disk_blocks = 0U;
uint32_t g_last_cpu = 0U;
uint32_t g_last_disk = 0U;

uint32_t bounded_percent(uint64_t numerator, uint64_t denominator) {
    if (denominator == 0U || numerator == 0U) return 0U;
    if (numerator >= denominator) return 100U;
    const uint64_t scaled = numerator * UINT64_C(100);
    return static_cast<uint32_t>(scaled / denominator);
}
} // namespace

void record_loop(bool busy) {
    __atomic_fetch_add(&g_total_loops, UINT64_C(1), __ATOMIC_RELAXED);
    if (busy) {
        __atomic_fetch_add(&g_busy_loops, UINT64_C(1), __ATOMIC_RELAXED);
    }
}

void record_disk_blocks(uint64_t blocks) {
    if (blocks == 0U) return;
    uint64_t current = __atomic_load_n(&g_disk_blocks, __ATOMIC_RELAXED);
    for (;;) {
        const uint64_t next = UINT64_MAX - current < blocks
            ? UINT64_MAX : current + blocks;
        if (__atomic_compare_exchange_n(
                &g_disk_blocks, &current, next, false,
                __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            return;
        }
    }
}

ActivitySnapshot sample() {
    const uint64_t total = __atomic_exchange_n(
        &g_total_loops, UINT64_C(0), __ATOMIC_RELAXED);
    const uint64_t busy = __atomic_exchange_n(
        &g_busy_loops, UINT64_C(0), __ATOMIC_RELAXED);
    const uint64_t disk = __atomic_exchange_n(
        &g_disk_blocks, UINT64_C(0), __ATOMIC_RELAXED);

    if (total != 0U) {
        g_last_cpu = bounded_percent(busy, total);
    }

    // 128 completed 512-byte sector operations during one widget sample is
    // treated as 100% activity. This is an activity indicator rather than a
    // bandwidth benchmark and stays meaningful across QEMU/VirtualBox hosts.
    if (disk == 0U) {
        g_last_disk = 0U;
    } else {
        g_last_disk = bounded_percent(disk, UINT64_C(128));
    }

    return ActivitySnapshot{g_last_cpu, g_last_disk};
}

} // namespace system_metrics
