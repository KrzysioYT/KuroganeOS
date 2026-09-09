#include "e1000e_msix_qualification.hpp"

#include <stddef.h>

#include "pci.hpp"
#include "pci_bar.hpp"
#include "pci_msix.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/virtual_memory.hpp"

namespace drivers::e1000e_msix_qualification {
namespace {

constexpr uint16_t kIntelVendor = UINT16_C(0x8086);
constexpr uint16_t kE1000e82574L = UINT16_C(0x10D3);
constexpr uint16_t kCommandDecodeMask = UINT16_C(0x0007);
constexpr uint16_t kCommandMemorySpace = UINT16_C(1) << 1U;
constexpr uint64_t kRegisterVirtualBase = UINT64_C(0xFFFFB50000000000);
constexpr uint64_t kMsiXVirtualBase = UINT64_C(0xFFFFB50000100000);
constexpr uint64_t kMaximumRegionBytes = UINT64_C(256) * 1024U;

constexpr size_t kInterruptCauseRead = 0x00C0U;
constexpr size_t kInterruptCauseSet = 0x00C8U;
constexpr size_t kInterruptMaskSet = 0x00D0U;
constexpr size_t kInterruptMaskClear = 0x00D8U;
constexpr size_t kInterruptVectorAllocation = 0x00E4U;
constexpr uint32_t kLinkStatusChange = UINT32_C(1) << 2U;
constexpr uint32_t kOtherCause = UINT32_C(1) << 24U;
constexpr uint32_t kQualificationInterruptMask =
    kLinkStatusChange | kOtherCause;
constexpr uint32_t kIvarOtherShift = 16U;
constexpr uint32_t kIvarEntryMask = UINT32_C(0x0F);
constexpr uint32_t kIvarEntryValid = UINT32_C(0x08);
constexpr uint32_t kMsiXEntry = UINT32_C(0);

struct Mapping {
    uint64_t virtual_base;
    volatile uint8_t* region;
    size_t mapped_pages;
    size_t bytes;
};

pci::Device g_device{};
Mapping g_register_mapping{};
Mapping g_msix_mapping{};
pci::msix::MmioRegion g_msix_region{};
pci::msix::Route g_route{};
uint64_t g_interrupt_count = 0U;
uint32_t g_last_causes = 0U;
uint32_t g_original_ivar = 0U;
uint32_t g_original_interrupt_mask = 0U;
uint16_t g_original_command = 0U;
Status g_status = Status::NotInitialized;
bool g_initialized = false;
bool g_ivar_saved = false;
bool g_interrupt_mask_saved = false;
bool g_command_saved = false;
bool g_route_active = false;

void relax() {
    __asm__ volatile("pause" : : : "memory");
}

volatile uint32_t* register_address(size_t offset) {
    return reinterpret_cast<volatile uint32_t*>(
        g_register_mapping.region + offset);
}

uint32_t read_register(size_t offset) {
    const uint32_t value = *register_address(offset);
    __asm__ volatile("lfence" : : : "memory");
    return value;
}

void write_register(size_t offset, uint32_t value) {
    *register_address(offset) = value;
    __asm__ volatile("mfence" : : : "memory");
}

bool mapping_empty(const Mapping& mapping) {
    return mapping.region == nullptr && mapping.mapped_pages == 0U;
}

void unmap_region(Mapping* mapping) {
    if (mapping == nullptr || mapping_empty(*mapping)) return;
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space != nullptr) {
        for (size_t index = 0U; index < mapping->mapped_pages; ++index) {
            static_cast<void>(memory::virtual_memory::unmap_page(
                address_space,
                mapping->virtual_base +
                    index * memory::virtual_memory::PAGE_SIZE));
        }
    }
    *mapping = {};
}

bool map_region(
    uint64_t physical_address,
    uint64_t bytes,
    uint64_t virtual_base,
    Mapping* output) {
    if (output == nullptr || !mapping_empty(*output) ||
        physical_address == 0U || bytes == 0U ||
        bytes > kMaximumRegionBytes || bytes > SIZE_MAX) {
        return false;
    }
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) return false;

