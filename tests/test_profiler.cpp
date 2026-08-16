#include "../kernel/apps/framework.hpp"
#include "../kernel/diagnostics/profiler.hpp"
#include "../kernel/memory/allocator.hpp"
#include "../kernel/memory/physical_memory.hpp"
#include "../kernel/net/service.hpp"
#include "../kernel/task/scheduler.hpp"

#include <stddef.h>
#include <stdint.h>

namespace {

size_t g_task_runs = 0;
size_t g_key_events = 0;

void task_callback(void*) {
    ++g_task_runs;
}

bool application_start(const char*) {
    return true;
}

void application_key(char) {
    ++g_key_events;
}

bool text_equals(const char* left, const char* right) {
    if (!left || !right) {
        return left == right;
    }
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

} // namespace

int main() {
    using namespace diagnostics::profiler;

    Snapshot untouched = {};
    untouched.sequence = UINT64_C(0x1122334455667788);
    if (is_enabled() ||
        capture(nullptr) != Status::InvalidArgument ||
        capture(&untouched) != Status::Disabled ||
        untouched.sequence != UINT64_C(0x1122334455667788)) {
        return 1;
    }

    reset_sequence();
    set_enabled(true);
    Snapshot partial = {};
    if (capture(&partial) != Status::Partial ||
        partial.version != SNAPSHOT_VERSION ||
        partial.size != sizeof(Snapshot) ||
        partial.sequence != 1 ||
        partial.available_sources != SOURCE_APPLICATIONS ||
        partial.unavailable_sources !=
            (ALL_SOURCES & ~SOURCE_APPLICATIONS)) {
        return 2;
    }

    alignas(64) static uint8_t heap[8192] = {};
    memory::init_kernel_heap(heap, sizeof(heap));
    void* allocation = memory::kmalloc(96, 32);
    if (!allocation) {
        return 3;
    }

    memory::init_physical_memory(
        static_cast<uintptr_t>(0x100000),
        32 * 4096,
        4096);
    void* frame = memory::alloc_frame();
    if (!frame) {
        return 4;
    }

    if (scheduler::initialize(10) != scheduler::Status::Ok) {
        return 5;
    }
    scheduler::TaskId task_id = scheduler::INVALID_TASK_ID;
    if (scheduler::create(
            "profiled",
            task_callback,
            nullptr,
            5,
            &task_id) != scheduler::Status::Ok ||
        scheduler::tick(15) != scheduler::Status::Ok) {
        return 6;
    }
    scheduler::RunResult run_result = {};
    if (scheduler::run_pending(1, &run_result) != scheduler::Status::Ok ||
        g_task_runs != 1) {
        return 7;
    }

    if (net::service::initialize() != net::Status::Ok ||
        net::service::ping_loopback(1) != net::Status::Ok) {
        return 8;
    }

    applications::initialize();
    const applications::Definition application = {
        "profiler-test",
        "Profiler test application",
        application_start,
        application_key,
        nullptr,
        nullptr,
        nullptr
    };
    if (applications::register_application(application) !=
            applications::Status::Ok ||
        applications::launch("profiler-test") != applications::Status::Ok) {
        return 9;
    }
    applications::dispatch_key('x');

    Snapshot first = {};
    if (capture(&first) != Status::Ok ||
        first.sequence != 2 ||
        first.available_sources != ALL_SOURCES ||
        first.unavailable_sources != 0 ||
        first.available_capabilities != IMPLEMENTED_CAPABILITIES ||
        first.scheduler_tick != 15 ||
        first.scheduler.metrics.tasks_created != 1 ||
        first.scheduler.metrics.callbacks_executed != 1 ||
        first.scheduler.task_count != 1 ||
        first.scheduler.waiting_tasks != 1 ||
        first.heap.used_bytes != 96 ||
        first.heap.live_allocations != 1 ||
        first.physical_memory.base != static_cast<uintptr_t>(0x100000) ||
        first.physical_memory.frame_size != 4096 ||
        first.physical_memory.total_frames != 32 ||
        first.physical_memory.used_frames != 1 ||
        first.network.stats.frames_received != 2 ||
        first.network.stats.frames_transmitted != 2 ||
        first.applications.registered_applications != 1 ||
        !first.applications.running ||
        !text_equals(first.applications.active_name, "profiler-test") ||
        g_key_events != 1) {
        return 10;
    }

    if (scheduler::tick(20) != scheduler::Status::Ok ||
        scheduler::run_pending(1, &run_result) != scheduler::Status::Ok ||
        net::service::ping_loopback(2) != net::Status::Ok) {
        return 11;
    }
    void* second_allocation = memory::kmalloc(48);
    if (!second_allocation) {
        return 12;
    }

    Snapshot second = {};
    if (capture(&second) != Status::Ok ||
        second.sequence != first.sequence + 1 ||
        second.scheduler_tick != 20 ||
        second.scheduler.metrics.callbacks_executed !=
            first.scheduler.metrics.callbacks_executed + 1 ||
        second.heap.used_bytes != first.heap.used_bytes + 48 ||
        second.heap.live_allocations != first.heap.live_allocations + 1 ||
        second.network.stats.frames_received !=
            first.network.stats.frames_received + 2 ||
        second.network.stats.frames_transmitted !=
            first.network.stats.frames_transmitted + 2) {
        return 13;
    }

    set_enabled(false);
    Snapshot disabled = {};
    disabled.sequence = UINT64_C(0xaabbccdd);
    if (capture(&disabled) != Status::Disabled ||
        disabled.sequence != UINT64_C(0xaabbccdd)) {
        return 14;
    }

    set_enabled(true);
    reset_sequence();
    Snapshot reset = {};
    if (capture(&reset) != Status::Ok ||
        reset.sequence != 1 ||
        implemented_capabilities() != IMPLEMENTED_CAPABILITIES ||
        known_unimplemented_capabilities() !=
            KNOWN_UNIMPLEMENTED_CAPABILITIES ||
        !text_equals(
            status_message(Status::Disabled),
            "profiler disabled")) {
        return 15;
    }

    memory::kfree(second_allocation);
    memory::kfree(allocation);
    memory::free_frame(frame);
    if (applications::stop() != applications::Status::Ok) {
        return 16;
    }
    return 0;
}
