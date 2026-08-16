#include <cassert>
#include <cstdint>
#include <iostream>

#include "../kernel/arch/x86_64/acpi.hpp"

namespace {

void put16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
}

void put32(uint8_t* bytes, uint32_t value) {
    for (size_t index = 0U; index < 4U; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8U));
    }
}

void put64(uint8_t* bytes, uint64_t value) {
    put32(bytes, static_cast<uint32_t>(value));
    put32(bytes + 4U, static_cast<uint32_t>(value >> 32U));
}

void text(uint8_t* bytes, const char* value, size_t size) {
    for (size_t index = 0U; index < size; ++index) {
        bytes[index] = static_cast<uint8_t>(value[index]);
    }
}

void checksum(uint8_t* bytes, size_t size, size_t field) {
    bytes[field] = 0U;
    uint8_t sum = 0U;
    for (size_t index = 0U; index < size; ++index) {
        sum = static_cast<uint8_t>(sum + bytes[index]);
    }
    bytes[field] = static_cast<uint8_t>(0U - sum);
}

} // namespace

int main() {
    alignas(8) uint8_t madt[86]{};
    text(madt, "APIC", 4U);
    put32(madt + 4U, sizeof(madt));
    madt[8] = 5U;
    put32(madt + 36U, 0xFEE00000U);
    put32(madt + 40U, 1U);
    size_t offset = 44U;
    madt[offset] = 0U;
    madt[offset + 1U] = 8U;
    madt[offset + 2U] = 7U;
    madt[offset + 3U] = 2U;
    put32(madt + offset + 4U, 1U);
    offset += 8U;
    madt[offset] = 1U;
    madt[offset + 1U] = 12U;
    madt[offset + 2U] = 3U;
    put32(madt + offset + 4U, 0xFEC00000U);
    put32(madt + offset + 8U, 0U);
    offset += 12U;
    madt[offset] = 2U;
    madt[offset + 1U] = 10U;
    madt[offset + 2U] = 0U;
    madt[offset + 3U] = 0U;
    put32(madt + offset + 4U, 2U);
    put16(madt + offset + 8U, 0x000DU);
    offset += 10U;
    madt[offset] = 5U;
    madt[offset + 1U] = 12U;
    put64(madt + offset + 4U, 0xFEE01000U);
    checksum(madt, sizeof(madt), 9U);

    alignas(8) uint8_t xsdt[44]{};
    text(xsdt, "XSDT", 4U);
    put32(xsdt + 4U, sizeof(xsdt));
    xsdt[8] = 1U;
    put64(xsdt + 36U, reinterpret_cast<uintptr_t>(madt));
    checksum(xsdt, sizeof(xsdt), 9U);

    alignas(8) uint8_t rsdp[36]{};
    text(rsdp, "RSD PTR ", 8U);
    text(rsdp + 9U, "KUROGN", 6U);
    rsdp[15U] = 2U;
    put32(rsdp + 20U, sizeof(rsdp));
    put64(rsdp + 24U, reinterpret_cast<uintptr_t>(xsdt));
    checksum(rsdp, 20U, 8U);
    checksum(rsdp, sizeof(rsdp), 32U);

    arch::x86_64::acpi::Topology topology{};
    using arch::x86_64::acpi::Status;
    assert(arch::x86_64::acpi::parse_rsdp(rsdp, &topology) == Status::Ok);
    assert(topology.local_apic_address == 0xFEE01000U);
    assert(topology.legacy_pic_present);
    assert(topology.processor_count == 1U);
    assert(topology.processors[0].acpi_id == 7U);
    assert(topology.processors[0].apic_id == 2U);
    assert(topology.io_apic_count == 1U);
    assert(topology.io_apics[0].address == 0xFEC00000U);
    assert(topology.override_count == 1U);
    assert(topology.overrides[0].global_interrupt == 2U);

    rsdp[8U] ^= 1U;
    assert(arch::x86_64::acpi::parse_rsdp(rsdp, &topology) ==
           Status::InvalidRsdp);
    std::cout << "ACPI/MADT parser tests: PASS\n";
    return 0;
}
