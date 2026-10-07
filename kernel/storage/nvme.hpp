#pragma once

#include <stdint.h>

#include "block_device.hpp"

namespace storage::nvme {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NoController,
    UnsupportedController,
    PciCommandRejected,
    BarUnavailable,
    MmioMappingFailed,
    DmaAllocationFailed,
    ControllerResetTimeout,
    ControllerStartTimeout,
    AdminCommandTimeout,
    AdminCommandFailed,
    IdentifyInvalid,
    NoNamespace,
    IoQueueCreationFailed,
    IoCommandFailed,
    ResourceReleaseFailed,
};

struct ControllerInfo {
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t version;
    uint16_t admin_queue_entries;
    uint16_t io_queue_entries;
    uint32_t namespace_id;
    uint32_t block_size;
    uint64_t block_count;
    char serial[21];
    char model[41];
};

Status initialize();
void shutdown();

bool initialized();
const ControllerInfo* controller_info();
const block::Device* block_device();
const char* status_message(Status status);

} // namespace storage::nvme