    constexpr uint64_t kPageSize = memory::virtual_memory::PAGE_SIZE;
    constexpr uint64_t kPageMask = kPageSize - 1U;
    if ((virtual_base & kPageMask) != 0U) return false;
    const uint64_t aligned_physical = physical_address & ~kPageMask;
    const uint64_t page_offset = physical_address & kPageMask;
    if (bytes > UINT64_MAX - page_offset) return false;
    const uint64_t span = bytes + page_offset;
    if (span > UINT64_MAX - kPageMask) return false;
    const uint64_t page_count64 = (span + kPageMask) / kPageSize;
    if (page_count64 == 0U || page_count64 > SIZE_MAX) return false;
    const uint64_t final_page_delta = (page_count64 - 1U) * kPageSize;
    if (aligned_physical > UINT64_MAX - final_page_delta ||
        virtual_base > UINT64_MAX - final_page_delta) {
        return false;
    }
    const size_t page_count = static_cast<size_t>(page_count64);

    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    size_t mapped = 0U;
    for (; mapped < page_count; ++mapped) {
        const uint64_t virtual_page =
            virtual_base + mapped * kPageSize;
        const uint64_t physical_page =
            aligned_physical + mapped * kPageSize;
        memory::virtual_memory::Mapping existing{};
        if (memory::virtual_memory::query_page(
                address_space, virtual_page, &existing) !=
                memory::virtual_memory::Status::NotMapped ||
            memory::virtual_memory::map_page(
                address_space, virtual_page, physical_page, flags) !=
                memory::virtual_memory::Status::Ok) {
            break;
        }
    }
    if (mapped != page_count) {
        while (mapped != 0U) {
            --mapped;
            static_cast<void>(memory::virtual_memory::unmap_page(
                address_space, virtual_base + mapped * kPageSize));
        }
        return false;
    }

    output->virtual_base = virtual_base;
    output->region = reinterpret_cast<volatile uint8_t*>(
        virtual_base + page_offset);
    output->mapped_pages = page_count;
    output->bytes = static_cast<size_t>(bytes);
    return true;
}

uint32_t qualification_ivar(uint32_t original) {
    const uint32_t field_mask = kIvarEntryMask << kIvarOtherShift;
    const uint32_t field =
        (kIvarEntryValid | kMsiXEntry) << kIvarOtherShift;
    return (original & ~field_mask) | field;
}

void interrupt_handler(arch::x86_64::interrupts::InterruptFrame&) {
    if (g_register_mapping.region == nullptr) return;
    const uint32_t causes = read_register(kInterruptCauseRead);
    if ((causes & kLinkStatusChange) == 0U) return;
    __atomic_fetch_or(&g_last_causes, causes, __ATOMIC_RELAXED);
    __atomic_fetch_add(&g_interrupt_count, UINT64_C(1), __ATOMIC_RELEASE);
}

void abandon_mappings() {
    if (g_ivar_saved && g_register_mapping.region != nullptr) {
        write_register(kInterruptMaskClear, UINT32_MAX);
        static_cast<void>(read_register(kInterruptCauseRead));
        write_register(kInterruptVectorAllocation, g_original_ivar);
    }
    g_ivar_saved = false;
    if (g_interrupt_mask_saved && g_register_mapping.region != nullptr) {
        write_register(kInterruptMaskSet, g_original_interrupt_mask);
    }
    g_interrupt_mask_saved = false;
    unmap_region(&g_msix_mapping);
    unmap_region(&g_register_mapping);
    if (g_command_saved) {
        pci::write16(g_device, 0x04U, g_original_command);
        g_command_saved = false;
    }
    g_msix_region = {};
    g_device = {};
}

