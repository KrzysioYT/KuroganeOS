#pragma once

#include <stdint.h>

// Common, subsystem-independent result codes. Subsystems may retain richer
// private diagnostics, but public foundation interfaces should translate them
// to KStatus at their boundary.
enum class KStatus : uint16_t {
    Ok = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    NoMemory,
    IoError,
    Timeout,
    PermissionDenied,
    NotSupported,
    Busy,
    Corrupted,
    InternalError,
    OutOfRange,
    BufferTooSmall,
    WouldBlock,
    Interrupted,
    BadState,
    NoDevice,
    DeviceFault,
    ReadOnly,
    EndOfStream,
};

constexpr bool kstatus_succeeded(KStatus status) {
    return status == KStatus::Ok;
}

constexpr bool kstatus_failed(KStatus status) {
    return !kstatus_succeeded(status);
}

const char* kstatus_name(KStatus status);
const char* kstatus_message(KStatus status);
