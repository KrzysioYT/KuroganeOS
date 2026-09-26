#pragma once

#include <stdint.h>

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
    char serial[21];
    char model[41];
};

Status initialize();
void shutdown();

bool initialized();
const ControllerInfo* controller_info();
const char* status_message(Status status);

} // namespace storage::nvme
