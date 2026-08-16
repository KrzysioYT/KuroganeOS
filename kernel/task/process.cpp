#include "process.hpp"

#include "thread.hpp"

#if !defined(KUROGANE_HOST_TEST)
#include "../user/runtime.hpp"
#endif

namespace process {
namespace {

constexpr uint64_t kSlotMask = UINT64_C(0xFF);

struct Slot {
    ProcessId pid;
    ProcessId parent_pid;
    uint64_t generation;
    State state;
    int32_t exit_code;
    uint64_t observed_pid;
    uint64_t address_space_root;
    uint32_t handle_count;
    threading::ThreadId thread_id;
    char name[MAX_PROCESS_NAME + 1U];
    char executable[MAX_EXECUTABLE_PATH + 1U];
    char working_directory[MAX_EXECUTABLE_PATH + 1U];
};

Slot g_slots[MAX_PROCESSES]{};
bool g_initialized = false;
ProcessId g_current = INVALID_PROCESS_ID;
bool g_init_spawned = false;
#if defined(KUROGANE_HOST_TEST)
HostImageRunner g_host_runner = nullptr;
#endif

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < size; ++index) {
        bytes[index] = 0U;
    }
}

bool path_starts_with(const char* path, const char* prefix) {
    if (path == nullptr || prefix == nullptr) return false;
    size_t index = 0U;
    while (prefix[index] != '\0') {
        if (path[index] != prefix[index]) return false;
        ++index;
    }
    return true;
}

bool copy_path(char* destination, const char* source, size_t capacity) {
    if (source == nullptr || source[0] != '/') {
        return false;
    }
    for (size_t index = 0U; index < capacity; ++index) {
        destination[index] = source[index];
        if (source[index] == '\0') {
            return true;
        }
    }
    return false;
}

void derive_name(Slot& slot) {
    size_t length = 0U;
    size_t component = 0U;
    while (slot.executable[length] != '\0') {
        if (slot.executable[length] == '/') {
            component = length + 1U;
        }
        ++length;
    }
    size_t output = 0U;
    while (component < length && output < MAX_PROCESS_NAME) {
        slot.name[output++] = slot.executable[component++];
    }
    slot.name[output] = '\0';
}

ProcessId allocate_pid(Slot& slot, size_t index) {
    ++slot.generation;
    if (slot.generation == 0U) {
        slot.generation = 1U;
    }
    return (slot.generation << 8U) | (index + 1U);
}

bool find(ProcessId pid, size_t* index) {
    if (pid == INVALID_PROCESS_ID || index == nullptr) {
        return false;
    }
    const uint64_t encoded = pid & kSlotMask;
    if (encoded == 0U || encoded > MAX_PROCESSES) {
        return false;
    }
    *index = static_cast<size_t>(encoded - 1U);
    return g_slots[*index].state != State::Empty &&
        g_slots[*index].pid == pid;
}

int32_t run_image(Slot& slot) {
#if defined(KUROGANE_HOST_TEST)
    if (g_host_runner == nullptr) {
        return -1;
    }
    return g_host_runner(
        slot.executable, slot.pid, &slot.observed_pid);
#else
    user::runtime::Result result{};
    const user::runtime::Status status =
        user::runtime::run(slot.executable, slot.pid, &result);
    slot.observed_pid = result.observed_pid;
    return status == user::runtime::Status::Ok
        ? result.exit_code
        : -1;
#endif
}

void process_entry(void* context) {
    auto* slot = static_cast<Slot*>(context);
    if (slot == nullptr || slot->state != State::Ready) {
        return;
    }
    slot->state = State::Running;
    g_current = slot->pid;
    slot->exit_code = run_image(*slot);
    g_current = INVALID_PROCESS_ID;
    slot->state = State::Zombie;
}

void snapshot(const Slot& slot, Stat& output) {
    output = {};
    output.pid = slot.pid;
    output.parent_pid = slot.parent_pid;
    output.state = slot.state;
    output.exit_code = slot.exit_code;
    output.observed_pid = slot.observed_pid;
    output.address_space_root = slot.address_space_root;
    output.main_thread = slot.thread_id;
    output.handle_count = slot.handle_count;
    threading::Stat thread_stat{};
    if (threading::stat(slot.thread_id, &thread_stat) ==
        threading::Status::Ok) {
        output.address_space_root = thread_stat.address_space_root;
    }
    for (size_t index = 0U; index <= MAX_PROCESS_NAME; ++index) {
        output.name[index] = slot.name[index];
        if (slot.name[index] == '\0') break;
    }
    for (size_t index = 0U; index <= MAX_EXECUTABLE_PATH; ++index) {
        output.executable[index] = slot.executable[index];
        if (slot.executable[index] == '\0') break;
    }
    for (size_t index = 0U; index <= MAX_EXECUTABLE_PATH; ++index) {
        output.working_directory[index] = slot.working_directory[index];
        if (slot.working_directory[index] == '\0') break;
    }
}

} // namespace

