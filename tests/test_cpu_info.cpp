#include "../kernel/arch/x86_64/cpu.hpp"

#include <cassert>
#include <iostream>

int main() {
    using namespace arch::x86_64::cpu;

    assert(classify_vendor("GenuineIntel") == Vendor::Intel);
    assert(classify_vendor("AuthenticAMD") == Vendor::Amd);
    assert(classify_vendor("KuroganeTest") == Vendor::Other);
    assert(classify_vendor(nullptr) == Vendor::Unknown);

    const Signature family6 = decode_signature(
        (UINT32_C(6) << 8U) | (UINT32_C(0xA) << 4U) |
        (UINT32_C(5) << 16U) | 3U);
    assert(family6.family == 6U);
    assert(family6.model == 0x5AU);
    assert(family6.stepping == 3U);

    const Signature family15 = decode_signature(
        (UINT32_C(0xF) << 8U) | (UINT32_C(2) << 20U) |
        (UINT32_C(4) << 4U) | (UINT32_C(1) << 16U) | 7U);
    assert(family15.family == 17U);
    assert(family15.model == 0x14U);
    assert(family15.stepping == 7U);

    std::cout << "x86 CPU vendor/signature decode: PASS\n";
    return 0;
}
