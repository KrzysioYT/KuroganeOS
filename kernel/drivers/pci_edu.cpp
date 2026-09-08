#include "pci_edu.hpp"

#include "pci.hpp"
#include "pci_msi.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/virtual_memory.hpp"

namespace drivers::pci_edu {
namespace {

constexpr uint16_t kVendorId = UINT16_C(0x1234);
constexpr uint16_t kDeviceId = UINT16_C(0x11E8);
constexpr uint64_t kVirtualBase = UINT64_C(0xFFFFB40000000000);
constexpr size_t kIdentification = 0x00U;
constexpr size_t kInterruptStatus = 0x24U;
constexpr size_t kInterruptRaise = 0x60U;
constexpr size_t kInterruptAcknowledge = 0x64U;
constexpr uint32_t kQualificationCause = UINT32_C(1);
constexpr uint32_t kIdentityLowMask = UINT32_C(0x0000FFFF);
constexpr uint32_t kIdentityLowValue = UINT32_C(0x000000ED);

volatile uint32_t* g_registers = nullptr;
pci::msi::Route g_route{};
uint64_t g_interrupt_count = 0U;
uint32_t g_last_causes = 0U;
Status g_status = Status::NotInitialized;
bool g_initialized = false;
bool g_route_active = false;

void relax() {
    __asm__ volatile("pause" : : : "memory");
}

uint32_t read_register(size_t offset) {
    return g_registers[offset / sizeof(uint32_t)];
}

void write_register(size_t offset, uint32_t value) {
    g_registers[offset / sizeof(uint32_t)] = value;
    __asm__ volatile("mfence" : : : "memory");
}

void interrupt_handler(arch::x86_64::interrupts::InterruptFrame&) {
    if (g_registers == nullptr) return;
    const uint32_t causes = read_register(kInterruptStatus);
    if (causes == 0U) return;
    write_register(kInterruptAcknowledge, causes);
    __atomic_fetch_or(&g_last_causes, causes, __ATOMIC_RELAXED);
    __atomic_fetch_add(&g_interrupt_count, UINT64_C(1), __ATOMIC_RELEASE);
}

bool map_registers(uint64_t physical_address) {
    constexpr uint64_t kPageMask = memory::virtual_memory::PAGE_SIZE - 1U;
    if ((physical_address & kPageMask) != 0U) return false;
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) return false;
    memory::virtual_memory::Mapping existing{};
    if (memory::virtual_memory::query_page(
            address_space, kVirtualBase, &existing) !=
        memory::virtual_memory::Status::NotMapped) {
        return false;
    }
    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    if (memory::virtual_memory::map_page(
            address_space, kVirtualBase, physical_address, flags) !=
        memory::virtual_memory::Status::Ok) {
        return false;
    }
    g_registers = reinterpret_cast<volatile uint32_t*>(kVirtualBase);
    return true;
}

void unmap_registers() {
    if (g_registers == nullptr) return;
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space != nullptr) {
        static_cast<void>(memory::virtual_memory::unmap_page(
            address_space, kVirtualBase));
    }
    g_registers = nullptr;
}

bool release_route() {
    if (!g_route_active) return true;
    const pci::msi::Status result = pci::msi::disable(&g_route);
    g_route_active = false;
    return result == pci::msi::Status::Ok;
}

} // namespace

Status initialize() {
    if (g_initialized) return Status::AlreadyInitialized;
    g_status = Status::NotInitialized;
    const pci::Device* device = pci::find(kVendorId, kDeviceId);
    if (device == nullptr) {
        g_status = Status::NotFound;
        return g_status;
    }

    bool is_io = false;
    const uint64_t bar = pci::bar_address(*device, 0U, &is_io);
    if (bar == 0U || is_io) {
        g_status = Status::InvalidBar;
        return g_status;
    }
    if (!map_registers(bar)) {
        g_status = Status::MmioMapFailed;
        return g_status;
    }
    if ((read_register(kIdentification) & kIdentityLowMask) !=
        kIdentityLowValue) {
        unmap_registers();
        g_status = Status::IdentityMismatch;
        return g_status;
    }

    const uint32_t pending = read_register(kInterruptStatus);
    if (pending != 0U) write_register(kInterruptAcknowledge, pending);
    const pci::msi::Status msi_status =
        pci::msi::enable(*device, interrupt_handler, &g_route);
    if (msi_status != pci::msi::Status::Ok) {
        unmap_registers();
        g_status = Status::MsiUnavailable;
        return g_status;
    }
    g_route_active = true;
    g_initialized = true;
    g_status = Status::Ok;
    return g_status;
}

bool msi_configured() {
    return g_initialized && g_route_active && g_registers != nullptr;
}

bool qualify_msi_delivery(uint32_t spin_budget) {
    if (!msi_configured() || spin_budget == 0U) {
        g_status = Status::NotInitialized;
        return false;
    }

    const uint32_t pending = read_register(kInterruptStatus);
    if (pending != 0U) write_register(kInterruptAcknowledge, pending);
    __atomic_store_n(&g_last_causes, UINT32_C(0), __ATOMIC_RELEASE);
    const uint64_t before =
        __atomic_load_n(&g_interrupt_count, __ATOMIC_ACQUIRE);
    write_register(kInterruptRaise, kQualificationCause);

    bool delivered = false;
    for (uint32_t attempt = 0U; attempt < spin_budget; ++attempt) {
        if (__atomic_load_n(&g_interrupt_count, __ATOMIC_ACQUIRE) != before) {
            delivered =
                (__atomic_load_n(&g_last_causes, __ATOMIC_ACQUIRE) &
                 kQualificationCause) != 0U;
            break;
        }
        relax();
    }

    const uint32_t remaining = read_register(kInterruptStatus);
    if (remaining != 0U) write_register(kInterruptAcknowledge, remaining);
    const bool teardown_ok = release_route();
    unmap_registers();
    g_initialized = false;
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
        case Status::InvalidBar: return "INVALID_BAR";
        case Status::MmioMapFailed: return "MMIO_MAP_FAILED";
        case Status::IdentityMismatch: return "IDENTITY_MISMATCH";
        case Status::MsiUnavailable: return "MSI_UNAVAILABLE";
        case Status::InterruptTimedOut: return "INTERRUPT_TIMED_OUT";
        case Status::TeardownFailed: return "TEARDOWN_FAILED";
    }
    return "UNKNOWN";
}

} // namespace drivers::pci_edu
