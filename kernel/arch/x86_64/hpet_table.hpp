#pragma once

#include <stddef.h>
#include <stdint.h>

namespace arch::x86_64::hpet {

enum class TableStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidSignature,
    InvalidLength,
    InvalidChecksum,
    UnsupportedAddressSpace,
    InvalidAddress,
};

struct TableInfo {
    uint32_t event_timer_block_id;
    uint64_t physical_address;
    uint8_t hpet_number;
    uint16_t minimum_clock_tick;
    uint8_t page_protection;
};

TableStatus parse_acpi_table(
    const void* table,
    size_t available_length,
    TableInfo* output);

struct Capabilities {
    uint8_t revision_id;
    uint8_t timer_count;
    bool counter_64_bit;
    bool legacy_replacement;
    uint16_t vendor_id;
    uint32_t counter_period_femtoseconds;
};

// Decodes General Capabilities and ID without touching MMIO.
bool decode_capabilities(uint64_t raw, Capabilities* output);

const char* table_status_message(TableStatus status);

} // namespace arch::x86_64::hpet
