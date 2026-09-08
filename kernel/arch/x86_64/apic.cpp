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
constexpr size_t LOCAL_TASK_PRIORITY_REGISTER = 0x80U;
constexpr size_t LOCAL_EOI_REGISTER = 0xB0U;
constexpr size_t LOCAL_SPURIOUS_REGISTER = 0xF0U;
constexpr uint32_t LOCAL_SPURIOUS_SOFTWARE_ENABLE = UINT32_C(1) << 8U;
constexpr uint32_t CPUID_APIC_BIT = UINT32_C(1) << 9U;
constexpr uint32_t IA32_APIC_BASE_MSR = 0x1BU;
constexpr uint64_t IA32_APIC_BASE_ENABLE = UINT64_C(1) << 11U;
constexpr uint64_t IA32_APIC_BASE_X2_MODE = UINT64_C(1) << 10U;
constexpr uint64_t IA32_APIC_BASE_ADDRESS_MASK =
    UINT64_C(0x000FFFFFFFFFF000);

bool g_prepared = false;
bool g_local_enabled = false;
volatile uint32_t* g_local_registers = nullptr;
uint64_t g_local_physical_address = 0U;
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

bool cpu_supports_apic() {
    uint32_t eax = 1U;
    uint32_t ebx = 0U;
    uint32_t ecx = 0U;
    uint32_t edx = 0U;
    __asm__ volatile(
        "cpuid"
        : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & CPUID_APIC_BIT) != 0U;
}

uint64_t read_msr(uint32_t index) {
    uint32_t low = 0U;
    uint32_t high = 0U;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(index));
    return static_cast<uint64_t>(low) |
        (static_cast<uint64_t>(high) << 32U);
}

void write_msr(uint32_t index, uint64_t value) {
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(index),
          "a"(static_cast<uint32_t>(value)),
          "d"(static_cast<uint32_t>(value >> 32U))
        : "memory");
}

uint32_t local_read(size_t offset) {
    return g_local_registers[offset / sizeof(uint32_t)];
}

void local_write(size_t offset, uint32_t value) {
    g_local_registers[offset / sizeof(uint32_t)] = value;
    __asm__ volatile("mfence" : : : "memory");
}

} // namespace

Status prepare(const acpi::Topology& topology) {
    g_prepared = false;
    g_local_enabled = false;
    g_local_registers = nullptr;
    g_local_physical_address = 0U;
    g_local_id = 0U;
    g_local_version = 0U;
    g_io_count = 0U;
    if (topology.local_apic_address == 0U ||
        topology.io_apic_count == 0U ||
        topology.io_apic_count > acpi::MAXIMUM_IO_APICS) {
        return Status::InvalidTopology;
    }
    if (!cpu_supports_apic()) return Status::CpuUnsupported;
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
    g_local_registers = local;
    g_local_physical_address = topology.local_apic_address;
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

Status enable_local() {
    if (!g_prepared || g_local_registers == nullptr) return Status::NotPrepared;
    if (!cpu_supports_apic()) return Status::CpuUnsupported;

    const uint64_t base = read_msr(IA32_APIC_BASE_MSR);
    if ((base & IA32_APIC_BASE_X2_MODE) != 0U) {
        return Status::UnsupportedMode;
    }
    if ((base & IA32_APIC_BASE_ADDRESS_MASK) !=
        (g_local_physical_address & IA32_APIC_BASE_ADDRESS_MASK)) {
        return Status::BaseMismatch;
    }
    if ((base & IA32_APIC_BASE_ENABLE) == 0U) {
        write_msr(IA32_APIC_BASE_MSR, base | IA32_APIC_BASE_ENABLE);
    }

    local_write(LOCAL_TASK_PRIORITY_REGISTER, 0U);
    uint32_t spurious = local_read(LOCAL_SPURIOUS_REGISTER);
    spurious &= ~UINT32_C(0xFF);
    spurious |= static_cast<uint32_t>(SPURIOUS_VECTOR);
    spurious |= LOCAL_SPURIOUS_SOFTWARE_ENABLE;
    local_write(LOCAL_SPURIOUS_REGISTER, spurious);
    g_local_enabled = true;
    return Status::Ok;
}

bool prepared() { return g_prepared; }
bool local_enabled() {
    return __atomic_load_n(&g_local_enabled, __ATOMIC_ACQUIRE);
}
void send_eoi() {
    if (!local_enabled() || g_local_registers == nullptr) return;
    local_write(LOCAL_EOI_REGISTER, 0U);
}
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
        case Status::NotPrepared: return "Local APIC not prepared";
        case Status::CpuUnsupported: return "CPU does not support Local APIC";
        case Status::UnsupportedMode: return "x2APIC mode is not supported yet";
        case Status::BaseMismatch: return "Local APIC base does not match MADT";
    }
    return "unknown APIC status";
}

} // namespace arch::x86_64::apic
