#include "device_registry.hpp"

namespace storage::device_registry {
namespace {

Entry g_entries[MAXIMUM_BLOCK_DEVICES]{};
size_t g_count = 0U;

bool valid_model(const char* model) {
    return model != nullptr && model[0] != '\0';
}

} // namespace

bool register_device(
    Backend backend,
    size_t backend_index,
    const block::Device* device,
    const char* model) {
    if (device == nullptr || block::validate(device) != block::Status::Ok ||
        !valid_model(model)) {
        return false;
    }
    for (size_t index = 0U; index < g_count; ++index) {
        if (g_entries[index].device != device) continue;
        g_entries[index] = {backend, backend_index, device, model};
        return true;
    }
    if (g_count >= MAXIMUM_BLOCK_DEVICES) return false;
    g_entries[g_count++] = {backend, backend_index, device, model};
    return true;
}

bool unregister_device(const block::Device* device) {
    if (device == nullptr) return false;
    for (size_t index = 0U; index < g_count; ++index) {
        if (g_entries[index].device != device) continue;
        for (size_t move = index + 1U; move < g_count; ++move) {
            g_entries[move - 1U] = g_entries[move];
        }
        --g_count;
        if (g_count < MAXIMUM_BLOCK_DEVICES) g_entries[g_count] = {};
        return true;
    }
    return false;
}

void reset() {
    for (size_t index = 0U; index < g_count; ++index) g_entries[index] = {};
    g_count = 0U;
}

size_t device_count() { return g_count; }

bool entry_at(size_t index, Entry* output) {
    if (output == nullptr || index >= g_count) return false;
    *output = g_entries[index];
    return output->device != nullptr;
}

const block::Device* device_at(size_t index) {
    return index < g_count ? g_entries[index].device : nullptr;
}

const char* backend_name(Backend backend) {
    switch (backend) {
        case Backend::Ahci: return "AHCI";
        case Backend::Nvme: return "NVMe";
        case Backend::UsbMassStorage: return "USB";
        case Backend::VirtioBlock: return "VirtIO";
        case Backend::Other: return "OTHER";
    }
    return "UNKNOWN";
}

} // namespace storage::device_registry
