#define KUROGANE_HOST_TEST 1

#include "../kernel/task/process.hpp"

#include <cassert>
#include <cstring>

namespace {
process::ProcessId g_inherited_child = process::INVALID_PROCESS_ID;

int32_t run_image(
    const char* executable,
    process::ProcessId pid,
    uint64_t* observed_pid) {
    assert(executable != nullptr);
    assert(observed_pid != nullptr);
    *observed_pid = pid;
    if (std::strcmp(executable, "/system/init") == 0) {
        assert(process::set_working_directory(pid, "/home") == process::Status::Ok);
        assert(process::spawn("/apps/inherited", &g_inherited_child) ==
               process::Status::Ok);
        assert(g_inherited_child != process::INVALID_PROCESS_ID);
        return 9;
    }
    if (std::strcmp(executable, "/apps/first") == 0) return 11;
    if (std::strcmp(executable, "/apps/second") == 0) return 22;
    if (std::strcmp(executable, "/apps/inherited") == 0) return 33;
    return -7;
}
}

int main() {
    assert(process::initialize(run_image) == process::Status::Ok);
    assert(process::spawn(nullptr, nullptr) == process::Status::InvalidArgument);
    process::ProcessId ignored_gui = 999U;
    assert(process::spawn("/gui/login", &ignored_gui) == process::Status::Ok);
    assert(ignored_gui == process::INVALID_PROCESS_ID);

    process::ProcessId init = process::INVALID_PROCESS_ID;
    assert(process::spawn_init("/system/init", &init) == process::Status::Ok);
    assert(init == 1U);
    assert(process::spawn_init("/system/init", nullptr) ==
           process::Status::AlreadyInitialized);

    process::Stat init_before{};
    assert(process::stat(init, &init_before) == process::Status::Ok);
    assert(std::strcmp(init_before.working_directory, "/") == 0);

    process::ProcessId first = process::INVALID_PROCESS_ID;
    process::ProcessId second = process::INVALID_PROCESS_ID;
    assert(process::spawn("/apps/first", &first) == process::Status::Ok);
    assert(process::spawn("/apps/second", &second) == process::Status::Ok);
    assert(first != second);

    process::RunResult run{};
    assert(process::run_ready(32U, &run) == process::Status::Ok);
    assert(run.completed_threads == 4U);
    assert(run.zombies == 4U);

    process::Stat init_after{};
    assert(process::stat(init, &init_after) == process::Status::Ok);
    assert(std::strcmp(init_after.working_directory, "/home") == 0);

    process::Stat inherited{};
    assert(process::stat(g_inherited_child, &inherited) == process::Status::Ok);
    assert(inherited.parent_pid == init);
    assert(std::strcmp(inherited.working_directory, "/home") == 0);
    assert(inherited.observed_pid == g_inherited_child);

    int32_t code = 0;
    assert(process::wait(init, &code) == process::Status::Ok);
    assert(code == 9);
    assert(process::wait(first, &code) == process::Status::Ok);
    assert(code == 11);
    assert(process::wait(second, &code) == process::Status::Ok);
    assert(code == 22);
    assert(process::wait(g_inherited_child, &code) == process::Status::Ok);
    assert(code == 33);

    process::Stat missing{};
    assert(process::stat(first, &missing) == process::Status::NotFound);
}
