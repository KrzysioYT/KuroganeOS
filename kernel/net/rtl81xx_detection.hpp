#pragma once

#include <stdint.h>

namespace net::rtl81xx {

/* PCI identification only. This module does NOT implement a Realtek NIC
 * driver: no DMA rings, RX/TX, reset, link or network interface are provided.
 * Keep unsupported hardware visible without claiming it is usable. */
enum class Model : uint8_t {
    Unknown = 0,
    Rtl8111_8168,
};

constexpr Model classify_model(uint16_t vendor_id, uint16_t device_id) {
    return vendor_id == UINT16_C(0x10EC) && device_id == UINT16_C(0x8168)
        ? Model::Rtl8111_8168 : Model::Unknown;
}

} // namespace net::rtl81xx
