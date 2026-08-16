#include "partition_device.hpp"

#include <stddef.h>

namespace storage::partition {

namespace {

block::Status validate_request(
    const Device* partition,
    uint64_t first_block,
    uint64_t block_count,
    const void* buffer,
    uint64_t* absolute_first_block) {
    if (partition == nullptr || !partition->initialized ||
        partition->parent == nullptr || buffer == nullptr ||
        absolute_first_block == nullptr || block_count == 0U) {
        return block::Status::InvalidArgument;
    }

    const block::Status parent_status = block::validate(partition->parent);
    if (parent_status != block::Status::Ok) {
        return parent_status;
    }
    if (first_block >= partition->block_device.sector_count ||
        block_count > partition->block_device.sector_count - first_block) {
        return block::Status::OutOfRange;
    }

    // initialize() proved that the complete partition fits in the parent, so
    // this addition cannot overflow and the translated request remains in it.
    *absolute_first_block = partition->first_lba + first_block;
    return block::Status::Ok;
}

block::Status read_partition(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    void* destination) {
    auto* partition = static_cast<Device*>(context);
    uint64_t absolute_first_block = 0U;
    const block::Status status = validate_request(
        partition,
        first_block,
        block_count,
        destination,
        &absolute_first_block);
    if (status != block::Status::Ok) {
        return status;
    }

    return block::normalize_backend_status(partition->parent->read(
        partition->parent->context,
        absolute_first_block,
        block_count,
        destination));
}

block::Status write_partition(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    const void* source) {
    auto* partition = static_cast<Device*>(context);
    uint64_t absolute_first_block = 0U;
    const block::Status status = validate_request(
        partition,
        first_block,
        block_count,
        source,
        &absolute_first_block);
    if (status != block::Status::Ok) {
        return status;
    }

    return block::normalize_backend_status(partition->parent->write(
        partition->parent->context,
        absolute_first_block,
        block_count,
        source));
}

block::Status flush_partition(void* context) {
    auto* partition = static_cast<Device*>(context);
    if (partition == nullptr || !partition->initialized ||
        partition->parent == nullptr) {
        return block::Status::InvalidArgument;
    }
    return block::flush(partition->parent);
}

} // namespace

block::Status initialize(
    Device* output,
    const block::Device* parent,
    uint64_t first_lba,
    uint64_t sector_count) {
    if (output == nullptr || parent == nullptr || sector_count == 0U) {
        return block::Status::InvalidArgument;
    }

    const block::Status parent_status = block::validate(parent);
    if (parent_status != block::Status::Ok) {
        return parent_status;
    }
    if (first_lba >= parent->sector_count ||
        sector_count > parent->sector_count - first_lba) {
        return block::Status::OutOfRange;
    }

    const Device candidate = {
        {
            output,
            parent->sector_size,
            sector_count,
            read_partition,
            write_partition,
            flush_partition
        },
        parent,
        first_lba,
        true
    };
    *output = candidate;
    return block::Status::Ok;
}

block::Device* as_block_device(Device* device) {
    return device != nullptr && device->initialized
               ? &device->block_device
               : nullptr;
}

const block::Device* as_block_device(const Device* device) {
    return device != nullptr && device->initialized
               ? &device->block_device
               : nullptr;
}

} // namespace storage::partition
