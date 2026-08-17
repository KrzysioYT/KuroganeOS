#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ipc::shared_memory {

using ProcessId = uint64_t;
using Handle = uint64_t;

constexpr ProcessId INVALID_PROCESS_ID = 0U;
constexpr Handle INVALID_HANDLE = 0U;
constexpr size_t MAX_OBJECTS = 16U;
constexpr size_t MAX_PAGES_PER_OBJECT = 16U;
constexpr size_t MAX_GRANTS_PER_OBJECT = 8U;
constexpr size_t PAGE_SIZE = 4096U;

struct View {
    void* frames[MAX_PAGES_PER_OBJECT];
    size_t page_count;
    size_t size;
};

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    InvalidArgument,
    OutOfRange,
    OutOfMemory,
    CapacityReached,
    StaleHandle,
    AccessDenied,
    AlreadyGranted,
    NotFound,
    Busy,
};

Status initialize();
Status create(ProcessId owner_pid, size_t size, Handle* handle);
Status grant(ProcessId owner_pid, Handle handle, ProcessId target_pid);
Status acquire(ProcessId pid, Handle handle, View* view);
Status release(ProcessId pid, Handle handle);
Status close(ProcessId pid, Handle handle);
void release_process(ProcessId pid);
const char* status_message(Status status);

} // namespace ipc::shared_memory
