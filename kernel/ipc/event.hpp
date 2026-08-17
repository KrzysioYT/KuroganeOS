#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ipc::event {

using ProcessId = uint64_t;
using Handle = uint64_t;

constexpr ProcessId INVALID_PROCESS_ID = 0U;
constexpr Handle INVALID_HANDLE = 0U;
constexpr size_t MAX_EVENTS = 32U;
constexpr size_t MAX_GRANTS_PER_EVENT = 8U;

enum class ResetMode : uint8_t {
    Auto = 0,
    Manual = 1,
};

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    InvalidArgument,
    CapacityReached,
    StaleHandle,
    AccessDenied,
    AlreadyGranted,
    WouldBlock,
};

Status initialize();
Status create(ProcessId owner_pid, ResetMode mode, bool signaled, Handle* handle);
Status grant(ProcessId owner_pid, Handle handle, ProcessId target_pid);
Status signal(ProcessId pid, Handle handle);
Status reset(ProcessId pid, Handle handle);
Status poll(ProcessId pid, Handle handle);
Status close(ProcessId pid, Handle handle);
void release_process(ProcessId pid);
const char* status_message(Status status);

} // namespace ipc::event
