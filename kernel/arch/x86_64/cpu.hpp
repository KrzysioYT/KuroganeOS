#pragma once

#include <stdint.h>

namespace arch::x86_64::cpu {

enum class Vendor : uint8_t {
    Unknown = 0,
    Intel,
    Amd,
    Other,
};

struct Signature {
    uint16_t family;
    uint16_t model;
    uint8_t stepping;
};

struct Info {
    Vendor vendor;
    char vendor_id[13];
    Signature signature;
    bool tsc;
    bool msr;
    bool apic;
    bool x2apic;
    bool sse2;
    bool nx;
    bool long_mode;
};

Vendor classify_vendor(const char* vendor_id);
Signature decode_signature(uint32_t eax);
bool initialize();
bool initialized();
const Info* info();
const char* vendor_name(Vendor vendor);

} // namespace arch::x86_64::cpu
