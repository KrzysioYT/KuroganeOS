#include "hpet_table.hpp"

namespace arch::x86_64::hpet {
namespace {

constexpr size_t SDT_HEADER_SIZE = 36U;
constexpr size_t HPET_TABLE_SIZE = 56U;
constexpr size_t GAS_OFFSET = 40U;

uint16_t read_u16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(bytes[1]) << 8U;
}

uint32_t read_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        static_cast<uint32_t>(bytes[1]) << 8U |
        static_cast<uint32_t>(bytes[2]) << 16U |
        static_cast<uint32_t>(bytes[3]) << 24U;
}

uint64_t read_u64(const uint8_t* bytes) {
    return static_cast<uint64_t>(read_u32(bytes)) |
        static_cast<uint64_t>(read_u32(bytes + 4U)) << 32U;
}

bool signature_equal(const uint8_t* bytes, const char* signature) {
    for (size_t index = 0U; index < 4U; ++index) {
        if (bytes[index] != static_cast<uint8_t>(signature[index])) return false;
    }
    return true;
}

bool checksum_valid(const uint8_t* bytes, size_t size) {
    uint8_t sum = 0U;
    for (size_t index = 0U; index < size; ++index) {
        sum = static_cast<uint8_t>(sum + bytes[index]);
    }
    return sum == 0U;
}

} // namespace

TableStatus parse_acpi_table(
    const void* table,
    size_t available_length,
    TableInfo* output) {
    if (table == nullptr || output == nullptr) {
        return TableStatus::InvalidArgument;
    }
    if (available_length < SDT_HEADER_SIZE) {
        return TableStatus::InvalidLength;
    }

    const auto* bytes = static_cast<const uint8_t*>(table);
    if (!signature_equal(bytes, "HPET")) {
        return TableStatus::InvalidSignature;
    }

    const uint32_t length = read_u32(bytes + 4U);
    if (length < HPET_TABLE_SIZE || length > available_length) {
        return TableStatus::InvalidLength;
    }
    if (!checksum_valid(bytes, length)) {
        return TableStatus::InvalidChecksum;
    }

    // ACPI Generic Address Structure. Steel initially accepts only System
    // Memory GAS because the production runtime will use bounded MMIO mapping.
    const uint8_t address_space_id = bytes[GAS_OFFSET];
    if (address_space_id != 0U) {
        return TableStatus::UnsupportedAddressSpace;
    }
    const uint64_t address = read_u64(bytes + GAS_OFFSET + 4U);
    if (address == 0U) {
        return TableStatus::InvalidAddress;
    }

    const TableInfo staged{
        read_u32(bytes + 36U),
        address,
        bytes[52U],
        read_u16(bytes + 53U),
        bytes[55U],
    };
    *output = staged;
    return TableStatus::Ok;
}

const char* table_status_message(TableStatus status) {
    switch (status) {
        case TableStatus::Ok: return "ok";
        case TableStatus::InvalidArgument: return "invalid HPET table argument";
        case TableStatus::InvalidSignature: return "ACPI HPET signature missing";
        case TableStatus::InvalidLength: return "invalid ACPI HPET table length";
        case TableStatus::InvalidChecksum: return "invalid ACPI HPET checksum";
        case TableStatus::UnsupportedAddressSpace:
            return "unsupported ACPI HPET address space";
        case TableStatus::InvalidAddress: return "invalid ACPI HPET address";
    }
    return "unknown HPET table status";
}

} // namespace arch::x86_64::hpet
