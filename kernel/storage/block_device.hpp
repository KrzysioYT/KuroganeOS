#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../libk/status.hpp"
#include "../core/system_metrics.hpp"

namespace storage::block {

// Status values are shared by the validation wrappers and device backends.
// A backend must return one of these values synchronously: returning Ok means
// that the complete request has finished before the callback returns.
enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidGeometry,
    MissingCallback,
    OutOfRange,
    ArithmeticOverflow,
    BufferTooSmall,
    ReadOnly,
    Unsupported,
    IoError,
    DeviceFault,
    BackendFailure,
    NoDevice,
    DeviceBusy,
    TimedOut,
    ControllerFault,
    CommandFailed,
    AddressNotSupported
};

using ReadBlocksCallback = Status (*)(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    void* destination);

using WriteBlocksCallback = Status (*)(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    const void* source);

using FlushCallback = Status (*)(void* context);

struct Device {
    void* context;
    uint32_t sector_size;
    uint64_t sector_count;
    ReadBlocksCallback read;
    WriteBlocksCallback write;
    FlushCallback flush;
};

constexpr bool is_known_status(Status status) {
    switch (status) {
        case Status::Ok:
        case Status::InvalidArgument:
        case Status::InvalidGeometry:
        case Status::MissingCallback:
        case Status::OutOfRange:
        case Status::ArithmeticOverflow:
        case Status::BufferTooSmall:
        case Status::ReadOnly:
        case Status::Unsupported:
        case Status::IoError:
        case Status::DeviceFault:
        case Status::BackendFailure:
        case Status::NoDevice:
        case Status::DeviceBusy:
        case Status::TimedOut:
        case Status::ControllerFault:
        case Status::CommandFailed:
        case Status::AddressNotSupported:
            return true;
    }

    return false;
}

inline Status normalize_backend_status(Status status) {
    return is_known_status(status) ? status : Status::BackendFailure;
}

inline Status validate(const Device* device) {
    if (device == nullptr) {
        return Status::InvalidArgument;
    }
    if (device->sector_size == 0U || device->sector_count == 0U) {
        return Status::InvalidGeometry;
    }
    if (device->read == nullptr || device->write == nullptr ||
        device->flush == nullptr) {
        return Status::MissingCallback;
    }

    return Status::Ok;
}

inline Status validate_transfer(
    const Device* device,
    uint64_t first_block,
    uint64_t block_count,
    const void* buffer,
    size_t buffer_size) {
    const Status device_status = validate(device);
    if (device_status != Status::Ok) {
        return device_status;
    }
    if (buffer == nullptr || block_count == 0U) {
        return Status::InvalidArgument;
    }

    // Subtraction after the first comparison avoids overflowing first+count.
    if (first_block >= device->sector_count ||
        block_count > device->sector_count - first_block) {
        return Status::OutOfRange;
    }

    const uint64_t maximum_size = static_cast<uint64_t>(SIZE_MAX);
    if (block_count > maximum_size /
            static_cast<uint64_t>(device->sector_size)) {
        return Status::ArithmeticOverflow;
    }

    const size_t required_size = static_cast<size_t>(
        block_count * static_cast<uint64_t>(device->sector_size));
    if (buffer_size < required_size) {
        return Status::BufferTooSmall;
    }

    return Status::Ok;
}

inline Status read_blocks(
    const Device* device,
    uint64_t first_block,
    uint64_t block_count,
    void* destination,
    size_t destination_size) {
    const Status status = validate_transfer(
        device,
        first_block,
        block_count,
        destination,
        destination_size);
    if (status != Status::Ok) {
        return status;
    }

    const Status result = normalize_backend_status(
        device->read(device->context, first_block, block_count, destination));
    if (result == Status::Ok) {
        system_metrics::record_disk_blocks(block_count);
    }
    return result;
}

inline Status write_blocks(
    const Device* device,
    uint64_t first_block,
    uint64_t block_count,
    const void* source,
    size_t source_size) {
    const Status status = validate_transfer(
        device,
        first_block,
        block_count,
        source,
        source_size);
    if (status != Status::Ok) {
        return status;
    }

    const Status result = normalize_backend_status(
        device->write(device->context, first_block, block_count, source));
    if (result == Status::Ok) {
        system_metrics::record_disk_blocks(block_count);
    }
    return result;
}

inline Status flush(const Device* device) {
    const Status status = validate(device);
    if (status != Status::Ok) {
        return status;
    }

    return normalize_backend_status(device->flush(device->context));
}

inline const char* status_message(Status status) {
    switch (status) {
        case Status::Ok:
            return "ok";
        case Status::InvalidArgument:
            return "invalid argument";
        case Status::InvalidGeometry:
            return "invalid device geometry";
        case Status::MissingCallback:
            return "missing device callback";
        case Status::OutOfRange:
            return "block range is outside the device";
        case Status::ArithmeticOverflow:
            return "transfer size overflow";
        case Status::BufferTooSmall:
            return "transfer buffer is too small";
        case Status::ReadOnly:
            return "device is read-only";
        case Status::Unsupported:
            return "operation is unsupported";
        case Status::IoError:
            return "I/O error";
        case Status::DeviceFault:
            return "device fault";
        case Status::BackendFailure:
            return "invalid or unspecified backend failure";
        case Status::NoDevice:
            return "no device";
        case Status::DeviceBusy:
            return "device is busy";
        case Status::TimedOut:
            return "device operation timed out";
        case Status::ControllerFault:
            return "storage controller fault";
        case Status::CommandFailed:
            return "device command failed";
        case Status::AddressNotSupported:
            return "DMA address is not supported";
    }

    return "unknown block-device status";
}

// Translate storage-specific diagnostics to the central Foundation status
// model at subsystem boundaries. Detailed Status remains available to storage
// internals and diagnostics.
inline KStatus to_kstatus(Status status) {
    switch (status) {
        case Status::Ok:
            return KStatus::Ok;
        case Status::InvalidArgument:
        case Status::InvalidGeometry:
        case Status::MissingCallback:
            return KStatus::InvalidArgument;
        case Status::OutOfRange:
        case Status::ArithmeticOverflow:
            return KStatus::OutOfRange;
        case Status::BufferTooSmall:
            return KStatus::BufferTooSmall;
        case Status::ReadOnly:
            return KStatus::ReadOnly;
        case Status::Unsupported:
        case Status::AddressNotSupported:
            return KStatus::NotSupported;
        case Status::IoError:
        case Status::BackendFailure:
        case Status::CommandFailed:
            return KStatus::IoError;
        case Status::DeviceFault:
        case Status::ControllerFault:
            return KStatus::DeviceFault;
        case Status::NoDevice:
            return KStatus::NoDevice;
        case Status::DeviceBusy:
            return KStatus::Busy;
        case Status::TimedOut:
            return KStatus::Timeout;
    }
    return KStatus::InternalError;
}

} // namespace storage::block
