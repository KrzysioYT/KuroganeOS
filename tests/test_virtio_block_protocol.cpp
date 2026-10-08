#include <assert.h>
#include <stdint.h>

#include "../kernel/storage/virtio_block_protocol.hpp"

int main() {
    using namespace storage::virtio_block::protocol;

    Geometry geometry{};
    assert(decode_geometry(4096U, false, 0U, false, true, &geometry) ==
        Status::Ok);
    assert(geometry.block_size == 512U);
    assert(geometry.block_count == 4096U);
    assert(geometry.flush_supported);

    assert(decode_geometry(8192U, true, 4096U, false, true, &geometry) ==
        Status::Ok);
    assert(geometry.block_size == 4096U);
    assert(geometry.block_count == 1024U);

    assert(decode_geometry(8193U, true, 4096U, false, true, &geometry) ==
        Status::InvalidCapacity);
    assert(decode_geometry(8192U, true, 1000U, false, true, &geometry) ==
        Status::UnsupportedBlockSize);
    assert(decode_geometry(8192U, true, 256U, false, true, &geometry) ==
        Status::UnsupportedBlockSize);

    assert(decode_geometry(8192U, true, 4096U, false, true, &geometry) ==
        Status::Ok);
    RequestHeader request{};
    size_t bytes = 0U;
    assert(build_request(
        Operation::Read, geometry, 3U, 2U, &request, &bytes) == Status::Ok);
    assert(request.type == 0U);
    assert(request.sector == 24U);
    assert(bytes == 8192U);

    geometry.read_only = true;
    assert(build_request(
        Operation::Write, geometry, 0U, 1U, &request, &bytes) ==
        Status::DeviceUnsupported);
    assert(build_request(
        Operation::Read, geometry, 1024U, 1U, &request, &bytes) ==
        Status::OutOfRange);

    assert(build_request(
        Operation::Flush, geometry, 0U, 0U, &request, &bytes) == Status::Ok);
    assert(request.type == 4U);
    assert(request.sector == 0U);
    assert(bytes == 0U);
    assert(build_request(
        Operation::Flush, geometry, 1U, 0U, &request, &bytes) ==
        Status::InvalidArgument);

    assert(decode_device_status(0U) == Status::Ok);
    assert(decode_device_status(1U) == Status::DeviceIoError);
    assert(decode_device_status(2U) == Status::DeviceUnsupported);
    assert(decode_device_status(3U) == Status::InvalidDeviceStatus);

    return 0;
}
