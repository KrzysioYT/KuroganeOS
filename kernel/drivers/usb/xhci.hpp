#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../core/device_manager.hpp"
#include "../pci.hpp"

namespace drivers::usb::xhci {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    InvalidArgument,
    UnsupportedController,
    MmioUnavailable,
    BiosHandoffTimeout,
    ControllerResetTimeout,
    DmaAllocationFailed,
    ControllerStartTimeout,
    NoDevice,
    PortResetTimeout,
    CommandFailed,
    TransferFailed,
    DescriptorInvalid,
    HidKeyboardNotFound,
    DeviceRegistrationFailed,
    ControllerHaltTimeout,
    ResourceReleaseFailed,
};

Status initialize(
    const pci::Device& pci_device,
    device::DeviceId parent_device,
    device::DriverId owner_driver);
size_t poll(size_t budget);
bool initialized();
bool keyboard_ready();
// Last runtime lifecycle result; NoDevice means a running controller retains
// bounded resources while waiting for its first or a replacement keyboard.
Status runtime_status();
uint64_t reports_received();
const char* status_message(Status status);

} // namespace drivers::usb::xhci
