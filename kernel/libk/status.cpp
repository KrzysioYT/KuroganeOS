#include "status.hpp"

const char* kstatus_name(KStatus status) {
    switch (status) {
        case KStatus::Ok: return "KSTATUS_OK";
        case KStatus::InvalidArgument: return "KSTATUS_INVALID_ARGUMENT";
        case KStatus::NotFound: return "KSTATUS_NOT_FOUND";
        case KStatus::AlreadyExists: return "KSTATUS_ALREADY_EXISTS";
        case KStatus::NoMemory: return "KSTATUS_NO_MEMORY";
        case KStatus::IoError: return "KSTATUS_IO_ERROR";
        case KStatus::Timeout: return "KSTATUS_TIMEOUT";
        case KStatus::PermissionDenied: return "KSTATUS_PERMISSION_DENIED";
        case KStatus::NotSupported: return "KSTATUS_NOT_SUPPORTED";
        case KStatus::Busy: return "KSTATUS_BUSY";
        case KStatus::Corrupted: return "KSTATUS_CORRUPTED";
        case KStatus::InternalError: return "KSTATUS_INTERNAL_ERROR";
        case KStatus::OutOfRange: return "KSTATUS_OUT_OF_RANGE";
        case KStatus::BufferTooSmall: return "KSTATUS_BUFFER_TOO_SMALL";
        case KStatus::WouldBlock: return "KSTATUS_WOULD_BLOCK";
        case KStatus::Interrupted: return "KSTATUS_INTERRUPTED";
        case KStatus::BadState: return "KSTATUS_BAD_STATE";
        case KStatus::NoDevice: return "KSTATUS_NO_DEVICE";
        case KStatus::DeviceFault: return "KSTATUS_DEVICE_FAULT";
        case KStatus::ReadOnly: return "KSTATUS_READ_ONLY";
        case KStatus::EndOfStream: return "KSTATUS_END_OF_STREAM";
        case KStatus::StaleHandle: return "KSTATUS_STALE_HANDLE";
    }
    return "KSTATUS_UNKNOWN";
}

const char* kstatus_message(KStatus status) {
    switch (status) {
        case KStatus::Ok: return "success";
        case KStatus::InvalidArgument: return "invalid argument";
        case KStatus::NotFound: return "not found";
        case KStatus::AlreadyExists: return "already exists";
        case KStatus::NoMemory: return "out of memory";
        case KStatus::IoError: return "I/O error";
        case KStatus::Timeout: return "operation timed out";
        case KStatus::PermissionDenied: return "permission denied";
        case KStatus::NotSupported: return "operation not supported";
        case KStatus::Busy: return "resource busy";
        case KStatus::Corrupted: return "corrupted data";
        case KStatus::InternalError: return "internal error";
        case KStatus::OutOfRange: return "value out of range";
        case KStatus::BufferTooSmall: return "buffer too small";
        case KStatus::WouldBlock: return "operation would block";
        case KStatus::Interrupted: return "operation interrupted";
        case KStatus::BadState: return "invalid state";
        case KStatus::NoDevice: return "device not present";
        case KStatus::DeviceFault: return "device fault";
        case KStatus::ReadOnly: return "resource is read-only";
        case KStatus::EndOfStream: return "end of stream";
        case KStatus::StaleHandle: return "stale handle";
    }
    return "unknown status";
}
