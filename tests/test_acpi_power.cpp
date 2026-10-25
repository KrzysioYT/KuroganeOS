#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "../kernel/arch/x86_64/acpi_power.hpp"

namespace {

void put32(uint8_t* bytes, uint32_t value) {
    for (size_t index = 0U; index < 4U; ++index)
        bytes[index] = static_cast<uint8_t>(value >> (index * 8U));
}
void put64(uint8_t* bytes, uint64_t value) {
    put32(bytes, static_cast<uint32_t>(value));
    put32(bytes + 4U, static_cast<uint32_t>(value >> 32U));
}
void text(uint8_t* bytes, const char* value, size_t size) {
    for (size_t index = 0U; index < size; ++index)
        bytes[index] = static_cast<uint8_t>(value[index]);
}
void checksum(uint8_t* bytes, size_t size, size_t field) {
    bytes[field] = 0U;
    uint8_t sum = 0U;
    for (size_t index = 0U; index < size; ++index)
        sum = static_cast<uint8_t>(sum + bytes[index]);
    bytes[field] = static_cast<uint8_t>(0U - sum);
}
}

int main() {
    alignas(8) uint8_t dsdt[48]{};
    text(dsdt, "DSDT", 4U);
    put32(dsdt + 4U, sizeof(dsdt));
    dsdt[8U] = 2U;
    size_t aml = 36U;
    dsdt[aml++] = 0x08U;
    text(dsdt + aml, "_S5_", 4U); aml += 4U;
    dsdt[aml++] = 0x12U;
    dsdt[aml++] = 0x06U;
    dsdt[aml++] = 0x02U;
    dsdt[aml++] = 0x0AU; dsdt[aml++] = 0x05U;
    dsdt[aml++] = 0x0AU; dsdt[aml++] = 0x06U;
    assert(aml == sizeof(dsdt));
    checksum(dsdt, sizeof(dsdt), 9U);

    alignas(8) uint8_t fadt[148]{};
    text(fadt, "FACP", 4U);
    put32(fadt + 4U, sizeof(fadt));
    fadt[8U] = 6U;
    put32(fadt + 40U, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(dsdt)));
    put32(fadt + 48U, 0xB2U);
    fadt[52U] = 0xA1U;
    put32(fadt + 64U, 0x604U);
    fadt[89U] = 2U;
    put32(fadt + 112U, UINT32_C(1) << 10U);
    fadt[116U] = 1U; fadt[117U] = 8U; fadt[119U] = 1U;
    put64(fadt + 120U, 0xCF9U);
    fadt[128U] = 0x06U;
    put64(fadt + 140U,
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(dsdt)));
    checksum(fadt, sizeof(fadt), 9U);

    alignas(8) uint8_t xsdt[44]{};
    text(xsdt, "XSDT", 4U);
    put32(xsdt + 4U, sizeof(xsdt));
    xsdt[8U] = 1U;
    put64(xsdt + 36U,
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(fadt)));
    checksum(xsdt, sizeof(xsdt), 9U);

    alignas(8) uint8_t rsdp[36]{};
    text(rsdp, "RSD PTR ", 8U);
    text(rsdp + 9U, "KUROGN", 6U);
    rsdp[15U] = 2U;
    put32(rsdp + 20U, sizeof(rsdp));
    put64(rsdp + 24U,
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(xsdt)));
    checksum(rsdp, 20U, 8U);
    checksum(rsdp, sizeof(rsdp), 32U);

    using namespace arch::x86_64::acpi_power;
    Configuration configuration{};
    assert(parse(rsdp, &configuration) == Status::Ok);
    assert(configuration.shutdown_supported);
    assert(configuration.reset_supported);
    assert(configuration.pm1a_control_port == 0x604U);
    assert(configuration.sleep_type_a == 5U);
    assert(configuration.sleep_type_b == 6U);
    assert(configuration.reset_register.address == 0xCF9U);
    assert(configuration.reset_value == 0x06U);

    const uint16_t control = compose_sleep_control(0x0001U, 5U);
    assert((control & UINT16_C(0x0001)) != 0U);
    assert((control & UINT16_C(0x1C00)) == UINT16_C(0x1400));
    assert((control & UINT16_C(0x2000)) != 0U);

    // ACPI RESET_REG is byte-wide by specification. A 16-bit declaration
    // must not be accepted merely because the I/O address itself is usable.
    fadt[117U] = 16U;
    checksum(fadt, sizeof(fadt), 9U);
    assert(parse(rsdp, &configuration) == Status::Ok);
    assert(configuration.shutdown_supported);
    assert(!configuration.reset_supported);

    // HW-reduced ACPI platforms use SLEEP_CONTROL_REG rather than PM1_CNT.
    // That mechanism is intentionally outside this bounded Steel baseline.
    fadt[117U] = 8U;
    put32(fadt + 112U, (UINT32_C(1) << 10U) | (UINT32_C(1) << 20U));
    checksum(fadt, sizeof(fadt), 9U);
    assert(parse(rsdp, &configuration) == Status::SleepStateNotFound);
    assert(!configuration.shutdown_supported);
    assert(configuration.reset_supported);

    // Restore the fixed-register platform and prove malformed DSDT is not
    // promoted into a shutdown method.
    put32(fadt + 112U, UINT32_C(1) << 10U);
    checksum(fadt, sizeof(fadt), 9U);
    dsdt[47U] ^= 1U;
    assert(parse(rsdp, &configuration) == Status::SleepStateNotFound);
    assert(!configuration.shutdown_supported);
    assert(configuration.reset_supported);

    std::cout << "ACPI power parser tests: PASS\n";
    return 0;
}
