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
    process::Stat init_stat{};
    assert(process::stat(init, &init_stat) == process::Status::Ok);
    assert(init_stat.job_id != process::job::INVALID_JOB_ID);
    process::job::Stat root_job{};
    assert(process::job::stat(init_stat.job_id, &root_job) == process::job::Status::Ok);
    assert(root_job.owner_pid == init && root_job.member_count == 1U);
    assert(process::spawn_init("/system/init", nullptr) ==
           process::Status::AlreadyInitialized);
    assert(process::spawn("/apps/first", &first) == process::Status::Ok);
    assert(process::spawn("/apps/second", &second) == process::Status::Ok);
    assert(first != second);

    // Handle telemetry must report real runtime ownership instead of the
    // previous permanently-zero placeholder field.
    assert(process::set_handle_count(first, 3U) == process::Status::Ok);
    assert(process::set_handle_count(second, 1U) == process::Status::Ok);
    assert(process::set_handle_count(UINT64_C(0xFFFFFFFF), 2U) ==
           process::Status::NotFound);
    process::Stat first_stat{};
    process::Stat second_stat{};
    assert(process::stat(first, &first_stat) == process::Status::Ok);
    assert(process::stat(second, &second_stat) == process::Status::Ok);
    assert(first_stat.handle_count == 3U);
    assert(second_stat.handle_count == 1U);

    int32_t code = 0;
    assert(process::wait(first, &code) == process::Status::WouldBlock);
    process::RunResult result{};
    assert(process::run_ready(16, &result) == process::Status::Ok);
    assert(result.completed_threads == 3 && result.zombies == 3);

    assert(process::stat(first, &first_stat) == process::Status::Ok);
    assert(process::stat(second, &second_stat) == process::Status::Ok);
    assert(first_stat.state == process::State::Zombie);
    assert(first_stat.observed_pid == first);
    assert(second_stat.observed_pid == second);
    // Runtime owns lifecycle changes. The core must preserve the last measured
    // value until runtime cleanup reports zero, rather than inventing a value.
    assert(first_stat.handle_count == 3U);

    assert(process::set_handle_count(first, 0U) == process::Status::Ok);
    assert(process::stat(first, &first_stat) == process::Status::Ok);
    assert(first_stat.handle_count == 0U);

    assert(process::wait(first, &code) == process::Status::Ok && code == 7);
    assert(process::wait(second, &code) == process::Status::ResourcesBusy);
    assert(process::set_handle_count(second, 0U) == process::Status::Ok);
    assert(process::wait(second, &code) == process::Status::Ok && code == 9);
    assert(process::wait(init, &code) == process::Status::Ok && code == 9);
    assert(process::stat(first, &first_stat) == process::Status::NotFound);
    return 0;
}
