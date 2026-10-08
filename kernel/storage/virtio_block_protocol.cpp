#include "virtio_block_protocol.hpp"

#include <limits.h>

namespace storage::virtio_block::protocol {
namespace {

bool power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

bool valid_block_size(uint32_t block_size) {
    // VirtIO requests address sectors in fixed 512-byte units even when the
    // exposed logical block size is larger. Keep the generic block ABI on
    // whole logical blocks and reject fractional/absurd geometries.
    return block_size >= kSectorBytes &&
        block_size <= 65536U &&
        power_of_two(block_size) &&
        (block_size % kSectorBytes) == 0U;
}

} // namespace

Status decode_geometry(
    uint64_t capacity_512_sectors,
    bool block_size_feature,
    uint32_t advertised_block_size,
    bool read_only,
    bool flush_supported,
    Geometry* output) {
    if (output == nullptr) return Status::InvalidArgument;
    *output = {};
    if (capacity_512_sectors == 0U) return Status::InvalidCapacity;

    const uint32_t block_size =
        block_size_feature ? advertised_block_size : kSectorBytes;
    if (!valid_block_size(block_size)) {
        return Status::UnsupportedBlockSize;
    }

    const uint64_t sectors_per_block =
        static_cast<uint64_t>(block_size / kSectorBytes);
    if (sectors_per_block == 0U ||
        (capacity_512_sectors % sectors_per_block) != 0U) {
        return Status::InvalidCapacity;
    }

    output->block_size = block_size;
    output->block_count = capacity_512_sectors / sectors_per_block;
    output->capacity_512_sectors = capacity_512_sectors;
    output->read_only = read_only;
    output->flush_supported = flush_supported;
    return output->block_count == 0U
        ? Status::InvalidCapacity
        : Status::Ok;
}

Status build_request(
    Operation operation,
    const Geometry& geometry,
    uint64_t first_block,
    uint64_t block_count,
    RequestHeader* output,
    size_t* out_transfer_bytes) {
    if (output == nullptr || out_transfer_bytes == nullptr ||
        !valid_block_size(geometry.block_size) ||
        geometry.block_count == 0U) {
        return Status::InvalidArgument;
    }
    *output = {};
    *out_transfer_bytes = 0U;

    if (operation == Operation::Flush) {
        if (block_count != 0U || first_block != 0U) {
            return Status::InvalidArgument;
        }
        output->type = static_cast<uint32_t>(Operation::Flush);
        return Status::Ok;
    }

    if (block_count == 0U) return Status::InvalidArgument;
    if (first_block >= geometry.block_count ||
        block_count > geometry.block_count - first_block) {
        return Status::OutOfRange;
    }
    if (operation == Operation::Write && geometry.read_only) {
        return Status::DeviceUnsupported;
    }

    const uint64_t sectors_per_block =
        static_cast<uint64_t>(geometry.block_size / kSectorBytes);
    if (first_block > UINT64_MAX / sectors_per_block) {
        return Status::ArithmeticOverflow;
    }
    const uint64_t sector = first_block * sectors_per_block;

    if (block_count > static_cast<uint64_t>(SIZE_MAX) /
            static_cast<uint64_t>(geometry.block_size)) {
        return Status::ArithmeticOverflow;
    }
    const size_t transfer = static_cast<size_t>(
        block_count * static_cast<uint64_t>(geometry.block_size));

    output->type = static_cast<uint32_t>(operation);
    output->reserved = 0U;
    output->sector = sector;
    *out_transfer_bytes = transfer;
    return Status::Ok;
}

Status decode_device_status(uint8_t raw_status) {
    switch (raw_status) {
        case 0U: return Status::Ok;
        case 1U: return Status::DeviceIoError;
        case 2U: return Status::DeviceUnsupported;
        default: return Status::InvalidDeviceStatus;
    }
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Ok: return "OK";
        case Status::InvalidArgument: return "INVALID_ARGUMENT";
        case Status::UnsupportedBlockSize: return "UNSUPPORTED_BLOCK_SIZE";
        case Status::InvalidCapacity: return "INVALID_CAPACITY";
        case Status::OutOfRange: return "OUT_OF_RANGE";
        case Status::ArithmeticOverflow: return "ARITHMETIC_OVERFLOW";
        case Status::DeviceIoError: return "DEVICE_IO_ERROR";
        case Status::DeviceUnsupported: return "DEVICE_UNSUPPORTED";
        case Status::InvalidDeviceStatus: return "INVALID_DEVICE_STATUS";
    }
    return "UNKNOWN";
}

} // namespace storage::virtio_block::protocol
