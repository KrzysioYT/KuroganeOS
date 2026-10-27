#pragma once

#include "network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::e1000 {

enum class Model : uint8_t {
    Unknown = 0,
    I82540EM,
    I82574L,
};

constexpr Model classify_model(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != UINT16_C(0x8086)) return Model::Unknown;
    switch (device_id) {
        case UINT16_C(0x100E): return Model::I82540EM;
        case UINT16_C(0x10D3): return Model::I82574L;
        default: return Model::Unknown;
    }
}

constexpr bool supported_model(uint16_t vendor_id, uint16_t device_id) {
    return classify_model(vendor_id, device_id) != Model::Unknown;
}

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
Model model();
uint16_t pci_device_id();
const char* model_name(Model model);
// Uses the E1000 Interrupt Cause Set register to request an actual device MSI
// and waits for the production IDT/APIC handler. Failure disables MSI and
// restores the polling-safe PCI state.
bool qualify_msi_delivery(uint32_t spin_budget);
uint64_t delivered_interrupts();
const char* status_message(Status status);

} // namespace net::e1000
