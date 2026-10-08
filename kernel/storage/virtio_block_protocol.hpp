#pragma once

#include <stddef.h>
#include <stdint.h>

namespace storage::virtio_block::protocol {

constexpr uint32_t kSectorBytes = 512U;

enum class Operation : uint32_t {
    Read = 0U,
    Write = 1U,
    Flush = 4U,
};

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    UnsupportedBlockSize,
    InvalidCapacity,
    OutOfRange,
    ArithmeticOverflow,
    DeviceIoError,
    DeviceUnsupported,
    InvalidDeviceStatus,
};

struct [[gnu::packed]] RequestHeader {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

static_assert(sizeof(RequestHeader) == 16U, "VirtIO-blk request header ABI mismatch");

struct Geometry {
    uint32_t block_size;
    uint64_t block_count;
    uint64_t capacity_512_sectors;
    bool read_only;
    bool flush_supported;
};

Status decode_geometry(
    uint64_t capacity_512_sectors,
    bool block_size_feature,
    uint32_t advertised_block_size,
    bool read_only,
    bool flush_supported,
    Geometry* output);

Status build_request(
    Operation operation,
    const Geometry& geometry,
    uint64_t first_block,
    uint64_t block_count,
    RequestHeader* output,
    size_t* out_transfer_bytes);

Status decode_device_status(uint8_t raw_status);
const char* status_name(Status status);

} // namespace storage::virtio_block::protocol
