#include "kurogane_mbedtls_platform.h"

#include "../../libk/format.hpp"
#include "../../memory/allocator.hpp"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

namespace {

constexpr size_t kMaximumEntropyRequest = 1024U;
constexpr unsigned kHardwareRandomRetries = 32U;
constexpr uint32_t kCpuidRdrandBit = UINT32_C(1) << 30U;
constexpr uint32_t kCpuidRdseedBit = UINT32_C(1) << 18U;

struct CpuidResult {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

CpuidResult cpuid(uint32_t leaf, uint32_t subleaf) {
    CpuidResult result{};
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile(
        "cpuid"
        : "=a"(result.eax), "=b"(result.ebx),
          "=c"(result.ecx), "=d"(result.edx)
        : "a"(leaf), "c"(subleaf)
        : "memory");
#else
    static_cast<void>(leaf);
    static_cast<void>(subleaf);
#endif
    return result;
}

bool cpu_has_rdrand() {
    const CpuidResult maximum = cpuid(0U, 0U);
    if (maximum.eax < 1U) return false;
    return (cpuid(1U, 0U).ecx & kCpuidRdrandBit) != 0U;
}

bool cpu_has_rdseed() {
    const CpuidResult maximum = cpuid(0U, 0U);
    if (maximum.eax < 7U) return false;
    return (cpuid(7U, 0U).ebx & kCpuidRdseedBit) != 0U;
}

bool rdrand64(uint64_t* output) {
    if (output == nullptr) return false;
#if defined(__x86_64__) || defined(_M_X64)
    for (unsigned attempt = 0U; attempt < kHardwareRandomRetries; ++attempt) {
        uint64_t value = 0U;
        unsigned char ready = 0U;
        __asm__ volatile(
            "rdrand %0; setc %1"
            : "=r"(value), "=qm"(ready)
            :
            : "cc");
        if (ready != 0U) {
            *output = value;
            return true;
        }
        __asm__ volatile("pause");
    }
#endif
    return false;
}

bool rdseed64(uint64_t* output) {
    if (output == nullptr) return false;
#if defined(__x86_64__) || defined(_M_X64)
    for (unsigned attempt = 0U; attempt < kHardwareRandomRetries; ++attempt) {
        uint64_t value = 0U;
        unsigned char ready = 0U;
        __asm__ volatile(
            "rdseed %0; setc %1"
            : "=r"(value), "=qm"(ready)
            :
            : "cc");
        if (ready != 0U) {
            *output = value;
            return true;
        }
        __asm__ volatile("pause");
    }
#endif
    return false;
}

bool hardware_random_word(uint64_t* output) {
    if (cpu_has_rdseed() && rdseed64(output)) return true;
    return cpu_has_rdrand() && rdrand64(output);
}

} // namespace

extern "C" void* ku_tls_calloc(size_t count, size_t size) {
    if (count != 0U && size > SIZE_MAX / count) return nullptr;
    size_t bytes = count * size;
    if (bytes == 0U) bytes = 1U;
    auto* allocation = static_cast<uint8_t*>(memory::kmalloc(bytes, 16U));
    if (allocation == nullptr) return nullptr;
    for (size_t index = 0U; index < bytes; ++index) allocation[index] = 0U;
    return allocation;
}

extern "C" void ku_tls_free(void* pointer) {
    if (pointer != nullptr) memory::kfree(pointer);
}

extern "C" int ku_tls_snprintf(
    char* output,
    size_t capacity,
    const char* format,
    ...) {
    if (format == nullptr || (output == nullptr && capacity != 0U)) return -1;
    va_list arguments;
    va_start(arguments, format);
    const int result = k_vsnprintf(output, capacity, format, arguments);
    va_end(arguments);
    return result;
}

extern "C" int ku_tls_hardware_entropy_available(void) {
    return cpu_has_rdseed() || cpu_has_rdrand() ? 1 : 0;
}

extern "C" int ku_tls_hardware_entropy(
    void* context,
    unsigned char* output,
    size_t length,
    size_t* output_length) {
    static_cast<void>(context);
    if (output_length != nullptr) *output_length = 0U;
    if ((length != 0U && output == nullptr) || output_length == nullptr ||
        length > kMaximumEntropyRequest) {
        return -1;
    }
    if (length == 0U) return 0;
    if (ku_tls_hardware_entropy_available() == 0) return -1;

    size_t written = 0U;
    while (written < length) {
        uint64_t word = 0U;
        if (!hardware_random_word(&word)) {
            for (size_t index = 0U; index < written; ++index) output[index] = 0U;
            return -1;
        }
        for (unsigned byte = 0U; byte < 8U && written < length; ++byte) {
            output[written++] = static_cast<unsigned char>(
                (word >> (byte * 8U)) & UINT64_C(0xFF));
        }
    }
    *output_length = written;
    return 0;
}
