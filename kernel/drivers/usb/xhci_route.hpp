#pragma once

#include <stdint.h>

namespace drivers::usb::xhci::route {

// xHCI slot context routes downstream devices via five four-bit port tiers;
// the root-hub port is a separate eight-bit field (Slot Context DW1[23:16]).
// Building these words does not enumerate or power a USB hub.
constexpr uint8_t MAX_TIERS = 5U;
constexpr uint32_t ROUTE_MASK = UINT32_C(0x000FFFFF);

struct Address {
    uint8_t root_port;
    uint8_t tiers;
    uint32_t route_string;
};

inline bool valid(const Address& address) {
    if (address.root_port == 0U || address.tiers > MAX_TIERS ||
        (address.route_string & ~ROUTE_MASK) != 0U) {
        return false;
    }
    for (uint8_t i = 0U; i < MAX_TIERS; ++i) {
        const uint8_t port = static_cast<uint8_t>(
            (address.route_string >> (4U * i)) & UINT32_C(0xF));
        if ((i < address.tiers && port == 0U) ||
            (i >= address.tiers && port != 0U))
            return false;
    }
    return true;
}

inline bool extend(
    const Address& parent, uint8_t downstream_port, Address* output) {
    if (output == nullptr || !valid(parent) ||
        parent.tiers >= MAX_TIERS ||
        downstream_port == 0U || downstream_port > 15U)
        return false;
    Address child = parent;
    child.route_string |= static_cast<uint32_t>(downstream_port) <<
        (4U * child.tiers);
    ++child.tiers;
    *output = child;
    return true;
}

inline bool slot_context_words(
    const Address& address, uint8_t speed, uint8_t context_entries,
    uint32_t* dw0, uint32_t* dw1) {
    if (dw0 == nullptr || dw1 == nullptr || dw0 == dw1 ||
        !valid(address) || speed == 0U || speed > 15U ||
        context_entries == 0U || context_entries > 31U)
        return false;
    const uint32_t first = address.route_string |
        (static_cast<uint32_t>(speed) << 20U) |
        (static_cast<uint32_t>(context_entries) << 27U);
    const uint32_t second = static_cast<uint32_t>(address.root_port) << 16U;
    *dw0 = first;
    *dw1 = second;
    return true;
}
} // namespace drivers::usb::xhci::route
