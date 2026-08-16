#pragma once

#include <stdint.h>

#include "block_device.hpp"

namespace storage::partition {

// A bounded block-device view over a contiguous range of a parent device.
// The wrapper owns no storage and must not outlive its parent. After a
// successful initialize(), do not move or copy the wrapper because the public
// block device keeps its address as callback context.
struct Device {
    block::Device block_device;
    const block::Device* parent;
    uint64_t first_lba;
    bool initialized;
};

// Transactional: an invalid parent or range leaves output unchanged.
block::Status initialize(
    Device* output,
    const block::Device* parent,
    uint64_t first_lba,
    uint64_t sector_count);

block::Device* as_block_device(Device* device);
const block::Device* as_block_device(const Device* device);

} // namespace storage::partition