Status initialize(
#if defined(KUROGANE_HOST_TEST)
    HostImageRunner runner
#endif
) {
    if (g_initialized) {
        return Status::AlreadyInitialized;
    }
#if defined(KUROGANE_HOST_TEST)
    if (runner == nullptr) {
        return Status::InvalidArgument;
    }
    g_host_runner = runner;
#endif
    const threading::Status thread_status = threading::initialize();
    if (thread_status != threading::Status::Ok &&
        thread_status != threading::Status::AlreadyInitialized) {
        return Status::SchedulerFailed;
    }
    clear_bytes(g_slots, sizeof(g_slots));
    g_current = INVALID_PROCESS_ID;
    g_init_spawned = false;
    g_initialized = true;
    return Status::Ok;
}

Status spawn(const char* executable, ProcessId* pid) {
    if (pid != nullptr) {
        *pid = INVALID_PROCESS_ID;
    }
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (executable == nullptr || executable[0] != '/') {
        return Status::InvalidArgument;
    }
#if !defined(KUROGANE_HOST_TEST)
    // 3.2 desktop ownership rule: GUI programs belong to a userspace session
    // tree. The old kernel main() still issues five anonymous /gui/* launch
    // requests for compatibility with pre-3.0 logs. Acknowledge those no-op
    // requests without allocating process slots; real GUI launches always
    // originate from PID1/Login/Home or descendants and therefore have a
    // non-zero current process.
    if (current() == INVALID_PROCESS_ID && path_starts_with(executable, "/gui/")) {
        return Status::Ok;
    }
#endif
    size_t index = MAX_PROCESSES;
    for (size_t candidate = 0U; candidate < MAX_PROCESSES; ++candidate) {
        if (g_slots[candidate].state == State::Empty) {
            index = candidate;
            break;
        }
    }
    if (index == MAX_PROCESSES) {
        return Status::CapacityReached;
    }
    Slot& slot = g_slots[index];
    const uint64_t generation = slot.generation;
    clear_bytes(&slot, sizeof(slot));
    slot.generation = generation;
    if (!copy_path(
            slot.executable,
            executable,
            sizeof(slot.executable))) {
        return Status::PathTooLong;
    }
    slot.pid = allocate_pid(slot, index);
    slot.parent_pid = current();
    slot.working_directory[0] = '/';
    slot.working_directory[1] = '\0';
    slot.state = State::Ready;
    derive_name(slot);
    if (threading::create_for_process(
            slot.name,
            process_entry,
            &slot,
            slot.pid,
            0U,
            &slot.thread_id) != threading::Status::Ok) {
        const uint64_t retained_generation = slot.generation;
        clear_bytes(&slot, sizeof(slot));
        slot.generation = retained_generation;
        return Status::ThreadCreationFailed;
    }
    if (pid != nullptr) {
        *pid = slot.pid;
    }
    return Status::Ok;
}

Status spawn_init(const char* executable, ProcessId* pid) {
    if (pid != nullptr) *pid = INVALID_PROCESS_ID;
    if (!g_initialized) return Status::NotInitialized;
    if (g_init_spawned || g_slots[0].state != State::Empty) {
        return Status::AlreadyInitialized;
    }
    if (executable == nullptr || executable[0] != '/') {
        return Status::InvalidArgument;
    }

    Slot& slot = g_slots[0];
    const uint64_t generation = slot.generation;
    clear_bytes(&slot, sizeof(slot));
    slot.generation = generation;
    if (!copy_path(slot.executable, executable, sizeof(slot.executable))) {
        return Status::PathTooLong;
    }
    slot.pid = UINT64_C(1);
    slot.parent_pid = INVALID_PROCESS_ID;
    slot.working_directory[0] = '/';
    slot.working_directory[1] = '\0';
    slot.state = State::Ready;
    derive_name(slot);
    if (threading::create_for_process(
            slot.name, process_entry, &slot, slot.pid, 0U,
            &slot.thread_id) != threading::Status::Ok) {
        clear_bytes(&slot, sizeof(slot));
        slot.generation = generation;
        return Status::ThreadCreationFailed;
    }
    g_init_spawned = true;
    if (pid != nullptr) *pid = slot.pid;
    return Status::Ok;
}

