#include "../kernel/core/system_metrics.hpp"
#include "../kernel/task/thread.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
namespace { uint64_t clock_ticks = 0U; uint64_t switches = 0U; }
namespace threading {
uint64_t timer_ticks() { return clock_ticks; }
Status list(ListCallback callback, void* context) {
    Stat stat{};
    stat.state = State::Ready;
    stat.switches = switches;
    (void)callback(stat, context);
    return Status::Ok;
}
}
int main() {
    auto reading = system_metrics::sample();
    assert(reading.cpu_percent == 0U && reading.disk_percent == 0U);
    system_metrics::record_disk_blocks(32U);
    system_metrics::record_graphics_work(24U);
    switches = 400U;
    clock_ticks = 50U;
    reading = system_metrics::sample();
    assert(reading.cpu_percent == 0U && reading.disk_percent == 0U);
    clock_ticks = 100U;
    reading = system_metrics::sample();
    assert(reading.cpu_percent == 100U);
    assert(reading.disk_percent == 25U);
    assert(reading.gpu_percent == 25U);
    auto second = system_metrics::sample();
    assert(second.cpu_percent == reading.cpu_percent);
    assert(second.disk_percent == reading.disk_percent);
    assert(second.gpu_percent == reading.gpu_percent);
    system_metrics::record_disk_blocks(64U);
    clock_ticks = 199U;
    assert(system_metrics::sample().disk_percent == 25U);
    clock_ticks = 200U;
    switches = 400U;
    reading = system_metrics::sample();
    assert(reading.disk_percent == 50U && reading.gpu_percent == 0U);
    assert(system_metrics::sample().disk_percent == 50U);
    std::puts("system metrics shared-window sampling: PASS");
    return 0;
}
