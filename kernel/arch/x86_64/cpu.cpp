#include "cpu.hpp"

namespace arch::x86_64::cpu {
namespace {

Info g_info{};
bool g_initialized = false;

void cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t* eax,
    uint32_t* ebx,
    uint32_t* ecx,
    uint32_t* edx) {
    uint32_t a = 0U;
    uint32_t b = 0U;
    uint32_t c = 0U;
    uint32_t d = 0U;
    __asm__ volatile(
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf)
        : "memory");
    if (eax != nullptr) *eax = a;
    if (ebx != nullptr) *ebx = b;
    if (ecx != nullptr) *ecx = c;
    if (edx != nullptr) *edx = d;
}

bool text_equal_12(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    for (size_t index = 0U; index < 12U; ++index) {
        if (left[index] != right[index]) return false;
    }
    return true;
}

void write_u32_chars(char* destination, uint32_t value) {
    destination[0] = static_cast<char>(value & 0xFFU);
    destination[1] = static_cast<char>((value >> 8U) & 0xFFU);
    destination[2] = static_cast<char>((value >> 16U) & 0xFFU);
    destination[3] = static_cast<char>((value >> 24U) & 0xFFU);
}

} // namespace

Vendor classify_vendor(const char* vendor_id) {
    if (vendor_id == nullptr) return Vendor::Unknown;
    if (text_equal_12(vendor_id, "GenuineIntel")) return Vendor::Intel;
    if (text_equal_12(vendor_id, "AuthenticAMD")) return Vendor::Amd;
    return Vendor::Other;
}

Signature decode_signature(uint32_t eax) {
    const uint16_t base_family =
        static_cast<uint16_t>((eax >> 8U) & 0x0FU);
    const uint16_t base_model =
        static_cast<uint16_t>((eax >> 4U) & 0x0FU);
    const uint16_t extended_family =
        static_cast<uint16_t>((eax >> 20U) & 0xFFU);
    const uint16_t extended_model =
        static_cast<uint16_t>((eax >> 16U) & 0x0FU);

    uint16_t family = base_family;
    if (base_family == 0x0FU) family =
        static_cast<uint16_t>(family + extended_family);

    uint16_t model = base_model;
    if (base_family == 0x06U || base_family == 0x0FU) {
        model = static_cast<uint16_t>(
            model | static_cast<uint16_t>(extended_model << 4U));
    }
    return {
        family,
        model,
        static_cast<uint8_t>(eax & 0x0FU),
    };
}

bool initialize() {
    uint32_t max_basic = 0U;
    uint32_t ebx = 0U;
    uint32_t ecx = 0U;
    uint32_t edx = 0U;
    cpuid(0U, 0U, &max_basic, &ebx, &ecx, &edx);
    if (max_basic == 0U) return false;

    g_info = {};
    write_u32_chars(g_info.vendor_id + 0U, ebx);
    write_u32_chars(g_info.vendor_id + 4U, edx);
    write_u32_chars(g_info.vendor_id + 8U, ecx);
    g_info.vendor_id[12U] = '\0';
    g_info.vendor = classify_vendor(g_info.vendor_id);

    uint32_t eax1 = 0U;
    uint32_t ecx1 = 0U;
    uint32_t edx1 = 0U;
    cpuid(1U, 0U, &eax1, nullptr, &ecx1, &edx1);
    g_info.signature = decode_signature(eax1);
    g_info.tsc = (edx1 & (UINT32_C(1) << 4U)) != 0U;
    g_info.msr = (edx1 & (UINT32_C(1) << 5U)) != 0U;
    g_info.apic = (edx1 & (UINT32_C(1) << 9U)) != 0U;
    g_info.sse2 = (edx1 & (UINT32_C(1) << 26U)) != 0U;
    g_info.x2apic = (ecx1 & (UINT32_C(1) << 21U)) != 0U;

    uint32_t max_extended = 0U;
    cpuid(UINT32_C(0x80000000), 0U, &max_extended, nullptr, nullptr, nullptr);
    if (max_extended >= UINT32_C(0x80000001)) {
        uint32_t extended_edx = 0U;
        cpuid(
            UINT32_C(0x80000001), 0U,
            nullptr, nullptr, nullptr, &extended_edx);
        g_info.nx = (extended_edx & (UINT32_C(1) << 20U)) != 0U;
        g_info.long_mode =
            (extended_edx & (UINT32_C(1) << 29U)) != 0U;
    }
    g_initialized = true;
    return true;
}

bool initialized() { return g_initialized; }
const Info* info() { return g_initialized ? &g_info : nullptr; }

const char* vendor_name(Vendor vendor) {
    switch (vendor) {
        case Vendor::Intel: return "Intel";
        case Vendor::Amd: return "AMD";
        case Vendor::Other: return "Other";
        case Vendor::Unknown: return "Unknown";
    }
    return "Unknown";
}

} // namespace arch::x86_64::cpu