bool release_resources() {
    if (g_register_mapping.region != nullptr) {
        write_register(kInterruptMaskClear, UINT32_MAX);
        static_cast<void>(read_register(kInterruptCauseRead));
        if (g_ivar_saved) {
            write_register(kInterruptVectorAllocation, g_original_ivar);
            g_ivar_saved = false;
        }
    }
    if (g_route_active) {
        if (pci::msix::disable(&g_route) != pci::msix::Status::Ok) {
            return false;
        }
        g_route_active = false;
    }
    if (g_interrupt_mask_saved && g_register_mapping.region != nullptr) {
        write_register(kInterruptMaskSet, g_original_interrupt_mask);
        g_interrupt_mask_saved = false;
    }
    if (g_command_saved) {
        pci::write16(g_device, 0x04U, g_original_command);
        g_command_saved = false;
    }
    unmap_region(&g_msix_mapping);
    unmap_region(&g_register_mapping);
    g_msix_region = {};
    g_device = {};
    g_initialized = false;
    return true;
}

} // namespace

Status initialize() {
    if (g_initialized) return Status::AlreadyInitialized;
    g_status = Status::NotInitialized;

    const pci::Device* const device =
        pci::find(kIntelVendor, kE1000e82574L);
    if (device == nullptr) {
        g_status = Status::NotFound;
        return g_status;
    }

    pci::MsiXInfo msix_info{};
    if (!pci::read_msix_info(*device, &msix_info)) {
        g_status = Status::CapabilityMalformed;
        return g_status;
    }
    if (msix_info.enabled) {
        g_status = Status::MsiXUnavailable;
        return g_status;
    }
    if (msix_info.table_bar != msix_info.pending_bit_array_bar ||
        msix_info.table_bar == 0U || msix_info.table_bar > 5U) {
        g_status = Status::UnsupportedRegion;
        return g_status;
    }

    const uint16_t original_command = pci::read16(*device, 0x04U);
    pci::write16(
        *device,
        0x04U,
        static_cast<uint16_t>(original_command & ~kCommandDecodeMask));
    pci::bar::Info register_bar{};
    pci::bar::Info msix_bar{};
    const pci::bar::Status register_status =
        pci::bar::probe_disabled(*device, 0U, &register_bar);
    const pci::bar::Status msix_status = register_status == pci::bar::Status::Ok
        ? pci::bar::probe_disabled(*device, msix_info.table_bar, &msix_bar)
        : pci::bar::Status::InvalidArgument;
    pci::write16(*device, 0x04U, original_command);

    if (register_status != pci::bar::Status::Ok ||
        msix_status != pci::bar::Status::Ok) {
        g_status = Status::BarProbeFailed;
        return g_status;
    }
    if (register_bar.kind == pci::bar::Kind::Io ||
        msix_bar.kind == pci::bar::Kind::Io ||
        register_bar.size < memory::virtual_memory::PAGE_SIZE ||
        msix_bar.size < memory::virtual_memory::PAGE_SIZE ||
        register_bar.size > kMaximumRegionBytes ||
        msix_bar.size > kMaximumRegionBytes) {
        g_status = Status::UnsupportedRegion;
        return g_status;
    }

    g_device = *device;
    g_original_command = original_command;
    g_command_saved = true;
    pci::write16(
        g_device,
        0x04U,
        static_cast<uint16_t>(original_command | kCommandMemorySpace));
    if ((pci::read16(g_device, 0x04U) & kCommandMemorySpace) == 0U) {
        abandon_mappings();
        g_status = Status::UnsupportedRegion;
        return g_status;
    }

    if (!map_region(
            register_bar.physical_address,
            register_bar.size,
            kRegisterVirtualBase,
            &g_register_mapping) ||
        !map_region(
            msix_bar.physical_address,
            msix_bar.size,
            kMsiXVirtualBase,
            &g_msix_mapping)) {
        abandon_mappings();
        g_status = Status::MmioMapFailed;
        return g_status;
    }

    g_msix_region.bar_index = msix_info.table_bar;
    g_msix_region.physical_address = msix_bar.physical_address;
    g_msix_region.virtual_address = g_msix_mapping.region;
    g_msix_region.bytes = g_msix_mapping.bytes;

    g_original_interrupt_mask = read_register(kInterruptMaskSet);
    g_interrupt_mask_saved = true;
    write_register(kInterruptMaskClear, UINT32_MAX);
    static_cast<void>(read_register(kInterruptCauseRead));
    g_original_ivar = read_register(kInterruptVectorAllocation);
    g_ivar_saved = true;
    write_register(
        kInterruptVectorAllocation,
        qualification_ivar(g_original_ivar));

    const pci::msix::Status route_status = pci::msix::enable_single(
        g_device,
        static_cast<uint16_t>(kMsiXEntry),
        g_msix_region,
        g_msix_region,
        interrupt_handler,
        &g_route);
    if (route_status != pci::msix::Status::Ok) {
        abandon_mappings();
        g_status = Status::MsiXUnavailable;
        return g_status;
    }

    g_route_active = true;
    g_initialized = true;
    g_status = Status::Ok;
    return g_status;
}

