#include "../kernel/net/e1000.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    using net::e1000::Model;
    using net::e1000::classify_model;
    using net::e1000::supported_model;

    static_assert(classify_model(UINT16_C(0x8086), UINT16_C(0x100E)) ==
                  Model::I82540EM);
    static_assert(classify_model(UINT16_C(0x8086), UINT16_C(0x10D3)) ==
                  Model::I82574L);
    static_assert(classify_model(UINT16_C(0x10EC), UINT16_C(0x8168)) ==
                  Model::Unknown);
    static_assert(!supported_model(UINT16_C(0x8086), UINT16_C(0x1234)));
    static_assert(!supported_model(UINT16_C(0x1234), UINT16_C(0x10D3)));

    assert(supported_model(UINT16_C(0x8086), UINT16_C(0x100E)));
    assert(supported_model(UINT16_C(0x8086), UINT16_C(0x10D3)));

    std::cout << "Intel GbE model classification tests passed\n";
    return 0;
}
