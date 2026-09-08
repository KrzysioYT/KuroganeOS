#pragma once

#include "network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::e1000 {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    NotFound,
    UnsupportedDevice,
    InvalidBar,
    MmioMapFailed,
    ResetTimedOut,
    InvalidMac,
    DmaAllocationFailed,
    LinkDown,
    TransmitTimedOut,
    DeviceError,
};

Status initialize();
bool ready();
bool link_up();
NetworkInterface* interface();
const MacAddress* hardware_address();
uint64_t transmitted_frames();
uint64_t received_frames();
uint64_t dropped_frames();
bool msi_configured();
// Uses the E1000 Interrupt Cause Set register to request an actual device MSI
// and waits for the production IDT/APIC handler. Failure disables MSI and
// restores the polling-safe PCI state.
bool qualify_msi_delivery(uint32_t spin_budget);
uint64_t delivered_interrupts();
const char* status_message(Status status);

} // namespace net::e1000
