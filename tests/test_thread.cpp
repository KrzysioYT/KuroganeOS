#include "../kernel/task/thread.hpp"

#include <cassert>
#include <cstdint>

extern "C" void x86_64_thread_start_interrupt_frame(
    arch::x86_64::interrupts::InterruptFrame*) {
    __builtin_trap();
}

extern "C" [[noreturn]] void x86_64_thread_resume_interrupt_frame(
    arch::x86_64::interrupts::InterruptFrame*) {
    __builtin_trap();
}

extern "C" [[noreturn]] void x86_64_thread_return_from_preemptive_run() {
    __builtin_trap();
}

namespace {

char trace[8]{};
size_t trace_size = 0;
uintptr_t stack_a = 0;
uintptr_t stack_b = 0;
bool process_retired = false;
size_t churn_children_completed = 0U;
size_t churn_children_created = 0U;
bool churn_failed = false;

void append(char value) {
    trace[trace_size++] = value;
}

void first(void*) {
    uint64_t local = 0xA1;
    stack_a = reinterpret_cast<uintptr_t>(&local);
    append('A');
    assert(threading::yield() == threading::Status::Ok);
    append('C');
}

void second(void*) {
    uint64_t local = 0xB1;
    stack_b = reinterpret_cast<uintptr_t>(&local);
    append('B');
    assert(threading::yield() == threading::Status::Ok);
    append('D');
}

void process_retire_probe(void*) {
    assert(threading::current_process() == UINT64_C(42));
    assert(threading::retire_current_user_frame() == threading::Status::Ok);
    process_retired = true;
}

void churn_child(void*) { ++churn_children_completed; }

void churn_parent(void*) {
    constexpr size_t iterations = threading::MAX_THREADS * 4U;
    for (size_t iteration = 0U; iteration < iterations; ++iteration) {
        threading::ThreadId child = threading::INVALID_THREAD_ID;
        if (threading::create_for_process(
                "churn-child", churn_child, nullptr,
                UINT64_C(101), 0U, &child) != threading::Status::Ok ||
            child == threading::INVALID_THREAD_ID) {
            churn_failed = true;
            return;
        }
        ++churn_children_created;
        // Give the child a scheduling turn. It returns immediately and
        // becomes Terminated before the parent's next create call.
        if (threading::yield() != threading::Status::Ok) {
            churn_failed = true;
            return;
        }
    }
}

} // namespace

int main() {
    assert(threading::initialize() == threading::Status::Ok);
    threading::ThreadId first_id = 0;
    threading::ThreadId second_id = 0;
    assert(threading::create("first", first, nullptr, &first_id) ==
           threading::Status::Ok);
    assert(threading::create("second", second, nullptr, &second_id) ==
           threading::Status::Ok);
    assert(first_id != second_id);

    threading::Stat first_stat{};
    threading::Stat second_stat{};
    assert(threading::stat(first_id, &first_stat) == threading::Status::Ok);
    assert(threading::stat(second_id, &second_stat) == threading::Status::Ok);
    assert(first_stat.stack_bottom != second_stat.stack_bottom);

    threading::RunResult result{};
    assert(threading::run_until_idle(16, &result) == threading::Status::Ok);
    assert(result.completed == 2);
    assert(result.ready_remaining == 0);
    assert(trace_size == 4);
    assert(trace[0] == 'A' && trace[1] == 'B' &&
           trace[2] == 'C' && trace[3] == 'D');
    assert(stack_a >= first_stat.stack_bottom && stack_a < first_stat.stack_top);
    assert(stack_b >= second_stat.stack_bottom && stack_b < second_stat.stack_top);
    assert(stack_a != stack_b);
    assert(threading::current() == threading::INVALID_THREAD_ID);
    assert(threading::retire_current_user_frame() == threading::Status::NotRunning);

    threading::ThreadId process_id = 0;
    assert(threading::create_for_process(
               "retire-probe", process_retire_probe, nullptr, UINT64_C(42), 0U,
               &process_id) == threading::Status::Ok);
    threading::RunResult process_result{};
    assert(threading::run_until_idle(8, &process_result) == threading::Status::Ok);
    assert(process_result.completed == 1U);
    assert(process_retired);

    // Reproduce the Ring-3 spawn/wait pattern: keep one parent alive
    // across far more child lifetimes than the scheduler slot capacity.
    threading::ThreadId churn = threading::INVALID_THREAD_ID;
    assert(threading::create_for_process(
               "churn-parent", churn_parent, nullptr, UINT64_C(100), 0U,
               &churn) == threading::Status::Ok);
    threading::RunResult churn_result{};
    assert(threading::run_until_idle(
               threading::MAX_THREADS * 16U, &churn_result) ==
           threading::Status::Ok);
    assert(!churn_failed);
    assert(churn_children_created == threading::MAX_THREADS * 4U);
    assert(churn_children_completed == churn_children_created);
    assert(churn_result.completed == churn_children_created + 1U);
    return 0;
}
