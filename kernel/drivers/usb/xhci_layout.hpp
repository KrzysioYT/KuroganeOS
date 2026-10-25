#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::usb::xhci {

enum class LayoutStatus : uint8_t {
    Ok = 0,
    InvalidMmioWindow,
    InvalidCapabilityLength,
    NoSlots,
    NoPorts,
    DoorbellWindowOutOfRange,
    RuntimeWindowOutOfRange,
    PortWindowOutOfRange,
};

LayoutStatus validate_capability_layout(
    uint8_t capability_length,
    uint32_t doorbell_offset,
    uint32_t runtime_offset,
    uint8_t maximum_slots,
    uint8_t maximum_ports,
    size_t mmio_bytes);

const char* layout_status_message(LayoutStatus status);

} // namespace drivers::usb::xhci
