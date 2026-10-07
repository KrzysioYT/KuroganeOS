#include "device_registry.hpp"

#include "ahci.hpp"
#include "nvme.hpp"
#include "../drivers/usb/xhci.hpp"

namespace storage::device_registry {

size_t device_count() {
    size_t count = ahci::device_count();
    if (nvme::block_device() != nullptr) ++count;
    if (drivers::usb::xhci::mass_storage_block_device() != nullptr) ++count;
    return count;
}

bool entry_at(size_t index, Entry* output) {
    if (output == nullptr) return false;
    *output = {};

    const size_t ahci_count = ahci::device_count();
    if (index < ahci_count) {
        const block::Device* device = ahci::device_at(index);
        const ahci::DeviceInfo* info = ahci::device_info_at(index);
        if (device == nullptr) return false;
        *output = {
            Backend::Ahci,
            index,
            device,
            info != nullptr && info->model[0] != '\0'
                ? info->model
                : "SATA/AHCI disk",
        };
        return true;
    }
    index -= ahci_count;

    const block::Device* nvme_device = nvme::block_device();
    if (nvme_device != nullptr) {
        if (index == 0U) {
            const nvme::ControllerInfo* info = nvme::controller_info();
            *output = {
                Backend::Nvme,
                0U,
                nvme_device,
                info != nullptr && info->model[0] != '\0'
                    ? info->model
                    : "NVMe namespace",
            };
            return true;
        }
        --index;
    }

    const block::Device* usb =
        drivers::usb::xhci::mass_storage_block_device();
    if (usb != nullptr && index == 0U) {
        *output = {
            Backend::UsbMassStorage,
            0U,
            usb,
            "USB Mass Storage",
        };
        return true;
    }
    return false;
}

const block::Device* device_at(size_t index) {
    Entry entry{};
    return entry_at(index, &entry) ? entry.device : nullptr;
}

const char* backend_name(Backend backend) {
    switch (backend) {
        case Backend::Ahci: return "AHCI";
        case Backend::Nvme: return "NVMe";
        case Backend::UsbMassStorage: return "USB";
    }
    return "UNKNOWN";
}

} // namespace storage::device_registry
