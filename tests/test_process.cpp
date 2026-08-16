#include "../kernel/task/process.hpp"

#include <cassert>
#include <cstring>

extern "C" void x86_64_thread_start_interrupt_frame(
    void*) {
    __builtin_trap();
}

extern "C" [[noreturn]] void x86_64_thread_resume_interrupt_frame(
    void*) {
    __builtin_trap();
}

extern "C" [[noreturn]] void x86_64_thread_return_from_preemptive_run() {
    __builtin_trap();
}

namespace {

int32_t run_image(
    const char* executable,
    process::ProcessId pid,
    uint64_t* observed_pid) {
    assert(process::current() == pid);
    *observed_pid = pid;
    return std::strcmp(executable, "/apps/first") == 0 ? 7 : 9;
}

} // namespace

int main() {
    assert(process::initialize(run_image) == process::Status::Ok);
    process::ProcessId init = 0;
    process::ProcessId first = 0;
    process::ProcessId second = 0;
    assert(process::spawn_init("/system/init", &init) == process::Status::Ok);
    assert(init == 1);
    assert(process::spawn_init("/system/init", nullptr) ==
           process::Status::AlreadyInitialized);
    assert(process::spawn("/apps/first", &first) == process::Status::Ok);
    assert(process::spawn("/apps/second", &second) == process::Status::Ok);
    assert(first != second);

    int32_t code = 0;
    assert(process::wait(first, &code) == process::Status::WouldBlock);
    process::RunResult result{};
    assert(process::run_ready(16, &result) == process::Status::Ok);
    assert(result.completed_threads == 3 && result.zombies == 3);

    process::Stat first_stat{};
    process::Stat second_stat{};
    assert(process::stat(first, &first_stat) == process::Status::Ok);
    assert(process::stat(second, &second_stat) == process::Status::Ok);
    assert(first_stat.state == process::State::Zombie);
    assert(first_stat.observed_pid == first);
    assert(second_stat.observed_pid == second);

    assert(process::wait(first, &code) == process::Status::Ok && code == 7);
    assert(process::wait(second, &code) == process::Status::Ok && code == 9);
    assert(process::wait(init, &code) == process::Status::Ok && code == 9);
    assert(process::stat(first, &first_stat) == process::Status::NotFound);
    return 0;
}
