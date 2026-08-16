#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../arch/x86_64/interrupts.hpp"

namespace user::runtime {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    CpuUnsupported,
    InterruptRegistrationFailed,
    NotInitialized,
    Busy,
    RootUnavailable,
    FileNotFound,
    FileTooLarge,
    FileReadFailed,
    OutOfMemory,
    AddressSpaceFailed,
    ElfLoadFailed,
    StackMappingFailed,
    TrampolineMappingFailed,
    AddressSpaceActivationFailed,
    CleanupFailed,
    ResourceLeak
};

struct Result {
    int32_t exit_code;
    uint8_t fault_vector;
    uint64_t bytes_written;
    uint64_t observed_pid;
    uint64_t observed_tid;
    bool entered_ring3;
    bool invalid_pointer_rejected;
    bool fault_isolated;
    bool resources_reclaimed;
};

Status initialize();
bool initialized();
Status run(const char* path, Result* result);
Status run(const char* path, uint64_t pid, Result* result);
void set_process_identity(uint64_t pid);
uint64_t process_identity();
bool request_termination(uint64_t pid, int32_t exit_code);

// Called before the kernel panic path. Returns true only for an exception
// belonging to the active ring-3 image and redirects it to SYS_EXIT.
bool handle_exception(
    arch::x86_64::interrupts::InterruptFrame& frame);

const char* status_message(Status status);

} // namespace user::runtime
