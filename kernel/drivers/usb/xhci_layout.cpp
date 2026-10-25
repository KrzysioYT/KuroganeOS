#include "xhci_layout.hpp"

namespace drivers::usb::xhci {
namespace {

constexpr size_t kMinimumCapabilityLength = 0x20U;
constexpr size_t kOperationalWindow = 0x40U;
constexpr size_t kRuntimeWindow = 0x40U;
constexpr size_t kDoorbellBytes = sizeof(uint32_t);
constexpr size_t kPortRegisterBase = 0x400U;
constexpr size_t kPortRegisterStride = 0x10U;

} // namespace

LayoutStatus validate_capability_layout(
    uint8_t capability_length,
    uint32_t doorbell_offset,
    uint32_t runtime_offset,
    uint8_t maximum_slots,
    uint8_t maximum_ports,
    size_t mmio_bytes) {
    if (mmio_bytes < kMinimumCapabilityLength ||
        mmio_bytes < kOperationalWindow) {
        return LayoutStatus::InvalidMmioWindow;
    }
    if (capability_length < kMinimumCapabilityLength ||
        static_cast<size_t>(capability_length) >
            mmio_bytes - kOperationalWindow) {
        return LayoutStatus::InvalidCapabilityLength;
    }
    if (maximum_slots == 0U) return LayoutStatus::NoSlots;
    if (maximum_ports == 0U) return LayoutStatus::NoPorts;

    const size_t doorbell_bytes =
        (static_cast<size_t>(maximum_slots) + 1U) * kDoorbellBytes;
    if (doorbell_bytes > mmio_bytes ||
        static_cast<size_t>(doorbell_offset) >
            mmio_bytes - doorbell_bytes) {
        return LayoutStatus::DoorbellWindowOutOfRange;
    }

    if (kRuntimeWindow > mmio_bytes ||
        static_cast<size_t>(runtime_offset) >
            mmio_bytes - kRuntimeWindow) {
        return LayoutStatus::RuntimeWindowOutOfRange;
    }

    const size_t port_bytes = kPortRegisterBase +
        static_cast<size_t>(maximum_ports) * kPortRegisterStride;
    // PORTSC is operational-relative, unlike DBOFF and RTSOFF. Include the
    // already-validated CAPLENGTH without overflowing the mapped BAR window.
    if (port_bytes > mmio_bytes - capability_length) {
        return LayoutStatus::PortWindowOutOfRange;
    }

    return LayoutStatus::Ok;
}

const char* layout_status_message(LayoutStatus status) {
    switch (status) {
        case LayoutStatus::Ok: return "ok";
        case LayoutStatus::InvalidMmioWindow: return "invalid MMIO window";
        case LayoutStatus::InvalidCapabilityLength: return "invalid capability length";
        case LayoutStatus::NoSlots: return "xHCI reports no slots";
        case LayoutStatus::NoPorts: return "xHCI reports no ports";
        case LayoutStatus::DoorbellWindowOutOfRange:
            return "doorbell window outside MMIO";
        case LayoutStatus::RuntimeWindowOutOfRange:
            return "runtime window outside MMIO";
        case LayoutStatus::PortWindowOutOfRange:
            return "port register window outside MMIO";
    }
    return "unknown xHCI layout status";
}

} // namespace drivers::usb::xhci
