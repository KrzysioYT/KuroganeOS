#pragma once

#include <stddef.h>
#include <stdint.h>

#include "block_device.hpp"

namespace storage::ahci {

constexpr size_t MAXIMUM_CONTROLLERS = 4U;
constexpr size_t MAXIMUM_DEVICES = 32U;

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    NoController,
    ControllerLimitReached,
    InvalidPciBar,
    PciCommandRejected,
    MmioWindowUnavailable,
    MmioMappingFailed,
    BiosHandoffTimeout,
    ControllerResetTimeout,
    NoSataDevice,
    DeviceLimitReached,
    DmaAllocationFailed,
    DmaAddressNotSupported,
    PortStopTimeout,
    PortStartTimeout,
    DeviceBusyTimeout,
    CommandSlotBusy,
    CommandTimeout,
    TaskFileError,
    InterfaceError,
    ShortTransfer,
    InvalidIdentifyData,
    Lba48Unsupported,
    UnsupportedSectorSize,
    InvalidArgument,
    OutOfRange,
    PortOffline
};

struct DeviceInfo {
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_function;
    uint8_t port;
    uint32_t sector_size;
    uint64_t sector_count;
    bool controller_supports_64_bit_dma;
    char model[41];
    Status last_status;
    uint32_t last_interrupt_status;
    uint32_t last_task_file_data;
    uint32_t last_sata_error;
};

// Discovers exact PCI class/subclass/prog-if 01:06:01 devices previously
// recorded by pci::scan(). Initialization is polling-only and leaves global
// and per-port interrupts disabled. It issues IDENTIFY DEVICE but performs no
// media writes or write self-tests.
Status initialize();

bool initialized();
bool initialization_attempted();
Status initialization_status();
size_t detected_controller_count();
size_t active_controller_count();
size_t failed_controller_count();
size_t device_count();
const block::Device* device_at(size_t index);
const DeviceInfo* device_info_at(size_t index);

const char* status_message(Status status);

} // namespace storage::ahci
