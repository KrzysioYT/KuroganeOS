#pragma once

#include <stdint.h>

namespace net::e1000::device {

constexpr uint16_t kIntelVendor = UINT16_C(0x8086);
constexpr uint16_t k82540Em = UINT16_C(0x100E);
constexpr uint16_t k82574L = UINT16_C(0x10D3);

enum class Model : uint8_t {
    Unsupported = 0,
    Intel82540Em,
    Intel82574L,
};

Model identify(uint16_t vendor_id, uint16_t device_id);
bool supported(uint16_t vendor_id, uint16_t device_id);
bool is_e1000e(Model model);
const char* driver_name(Model model);
const char* model_name(Model model);

} // namespace net::e1000::device
