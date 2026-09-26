#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../kernel/arch/x86_64/hpet_table.hpp"

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

void checksum(uint8_t* bytes, size_t size) {
    bytes[9U] = 0U;
    uint8_t sum = 0U;
    for (size_t index = 0U; index < size; ++index) {
        sum = static_cast<uint8_t>(sum + bytes[index]);
    }
    bytes[9U] = static_cast<uint8_t>(0U - sum);
}

void make_valid(uint8_t* table, size_t size) {
    std::memset(table, 0, size);
    table[0] = 'H';
    table[1] = 'P';
    table[2] = 'E';
    table[3] = 'T';
    put32(table + 4U, static_cast<uint32_t>(size));
    table[8U] = 1U;
    put32(table + 36U, UINT32_C(0x8086A201));
    table[40U] = 0U;
    table[41U] = 64U;
    table[42U] = 0U;
    table[43U] = 0U;
    put64(table + 44U, UINT64_C(0xFED00000));
    table[52U] = 0U;
    put16(table + 53U, 128U);
    table[55U] = 0U;
    checksum(table, size);
}

} // namespace

int main() {
    using arch::x86_64::hpet::TableInfo;
    using arch::x86_64::hpet::TableStatus;

    uint8_t table[56]{};
    make_valid(table, sizeof(table));

    TableInfo info{};
    assert(arch::x86_64::hpet::parse_acpi_table(
        table, sizeof(table), &info) == TableStatus::Ok);
    assert(info.event_timer_block_id == UINT32_C(0x8086A201));
    assert(info.physical_address == UINT64_C(0xFED00000));
    assert(info.hpet_number == 0U);
    assert(info.minimum_clock_tick == 128U);
    assert(info.page_protection == 0U);

    const TableInfo sentinel{
        UINT32_C(0x11111111), UINT64_C(0x22222222), 3U, 4U, 5U};

    info = sentinel;
    table[0] = 'X';
    assert(arch::x86_64::hpet::parse_acpi_table(
        table, sizeof(table), &info) == TableStatus::InvalidSignature);
    assert(info.physical_address == sentinel.physical_address);

    make_valid(table, sizeof(table));
    put32(table + 4U, 55U);
    checksum(table, sizeof(table));
    info = sentinel;
    assert(arch::x86_64::hpet::parse_acpi_table(
        table, sizeof(table), &info) == TableStatus::InvalidLength);
    assert(info.physical_address == sentinel.physical_address);

    make_valid(table, sizeof(table));
    table[20U] ^= 1U;
    info = sentinel;
    assert(arch::x86_64::hpet::parse_acpi_table(
        table, sizeof(table), &info) == TableStatus::InvalidChecksum);
    assert(info.physical_address == sentinel.physical_address);

    make_valid(table, sizeof(table));
    table[40U] = 1U;
    checksum(table, sizeof(table));
    info = sentinel;
    assert(arch::x86_64::hpet::parse_acpi_table(
        table, sizeof(table), &info) ==
        TableStatus::UnsupportedAddressSpace);
    assert(info.physical_address == sentinel.physical_address);

    make_valid(table, sizeof(table));
    put64(table + 44U, 0U);
    checksum(table, sizeof(table));
    info = sentinel;
    assert(arch::x86_64::hpet::parse_acpi_table(
        table, sizeof(table), &info) == TableStatus::InvalidAddress);
    assert(info.physical_address == sentinel.physical_address);

    std::puts("ACPI HPET table parser: PASS");
    return 0;
}
