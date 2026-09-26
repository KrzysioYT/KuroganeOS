#include "hpet.hpp"

#include "acpi.hpp"
#include "../../memory/kernel_virtual_memory.hpp"
#include "../../memory/virtual_memory.hpp"

namespace arch::x86_64::hpet {
namespace {

constexpr uint64_t VIRTUAL_BASE = UINT64_C(0xFFFFB40000000000);
constexpr size_t REGISTER_SPAN = 0x100U;
constexpr size_t GENERAL_CAPABILITIES = 0x00U;
constexpr size_t GENERAL_CONFIGURATION = 0x10U;
constexpr size_t MAIN_COUNTER = 0xF0U;
constexpr uint64_t CONFIG_ENABLE = UINT64_C(1);
constexpr uint64_t CONFIG_LEGACY_REPLACEMENT = UINT64_C(1) << 1U;

volatile uint8_t* g_registers = nullptr;
uint64_t g_mapped_virtual_base = 0U;
uint64_t g_original_configuration = 0U;
Capabilities g_capabilities{};
bool g_initialized = false;

uint64_t read64(size_t offset) {
    return *reinterpret_cast<const volatile uint64_t*>(g_registers + offset);
}

void write64(size_t offset, uint64_t value) {
    *reinterpret_cast<volatile uint64_t*>(g_registers + offset) = value;
    __asm__ volatile("mfence" : : : "memory");
}

void reset_state() {
    g_registers = nullptr;
    g_mapped_virtual_base = 0U;
    g_original_configuration = 0U;
    g_capabilities = {};
    g_initialized = false;
}

bool unmap_registers() {
    if (g_mapped_virtual_base == 0U) return true;
    auto* space = memory::kernel_virtual_memory::address_space();
    if (space == nullptr) return false;
    if (memory::virtual_memory::unmap_page(space, g_mapped_virtual_base) !=
        memory::virtual_memory::Status::Ok) {
        return false;
    }
    g_mapped_virtual_base = 0U;
    g_registers = nullptr;
    return true;
}

} // namespace

Status initialize(uint64_t rsdp_physical_address) {
    if (g_initialized) return Status::AlreadyInitialized;
    if (rsdp_physical_address == 0U) return Status::InvalidArgument;

    acpi::TableView view{};
    const auto lookup = acpi::find_table(
        reinterpret_cast<const void*>(
            static_cast<uintptr_t>(rsdp_physical_address)),
        "HPET",
        &view);
    if (lookup == acpi::Status::TableNotFound) {
        return Status::TableUnavailable;
    }
    if (lookup != acpi::Status::Ok) return Status::InvalidTable;

    TableInfo table{};
    if (parse_acpi_table(view.address, view.length, &table) != TableStatus::Ok) {
        return Status::InvalidTable;
    }
    if ((table.physical_address & UINT64_C(7)) != 0U) {
        return Status::InvalidTable;
    }

    auto* space = memory::kernel_virtual_memory::address_space();
    if (space == nullptr) return Status::PagingUnavailable;

    constexpr uint64_t page_size = memory::virtual_memory::PAGE_SIZE;
    constexpr uint64_t page_mask = page_size - 1U;
    const uint64_t aligned = table.physical_address & ~page_mask;
    const uint64_t offset = table.physical_address & page_mask;
    if (offset + REGISTER_SPAN > page_size) return Status::InvalidTable;

    memory::virtual_memory::Mapping existing{};
    if (memory::virtual_memory::query_page(space, VIRTUAL_BASE, &existing) !=
        memory::virtual_memory::Status::NotMapped) {
        return Status::MappingFailed;
    }
    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    if (memory::virtual_memory::map_page(
            space, VIRTUAL_BASE, aligned, flags) !=
        memory::virtual_memory::Status::Ok) {
        return Status::MappingFailed;
    }

    g_mapped_virtual_base = VIRTUAL_BASE;
    g_registers = reinterpret_cast<volatile uint8_t*>(VIRTUAL_BASE + offset);

    Capabilities capabilities{};
    if (!decode_capabilities(read64(GENERAL_CAPABILITIES), &capabilities)) {
        static_cast<void>(unmap_registers());
        reset_state();
        return Status::InvalidCapabilities;
    }

    g_original_configuration = read64(GENERAL_CONFIGURATION);
    const uint64_t enabled =
        (g_original_configuration & ~CONFIG_LEGACY_REPLACEMENT) | CONFIG_ENABLE;
    write64(GENERAL_CONFIGURATION, enabled);
    g_capabilities = capabilities;
    g_initialized = true;

    // An all-ones counter is a strong MMIO failure signal and should not be
    // published as a usable clock.
    if (counter() == UINT64_MAX) {
        shutdown();
        return Status::CounterUnavailable;
    }
    return Status::Ok;
}

void shutdown() {
    if (g_registers != nullptr) {
        write64(GENERAL_CONFIGURATION, g_original_configuration);
    }
    static_cast<void>(unmap_registers());
    reset_state();
}

bool initialized() { return g_initialized; }

uint64_t counter() {
    if (!g_initialized || g_registers == nullptr) return 0U;
    const uint64_t value = read64(MAIN_COUNTER);
    return g_capabilities.counter_64_bit
        ? value
        : static_cast<uint32_t>(value);
}

uint32_t counter_period_femtoseconds() {
    return g_initialized ? g_capabilities.counter_period_femtoseconds : 0U;
}

const Capabilities* capabilities() {
    return g_initialized ? &g_capabilities : nullptr;
}

bool counter_advances(size_t read_budget) {
    if (!g_initialized || read_budget == 0U) return false;
    const uint64_t start = counter();
    for (size_t attempt = 0U; attempt < read_budget; ++attempt) {
        __asm__ volatile("pause" : : : "memory");
        if (counter() != start) return true;
    }
    return false;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "HPET already initialized";
        case Status::InvalidArgument: return "missing ACPI RSDP for HPET";
        case Status::TableUnavailable: return "ACPI HPET table not found";
        case Status::InvalidTable: return "invalid ACPI HPET table";
        case Status::PagingUnavailable: return "paging unavailable for HPET";
        case Status::MappingFailed: return "HPET MMIO mapping failed";
        case Status::InvalidCapabilities: return "invalid HPET capabilities";
        case Status::CounterUnavailable: return "HPET main counter unavailable";
    }
    return "unknown HPET status";
}

} // namespace arch::x86_64::hpet
