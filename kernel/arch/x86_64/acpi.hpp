#pragma once

#include <stddef.h>
#include <stdint.h>

namespace arch::x86_64::acpi {

constexpr size_t MAXIMUM_PROCESSORS = 64U;
constexpr size_t MAXIMUM_IO_APICS = 8U;
constexpr size_t MAXIMUM_OVERRIDES = 32U;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidRsdp,
    InvalidRootTable,
    TableNotFound,
    MadtNotFound,
    InvalidMadt,
    TooManyEntries,
};

struct Processor {
    uint8_t acpi_id;
    uint8_t apic_id;
    uint32_t flags;
};

struct IoApic {
    uint8_t id;
    uint32_t address;
    uint32_t global_interrupt_base;
};

struct InterruptOverride {
    uint8_t bus;
    uint8_t source_irq;
    uint32_t global_interrupt;
    uint16_t flags;
};

struct TableView {
    const void* address;
    size_t length;
};

struct Topology {
    uint64_t local_apic_address;
    uint32_t madt_flags;
    Processor processors[MAXIMUM_PROCESSORS];
    size_t processor_count;
    IoApic io_apics[MAXIMUM_IO_APICS];
    size_t io_apic_count;
    InterruptOverride overrides[MAXIMUM_OVERRIDES];
    size_t override_count;
    bool legacy_pic_present;
};

// Finds one checksum-valid ACPI SDT through an RSDT/XSDT using the firmware's
// physical identity mapping. The four-byte signature is matched exactly.
Status find_table(
    const void* rsdp,
    const char signature[4],
    TableView* output);

// Parses an RSDP and MADT using the same validated table lookup contract.
Status parse_rsdp(const void* rsdp, Topology* output);
Status discover(uint64_t rsdp_physical_address);
bool available();
const Topology* topology();
const char* status_message(Status status);

} // namespace arch::x86_64::acpi
