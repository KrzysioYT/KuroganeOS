#pragma once

#include <stddef.h>
#include <stdint.h>

namespace threading {
struct PreemptRunResult;
}

namespace process {

using ProcessId = uint64_t;
constexpr ProcessId INVALID_PROCESS_ID = 0U;
constexpr size_t MAX_PROCESSES = 16U;
constexpr size_t MAX_PROCESS_NAME = 31U;
constexpr size_t MAX_EXECUTABLE_PATH = 255U;

enum class State : uint8_t {
    Empty = 0,
    New,
    Ready,
    Running,
    Blocked,
    Sleeping,
    Zombie,
    Terminated
};

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    PathTooLong,
    CapacityReached,
    ThreadCreationFailed,
    NotFound,
    PermissionDenied,
    WouldBlock,
    RunnerFailed,
    SchedulerFailed
};

struct Stat {
    ProcessId pid;
    ProcessId parent_pid;
    State state;
    int32_t exit_code;
    uint64_t observed_pid;
    uint64_t address_space_root;
    uint64_t main_thread;
    uint32_t handle_count;
    char name[MAX_PROCESS_NAME + 1U];
    char executable[MAX_EXECUTABLE_PATH + 1U];
    char working_directory[MAX_EXECUTABLE_PATH + 1U];
};

struct RunResult {
    uint64_t context_switches;
    uint64_t completed_threads;
    size_t zombies;
};

using ListCallback = bool (*)(const Stat& stat, void* context);

#if defined(KUROGANE_HOST_TEST)
using HostImageRunner = int32_t (*)(
    const char* executable,
    ProcessId pid,
    uint64_t* observed_pid);
#endif

Status initialize(
#if defined(KUROGANE_HOST_TEST)
    HostImageRunner runner = nullptr
#endif
);
Status spawn(const char* executable, ProcessId* pid);
// Creates the system root process with the stable public identity PID 1.
// It may be called only once and only while process slot zero is unused.
Status spawn_init(const char* executable, ProcessId* pid);
Status run_ready(uint64_t switch_budget, RunResult* result = nullptr);
Status run_preemptive_for(
    uint64_t maximum_timer_ticks,
    threading::PreemptRunResult* result = nullptr);
Status wait(ProcessId pid, int32_t* exit_code);
Status terminate(ProcessId pid, int32_t exit_code);
Status stat(ProcessId pid, Stat* output);
Status set_working_directory(ProcessId pid, const char* path);
Status list(ListCallback callback, void* context);
ProcessId current();
const char* status_message(Status status);

} // namespace process
