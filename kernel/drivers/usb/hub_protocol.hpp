#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::usb::hub {

// Bounded USB 2.0 hub descriptor (type 0x29) parser. This is protocol
// groundwork, not xHCI topology, TT scheduling or downstream enumeration.
// xHCI route-string port numbers are four bits, so intentionally reject
// hubs with more than 15 ports instead of silently truncating their route.
constexpr uint8_t DESCRIPTOR_TYPE_USB2_HUB = 0x29U;
constexpr uint8_t MAX_ROUTE_PORTS = 15U;
constexpr size_t MAX_BITMAP_BYTES = 2U;

enum class PowerMode : uint8_t {
    Ganged = 0U,
    Individual = 1U,
    AlwaysOn = 2U,
};
enum class OverCurrentMode : uint8_t {
    Global = 0U,
    PerPort = 1U,
    NotReported = 2U,
};
enum class ParseStatus : uint8_t {
    Ok = 0U,
    InvalidArgument,
    Truncated,
    InvalidDescriptor,
    UnsupportedPortCount,
    ReservedMode,
};

struct Descriptor {
    uint8_t port_count;
    PowerMode power_mode;
    OverCurrentMode overcurrent_mode;
    uint8_t power_on_delay_2ms;
    uint8_t controller_current_ma;
    uint8_t bitmap_bytes;
    uint8_t removable_bitmap[MAX_BITMAP_BYTES];
    uint8_t port_power_mask[MAX_BITMAP_BYTES];
};

constexpr size_t descriptor_bitmap_bytes(uint8_t ports) {
    // Include the hub's reserved bitmap bit zero; no zero-port hub is valid.
    return (static_cast<size_t>(ports) + 8U) / 8U;
}

inline ParseStatus parse_usb2_descriptor(
    const uint8_t* source, size_t size, Descriptor* output) {
    if (source == nullptr || output == nullptr)
        return ParseStatus::InvalidArgument;
    if (size < 7U) return ParseStatus::Truncated;
    if (source[1U] != DESCRIPTOR_TYPE_USB2_HUB ||
        source[0U] < 9U)
        return ParseStatus::InvalidDescriptor;
    const uint8_t ports = source[2U];
    if (ports == 0U || ports > MAX_ROUTE_PORTS)
        return ParseStatus::UnsupportedPortCount;
    const size_t bytes = descriptor_bitmap_bytes(ports);
    const size_t required_length = 7U + 2U * bytes;
    if (source[0U] != required_length)
        return ParseStatus::InvalidDescriptor;
    if (size < required_length) return ParseStatus::Truncated;

    const uint16_t characteristics =
        static_cast<uint16_t>(source[3U]) |
        (static_cast<uint16_t>(source[4U]) << 8U);
    const uint8_t power = static_cast<uint8_t>(characteristics & 3U);
    const uint8_t overcurrent =
        static_cast<uint8_t>((characteristics >> 3U) & 3U);
    if (power == 3U || overcurrent == 3U)
        return ParseStatus::ReservedMode;

    Descriptor parsed{};
    parsed.port_count = ports;
    parsed.power_mode = static_cast<PowerMode>(power);
    parsed.overcurrent_mode = static_cast<OverCurrentMode>(overcurrent);
    parsed.power_on_delay_2ms = source[5U];
    parsed.controller_current_ma = source[6U];
    parsed.bitmap_bytes = static_cast<uint8_t>(bytes);
    for (size_t i = 0U; i < bytes; ++i) {
        parsed.removable_bitmap[i] = source[7U + i];
        parsed.port_power_mask[i] = source[7U + bytes + i];
    }
    // Commit only on success: malformed/untrusted hub input must never
    // partly overwrite a previously valid topology record.
    *output = parsed;
    return ParseStatus::Ok;
}

inline bool port_is_removable(
    const Descriptor& descriptor, uint8_t port) {
    if (port == 0U || port > descriptor.port_count ||
        descriptor.bitmap_bytes == 0U ||
        descriptor.bitmap_bytes > MAX_BITMAP_BYTES)
        return false;
    const size_t byte_index = static_cast<size_t>(port) / 8U;
    if (byte_index >= descriptor.bitmap_bytes) return false;
    const uint8_t mask = static_cast<uint8_t>(1U << (port & 7U));
    return (descriptor.removable_bitmap[byte_index] & mask) == 0U;
}

} // namespace drivers::usb::hub
