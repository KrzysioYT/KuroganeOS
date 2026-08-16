#include "apic.hpp"

#include "../../memory/kernel_virtual_memory.hpp"
#include "../../memory/virtual_memory.hpp"

namespace arch::x86_64::apic {
namespace {

constexpr uint64_t LOCAL_VIRTUAL_BASE = UINT64_C(0xFFFFB30000000000);
constexpr uint64_t IO_VIRTUAL_BASE = UINT64_C(0xFFFFB30000100000);
constexpr uint64_t IO_VIRTUAL_STRIDE = UINT64_C(0x10000);
constexpr size_t LOCAL_ID_REGISTER = 0x20U;
constexpr size_t LOCAL_VERSION_REGISTER = 0x30U;

bool g_prepared = false;
uint32_t g_local_id = 0U;
uint32_t g_local_version = 0U;
uint32_t g_io_versions[acpi::MAXIMUM_IO_APICS]{};
size_t g_io_count = 0U;

volatile uint32_t* map_register_page(uint64_t physical, uint64_t virtual_base) {
    if ((physical & (memory::virtual_memory::PAGE_SIZE - 1U)) != 0U) {
        return nullptr;
    }
    auto* space = memory::kernel_virtual_memory::address_space();
    if (space == nullptr) return nullptr;
    memory::virtual_memory::Mapping existing{};
    if (memory::virtual_memory::query_page(space, virtual_base, &existing) !=
        memory::virtual_memory::Status::NotMapped) {
        return nullptr;
    }
    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    if (memory::virtual_memory::map_page(
            space, virtual_base, physical, flags) !=
        memory::virtual_memory::Status::Ok) {
        return nullptr;
    }
    return reinterpret_cast<volatile uint32_t*>(virtual_base);
}

uint32_t io_read(volatile uint32_t* registers, uint8_t index) {
    registers[0] = index;
    __asm__ volatile("mfence" : : : "memory");
    return registers[4];
}

} // namespace

Status prepare(const acpi::Topology& topology) {
    g_prepared = false;
    g_local_id = 0U;
    g_local_version = 0U;
    g_io_count = 0U;
    if (topology.local_apic_address == 0U ||
        topology.io_apic_count == 0U ||
        topology.io_apic_count > acpi::MAXIMUM_IO_APICS) {
        return Status::InvalidTopology;
    }
    if (memory::kernel_virtual_memory::address_space() == nullptr) {
        return Status::PagingUnavailable;
    }
    volatile uint32_t* local = map_register_page(
        topology.local_apic_address, LOCAL_VIRTUAL_BASE);
    if (local == nullptr) return Status::MappingFailed;
    g_local_id = local[LOCAL_ID_REGISTER / sizeof(uint32_t)] >> 24U;
    g_local_version = local[LOCAL_VERSION_REGISTER / sizeof(uint32_t)];
    if ((g_local_version & 0xFFU) == 0U || g_local_version == UINT32_MAX) {
        return Status::HardwareUnavailable;
    }
    for (size_t index = 0U; index < topology.io_apic_count; ++index) {
        volatile uint32_t* io = map_register_page(
            topology.io_apics[index].address,
            IO_VIRTUAL_BASE + index * IO_VIRTUAL_STRIDE);
        if (io == nullptr) return Status::MappingFailed;
        const uint32_t version = io_read(io, 1U);
        if ((version & 0xFFU) == 0U || version == UINT32_MAX) {
            return Status::HardwareUnavailable;
        }
        g_io_versions[index] = version;
        ++g_io_count;
    }
    g_prepared = true;
    return Status::Ok;
}

bool prepared() { return g_prepared; }
uint32_t local_apic_id() { return g_local_id; }
uint32_t local_apic_version() { return g_local_version; }
size_t io_apic_count() { return g_io_count; }
uint32_t io_apic_version(size_t index) {
    return index < g_io_count ? g_io_versions[index] : 0U;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidTopology: return "invalid APIC topology";
        case Status::PagingUnavailable: return "paging unavailable";
        case Status::MappingFailed: return "APIC MMIO mapping failed";
        case Status::HardwareUnavailable: return "APIC registers unavailable";
    }
    return "unknown APIC status";
}

} // namespace arch::x86_64::apic
