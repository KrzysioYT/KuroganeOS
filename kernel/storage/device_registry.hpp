#pragma once

#include <stddef.h>
#include <stdint.h>

#include "block_device.hpp"

namespace storage::device_registry {

enum class Backend : uint8_t {
    Ahci = 0,
    Nvme,
    UsbMassStorage,
};

struct Entry {
    Backend backend;
    size_t backend_index;
    const block::Device* device;
    const char* model;
};

size_t device_count();
bool entry_at(size_t index, Entry* output);
const block::Device* device_at(size_t index);
const char* backend_name(Backend backend);

} // namespace storage::device_registry
