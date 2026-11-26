#include "../kernel/net/rtl81xx_detection.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using net::rtl81xx::Model;
    using net::rtl81xx::classify_model;
    static_assert(classify_model(UINT16_C(0x10EC), UINT16_C(0x8168)) ==
                  Model::Rtl8111_8168, "RTL8168 PCI identification");
    static_assert(classify_model(UINT16_C(0x8086), UINT16_C(0x8168)) ==
                  Model::Unknown, "different vendor must not match");
    static_assert(classify_model(UINT16_C(0x10EC), UINT16_C(0x8169)) ==
                  Model::Unknown, "other Realtek models require distinct validation");
    assert(classify_model(UINT16_C(0x10EC), UINT16_C(0x8168)) ==
           Model::Rtl8111_8168);
    return 0;
}
