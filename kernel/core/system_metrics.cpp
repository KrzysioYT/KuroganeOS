#include "system_metrics.hpp"

#include "../task/thread.hpp"

namespace system_metrics {
namespace {
volatile uint64_t g_total_loops = 0U;
volatile uint64_t g_busy_loops = 0U;
volatile uint64_t g_disk_blocks = 0U;
volatile uint64_t g_graphics_units = 0U;
uint64_t g_previous_switches = 0U;
uint64_t g_previous_ticks = 0U;
uint32_t g_last_cpu = 0U;
uint32_t g_last_gpu = 0U;
uint32_t g_last_disk = 0U;

uint32_t bounded_percent(uint64_t numerator, uint64_t denominator) {
    if (denominator == 0U || numerator == 0U) return 0U;
    if (numerator >= denominator) return 100U;
    if (numerator > UINT64_MAX / UINT64_C(100)) return 100U;
    return static_cast<uint32_t>((numerator * UINT64_C(100)) / denominator);
}

void saturating_atomic_add(volatile uint64_t* target, uint64_t increment) {
    uint64_t current = __atomic_load_n(target, __ATOMIC_RELAXED);
    for (;;) {
        const uint64_t next = UINT64_MAX - current < increment
            ? UINT64_MAX : current + increment;
        if (__atomic_compare_exchange_n(
                target, &current, next, false,
                __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            return;
        }
    }
}

struct SchedulerSample {
    uint64_t switches;
    uint32_t runnable;
};

bool collect_thread(const threading::Stat& stat, void* context) {
    auto* sample = static_cast<SchedulerSample*>(context);
    if (sample == nullptr) return false;
    if (UINT64_MAX - sample->switches < stat.switches) {
        sample->switches = UINT64_MAX;
    } else {
        sample->switches += stat.switches;
    }
    if (stat.state == threading::State::Ready ||
        stat.state == threading::State::Running) {
        ++sample->runnable;
    }
    return true;
}
} // namespace

void record_loop(bool busy) {
    __atomic_fetch_add(&g_total_loops, UINT64_C(1), __ATOMIC_RELAXED);
    if (busy) {
        __atomic_fetch_add(&g_busy_loops, UINT64_C(1), __ATOMIC_RELAXED);
    }
}

void record_disk_blocks(uint64_t blocks) {
    if (blocks != 0U) saturating_atomic_add(&g_disk_blocks, blocks);
}

void record_graphics_work(uint64_t units) {
    if (units != 0U) saturating_atomic_add(&g_graphics_units, units);
}

ActivitySnapshot sample() {
    SchedulerSample scheduler{};
    static_cast<void>(threading::list(collect_thread, &scheduler));
    const uint64_t ticks = threading::timer_ticks();
    const uint64_t switch_delta = scheduler.switches >= g_previous_switches
        ? scheduler.switches - g_previous_switches : 0U;
    const uint64_t tick_delta = ticks >= g_previous_ticks
        ? ticks - g_previous_ticks : 0U;
    g_previous_switches = scheduler.switches;
    g_previous_ticks = ticks;

    if (tick_delta != 0U) {
        uint64_t scheduler_units = switch_delta;
        const uint64_t runnable_floor = scheduler.runnable == 0U
            ? 0U : static_cast<uint64_t>(scheduler.runnable);
        if (UINT64_MAX - scheduler_units < runnable_floor) {
            scheduler_units = UINT64_MAX;
        } else {
            scheduler_units += runnable_floor;
        }
        g_last_cpu = bounded_percent(
            scheduler_units,
            tick_delta > UINT64_MAX / UINT64_C(2)
                ? UINT64_MAX : tick_delta * UINT64_C(2));
    } else if (scheduler.runnable != 0U) {
        g_last_cpu = scheduler.runnable >= 4U
            ? 100U : scheduler.runnable * 25U;
    } else {
        g_last_cpu = 0U;
    }

    static_cast<void>(__atomic_exchange_n(
        &g_total_loops, UINT64_C(0), __ATOMIC_RELAXED));
    static_cast<void>(__atomic_exchange_n(
        &g_busy_loops, UINT64_C(0), __ATOMIC_RELAXED));
    const uint64_t disk = __atomic_exchange_n(
        &g_disk_blocks, UINT64_C(0), __ATOMIC_RELAXED);
    const uint64_t graphics = __atomic_exchange_n(
        &g_graphics_units, UINT64_C(0), __ATOMIC_RELAXED);

    g_last_disk = disk == 0U
        ? 0U : bounded_percent(disk, UINT64_C(128));

    // Compatibility graphics currently goes through the software compositor
    // and GOP scanout. This measures graphics submission pressure, not a
    // vendor GPU engine's hardware busy counter.
    g_last_gpu = graphics == 0U
        ? 0U : bounded_percent(graphics, UINT64_C(96));

    return ActivitySnapshot{g_last_cpu, g_last_gpu, g_last_disk};
}

} // namespace system_metrics