Status run_preemptive_for(
    uint64_t maximum_timer_ticks,
    threading::PreemptRunResult* result) {
    if (!g_initialized) return Status::NotInitialized;
    const threading::Status status = maximum_timer_ticks == 0U
        ? threading::run_preemptive(result)
        : threading::run_preemptive_for(maximum_timer_ticks, result);
    return status == threading::Status::Ok
        ? Status::Ok
        : Status::SchedulerFailed;
}

Status run_ready(uint64_t switch_budget, RunResult* result) {
    if (result != nullptr) {
        *result = {};
    }
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    threading::RunResult thread_result{};
    const threading::Status status =
        threading::run_until_idle(switch_budget, &thread_result);
    if (status != threading::Status::Ok &&
        status != threading::Status::BudgetExhausted) {
        return Status::SchedulerFailed;
    }
    size_t zombies = 0U;
    for (const Slot& slot : g_slots) {
        if (slot.state == State::Zombie) ++zombies;
    }
    if (result != nullptr) {
        result->context_switches = thread_result.switches;
        result->completed_threads = thread_result.completed;
        result->zombies = zombies;
    }
    return status == threading::Status::Ok
        ? Status::Ok
        : Status::WouldBlock;
}

Status wait(ProcessId pid, int32_t* exit_code) {
    if (!g_initialized) return Status::NotInitialized;
    if (exit_code == nullptr) return Status::InvalidArgument;
    size_t index = 0U;
    if (!find(pid, &index)) return Status::NotFound;
    Slot& slot = g_slots[index];
    const ProcessId caller = current();
    if (caller != INVALID_PROCESS_ID && slot.parent_pid != caller) {
        return Status::PermissionDenied;
    }
    if (slot.state != State::Zombie) return Status::WouldBlock;
    *exit_code = slot.exit_code;
    const uint64_t generation = slot.generation;
    clear_bytes(&slot, sizeof(slot));
    slot.generation = generation;
    slot.state = State::Empty;
    return Status::Ok;
}

Status terminate(ProcessId pid, int32_t exit_code) {
    if (!g_initialized) return Status::NotInitialized;
    size_t index = 0U;
    if (!find(pid, &index)) return Status::NotFound;
    Slot& slot = g_slots[index];
    if (slot.state == State::Zombie) return Status::Ok;
#if defined(KUROGANE_HOST_TEST)
    static_cast<void>(exit_code);
    return Status::RunnerFailed;
#else
    return user::runtime::request_termination(pid, exit_code)
        ? Status::Ok
        : Status::RunnerFailed;
#endif
}

Status stat(ProcessId pid, Stat* output) {
    if (!g_initialized) return Status::NotInitialized;
    if (output == nullptr) return Status::InvalidArgument;
    size_t index = 0U;
    if (!find(pid, &index)) {
        *output = {};
        return Status::NotFound;
    }
    snapshot(g_slots[index], *output);
    return Status::Ok;
}

Status list(ListCallback callback, void* context) {
    if (!g_initialized) return Status::NotInitialized;
    if (callback == nullptr) return Status::InvalidArgument;
    for (const Slot& slot : g_slots) {
        if (slot.state == State::Empty) continue;
        Stat value{};
        snapshot(slot, value);
        if (!callback(value, context)) break;
    }
    return Status::Ok;
}

ProcessId current() {
#if defined(KUROGANE_HOST_TEST)
    return g_current;
#else
    const ProcessId threaded = threading::current_process();
    return threaded;
#endif
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "not initialized";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::InvalidArgument: return "invalid argument";
        case Status::PathTooLong: return "executable path too long";
        case Status::CapacityReached: return "process capacity reached";
        case Status::ThreadCreationFailed: return "thread creation failed";
        case Status::NotFound: return "process not found";
        case Status::PermissionDenied: return "process is not a child";
        case Status::WouldBlock: return "process has not exited";
        case Status::RunnerFailed: return "image runner failed";
        case Status::SchedulerFailed: return "thread scheduler failed";
    }
    return "unknown process status";
}

} // namespace process
