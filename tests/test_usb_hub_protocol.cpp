#include "../kernel/drivers/usb/hub_protocol.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

int main() {
    namespace hub = drivers::usb::hub;
    static_assert(hub::descriptor_bitmap_bytes(1U) == 1U);
    static_assert(hub::descriptor_bitmap_bytes(7U) == 1U);
    static_assert(hub::descriptor_bitmap_bytes(8U) == 2U);
    static_assert(hub::descriptor_bitmap_bytes(15U) == 2U);

    // USB 2.0 four-port hub, individual power, per-port overcurrent:
    // Port 4 is non-removable, other ports may disconnect.
    const uint8_t four_port[] = {
        9U, 0x29U, 4U, 0x09U, 0x00U, 50U, 90U, 0x10U, 0xFFU
    };
    hub::Descriptor result{};
    assert(hub::parse_usb2_descriptor(
        four_port, sizeof(four_port), &result) == hub::ParseStatus::Ok);
    assert(result.port_count == 4U);
    assert(result.bitmap_bytes == 1U);
    assert(result.power_mode == hub::PowerMode::Individual);
    assert(result.overcurrent_mode == hub::OverCurrentMode::PerPort);
    assert(result.power_on_delay_2ms == 50U);
    assert(result.controller_current_ma == 90U);
    assert(hub::port_is_removable(result, 1U));
    assert(!hub::port_is_removable(result, 4U));
    assert(!hub::port_is_removable(result, 0U));
    assert(!hub::port_is_removable(result, 5U));

    // Include both reserved bitmap bit zero and bit 15: index must not
    // wrap when passing from the first to the second bitmap byte.
    const uint8_t fifteen_port[] = {
        11U, 0x29U, 15U, 0x12U, 0U, 1U, 0U,
        0x00U, 0x80U, 0xFFU, 0xFFU,
    };
    assert(hub::parse_usb2_descriptor(
        fifteen_port, sizeof(fifteen_port), &result) == hub::ParseStatus::Ok);
    assert(result.port_count == 15U && result.bitmap_bytes == 2U);
    assert(result.power_mode == hub::PowerMode::AlwaysOn);
    assert(result.overcurrent_mode == hub::OverCurrentMode::NotReported);
    assert(hub::port_is_removable(result, 8U));
    assert(hub::port_is_removable(result, 14U));
    assert(!hub::port_is_removable(result, 15U));
    assert(!hub::port_is_removable(result, 16U));

    // A successful record is unchanged by any malformed descriptor.
    const auto preserved = result;
    assert(hub::parse_usb2_descriptor(
        nullptr, sizeof(four_port), &result) ==
        hub::ParseStatus::InvalidArgument);
    assert(hub::parse_usb2_descriptor(
        four_port, sizeof(four_port), nullptr) ==
        hub::ParseStatus::InvalidArgument);
    assert(hub::parse_usb2_descriptor(
        four_port, 6U, &result) == hub::ParseStatus::Truncated);
    assert(hub::parse_usb2_descriptor(
        four_port, 8U, &result) == hub::ParseStatus::Truncated);

    uint8_t malformed[sizeof(four_port)]{};
    for (size_t i = 0U; i < sizeof(malformed); ++i)
        malformed[i] = four_port[i];
    malformed[1U] = 0x2AU; // SuperSpeed uses a different descriptor layout.
    assert(hub::parse_usb2_descriptor(
        malformed, sizeof(malformed), &result) ==
        hub::ParseStatus::InvalidDescriptor);
    malformed[1U] = 0x29U;
    malformed[0U] = 8U;
    assert(hub::parse_usb2_descriptor(
        malformed, sizeof(malformed), &result) ==
        hub::ParseStatus::InvalidDescriptor);
    malformed[0U] = 9U;
    malformed[2U] = 0U;
    assert(hub::parse_usb2_descriptor(
        malformed, sizeof(malformed), &result) ==
        hub::ParseStatus::UnsupportedPortCount);
    malformed[2U] = 16U;
    assert(hub::parse_usb2_descriptor(
        malformed, sizeof(malformed), &result) ==
        hub::ParseStatus::UnsupportedPortCount);
    malformed[2U] = 4U;
    malformed[3U] = 0x03U; // Reserved power switching mode.
    assert(hub::parse_usb2_descriptor(
        malformed, sizeof(malformed), &result) ==
        hub::ParseStatus::ReservedMode);
    malformed[3U] = 0x18U; // Reserved overcurrent mode.
    assert(hub::parse_usb2_descriptor(
        malformed, sizeof(malformed), &result) ==
        hub::ParseStatus::ReservedMode);

    assert(result.port_count == preserved.port_count);
    assert(result.bitmap_bytes == preserved.bitmap_bytes);
    assert(result.power_mode == preserved.power_mode);
    assert(result.overcurrent_mode == preserved.overcurrent_mode);
    assert(result.removable_bitmap[0U] == preserved.removable_bitmap[0U]);
    assert(result.removable_bitmap[1U] == preserved.removable_bitmap[1U]);
    return 0;
}