bool msix_configured() {
    return g_initialized && g_route_active &&
        g_register_mapping.region != nullptr &&
        g_msix_mapping.region != nullptr;
}

bool qualify_delivery(uint32_t spin_budget) {
    if (!msix_configured() || spin_budget == 0U) {
        g_status = Status::NotInitialized;
        return false;
    }

    write_register(kInterruptMaskClear, UINT32_MAX);
    static_cast<void>(read_register(kInterruptCauseRead));
    __atomic_store_n(&g_last_causes, UINT32_C(0), __ATOMIC_RELEASE);
    const uint64_t before =
        __atomic_load_n(&g_interrupt_count, __ATOMIC_ACQUIRE);
    // 82574L converts non-queue causes such as LSC into the MSI-X OTHER
    // cause. Both bits must be unmasked: observing only the synthetic ICS
    // write without OTHER routing would not prove an MSI-X notification.
    write_register(kInterruptMaskSet, kQualificationInterruptMask);
    write_register(kInterruptCauseSet, kLinkStatusChange);

    bool delivered = false;
    for (uint32_t attempt = 0U; attempt < spin_budget; ++attempt) {
        if (__atomic_load_n(&g_interrupt_count, __ATOMIC_ACQUIRE) != before) {
            delivered =
                (__atomic_load_n(&g_last_causes, __ATOMIC_ACQUIRE) &
                 kLinkStatusChange) != 0U;
            break;
        }
        relax();
    }

    const bool teardown_ok = release_resources();
    if (!teardown_ok) {
        g_status = Status::TeardownFailed;
        return false;
    }
    g_status = delivered ? Status::Ok : Status::InterruptTimedOut;
    return delivered;
}

Status status() { return g_status; }

const char* status_name(Status status_value) {
    switch (status_value) {
        case Status::Ok: return "OK";
        case Status::AlreadyInitialized: return "ALREADY_INITIALIZED";
        case Status::NotInitialized: return "NOT_INITIALIZED";
        case Status::NotFound: return "NOT_FOUND";
        case Status::CapabilityMalformed: return "CAPABILITY_MALFORMED";
        case Status::BarProbeFailed: return "BAR_PROBE_FAILED";
        case Status::UnsupportedRegion: return "UNSUPPORTED_REGION";
        case Status::MmioMapFailed: return "MMIO_MAP_FAILED";
        case Status::MsiXUnavailable: return "MSIX_UNAVAILABLE";
        case Status::InterruptTimedOut: return "INTERRUPT_TIMED_OUT";
        case Status::TeardownFailed: return "TEARDOWN_FAILED";
    }
    return "UNKNOWN";
}

} // namespace drivers::e1000e_msix_qualification
