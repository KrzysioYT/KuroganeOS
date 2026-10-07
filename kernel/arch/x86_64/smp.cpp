#include "smp.hpp"

#include "apic.hpp"
#include "interrupts.hpp"
#include "../../memory/kernel_virtual_memory.hpp"

namespace arch::x86_64::smp {
namespace {

constexpr uintptr_t kTrampolinePhysical = static_cast<uintptr_t>(0x7000U);
constexpr size_t kTrampolineCapacity = 4096U;
constexpr uint8_t kStartupVector =
    static_cast<uint8_t>(kTrampolinePhysical >> 12U);
constexpr size_t kApStackSize = 32U * 1024U;
constexpr uint32_t kStartupSpinBudget = UINT32_C(30000000);

struct CpuState {
    uint32_t apic_id;
    uint32_t acpi_id;
    uint8_t discovered;
    uint8_t online;
    uint8_t reserved0;
    uint8_t reserved1;
    uint64_t work_pending_generation;
    uint64_t work_completed_generation;
    uint64_t tlb_completed_generation;
};

alignas(64) CpuState g_cpus[MAXIMUM_CPUS]{};
alignas(16) uint8_t g_ap_stacks[MAXIMUM_CPUS][kApStackSize]{};
size_t g_discovered_count = 0U;
size_t g_online_count = 0U;
uint8_t g_initialized = 0U;

alignas(1) uint8_t g_work_lock = 0U;
WorkCallback g_work_callback = nullptr;
void* g_work_context = nullptr;
uint64_t g_work_generation = 0U;

alignas(1) uint8_t g_tlb_lock = 0U;
uintptr_t g_tlb_address = 0U;
uint64_t g_tlb_generation = 0U;

extern "C" const uint8_t smp_trampoline_start[];
extern "C" const uint8_t smp_trampoline_end[];
extern "C" const uint8_t smp_trampoline_mailbox_cr3[];
extern "C" const uint8_t smp_trampoline_mailbox_stack[];
extern "C" const uint8_t smp_trampoline_mailbox_entry[];
extern "C" const uint8_t smp_trampoline_mailbox_cpu_index[];
extern "C" const uint8_t smp_trampoline_mailbox_apic_id[];

void pause_cpu() {
    __asm__ volatile("pause" : : : "memory");
}

void lock(uint8_t* value) {
    while (__atomic_test_and_set(value, __ATOMIC_ACQUIRE)) {
        pause_cpu();
    }
}

void unlock(uint8_t* value) {
    __atomic_clear(value, __ATOMIC_RELEASE);
}

void invalidate_local(uintptr_t virtual_address) {
    __asm__ volatile(
        "invlpg (%0)"
        :
        : "r"(virtual_address)
        : "memory");
}

size_t trampoline_offset(const uint8_t* symbol) {
    return static_cast<size_t>(symbol - smp_trampoline_start);
}

template <typename T>
void patch_trampoline(size_t offset, T value) {
    auto* destination = reinterpret_cast<volatile T*>(
        kTrampolinePhysical + offset);
    *destination = value;
}

bool install_trampoline(
    uint64_t root_cr3,
    size_t cpu_index,
    uint32_t apic_id) {
    const size_t size = static_cast<size_t>(
        smp_trampoline_end - smp_trampoline_start);
    if (size == 0U || size > kTrampolineCapacity ||
        root_cr3 > UINT32_MAX || cpu_index > UINT32_MAX) {
        return false;
    }

    auto* destination =
        reinterpret_cast<volatile uint8_t*>(kTrampolinePhysical);
    for (size_t index = 0U; index < kTrampolineCapacity; ++index) {
        destination[index] = 0U;
    }
    for (size_t index = 0U; index < size; ++index) {
        destination[index] = smp_trampoline_start[index];
    }

    const uintptr_t stack_top =
        reinterpret_cast<uintptr_t>(&g_ap_stacks[cpu_index][kApStackSize]);
    patch_trampoline<uint32_t>(
        trampoline_offset(smp_trampoline_mailbox_cr3),
        static_cast<uint32_t>(root_cr3));
    patch_trampoline<uint64_t>(
        trampoline_offset(smp_trampoline_mailbox_stack),
        static_cast<uint64_t>(stack_top));
    patch_trampoline<uint64_t>(
        trampoline_offset(smp_trampoline_mailbox_entry),
        static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(&kurogane_smp_ap_entry)));
    patch_trampoline<uint32_t>(
        trampoline_offset(smp_trampoline_mailbox_cpu_index),
        static_cast<uint32_t>(cpu_index));
    patch_trampoline<uint32_t>(
        trampoline_offset(smp_trampoline_mailbox_apic_id),
        apic_id);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return true;
}

bool processor_enabled(const acpi::Processor& processor) {
    return (processor.flags & UINT32_C(0x3)) != 0U;
}

size_t find_cpu_by_apic(uint32_t apic_id) {
    for (size_t index = 0U; index < g_discovered_count; ++index) {
        if (g_cpus[index].discovered != 0U &&
            g_cpus[index].apic_id == apic_id) {
            return index;
        }
    }
    return MAXIMUM_CPUS;
}

uint64_t next_generation(uint64_t current) {
    return current == UINT64_MAX ? UINT64_C(1) : current + UINT64_C(1);
}

bool wait_for_online(size_t cpu_index) {
    for (uint32_t attempt = 0U; attempt < kStartupSpinBudget; ++attempt) {
        if (__atomic_load_n(
                &g_cpus[cpu_index].online,
                __ATOMIC_ACQUIRE) != 0U) {
            return true;
        }
        pause_cpu();
    }
    return false;
}

bool wait_for_work_generation(uint64_t generation, uint32_t spin_budget) {
    for (uint32_t attempt = 0U; attempt < spin_budget; ++attempt) {
        bool complete = true;
        for (size_t index = 0U; index < g_discovered_count; ++index) {
            if (__atomic_load_n(&g_cpus[index].online, __ATOMIC_ACQUIRE) == 0U) {
                continue;
            }
            if (__atomic_load_n(
                    &g_cpus[index].work_completed_generation,
                    __ATOMIC_ACQUIRE) != generation) {
                complete = false;
                break;
            }
        }
        if (complete) return true;
        pause_cpu();
    }
    return false;
}

bool wait_for_tlb_generation(uint64_t generation, uint32_t spin_budget) {
    for (uint32_t attempt = 0U; attempt < spin_budget; ++attempt) {
        bool complete = true;
        for (size_t index = 0U; index < g_discovered_count; ++index) {
            if (__atomic_load_n(&g_cpus[index].online, __ATOMIC_ACQUIRE) == 0U) {
                continue;
            }
            if (__atomic_load_n(
                    &g_cpus[index].tlb_completed_generation,
                    __ATOMIC_ACQUIRE) != generation) {
                complete = false;
                break;
            }
        }
        if (complete) return true;
        pause_cpu();
    }
    return false;
}

void work_ipi_handler(interrupts::InterruptFrame&) {
    apic::send_eoi();
}

void tlb_ipi_handler(interrupts::InterruptFrame&) {
    const size_t cpu_index = current_cpu_index();
    const uint64_t generation =
        __atomic_load_n(&g_tlb_generation, __ATOMIC_ACQUIRE);
    const uintptr_t address =
        __atomic_load_n(&g_tlb_address, __ATOMIC_ACQUIRE);
    invalidate_local(address);
    if (cpu_index < g_discovered_count) {
        __atomic_store_n(
            &g_cpus[cpu_index].tlb_completed_generation,
            generation,
            __ATOMIC_RELEASE);
    }
    apic::send_eoi();
}

void qualification_worker(size_t cpu_index, void* context) {
    auto* hits = static_cast<uint64_t*>(context);
    if (hits == nullptr || cpu_index >= MAXIMUM_CPUS) return;
    __atomic_fetch_add(&hits[cpu_index], UINT64_C(1), __ATOMIC_RELAXED);
}

} // namespace

Status initialize(const acpi::Topology& topology) {
    if (__atomic_load_n(&g_initialized, __ATOMIC_ACQUIRE) != 0U) {
        return Status::AlreadyInitialized;
    }
    if (!apic::prepared() || !apic::local_enabled()) {
        return Status::ApicUnavailable;
    }
    if (!interrupts::initialized()) {
        return Status::IdtUnavailable;
    }
    if (topology.processor_count == 0U ||
        topology.processor_count > MAXIMUM_CPUS) {
        return Status::InvalidTopology;
    }

    const uint64_t root_cr3 =
        memory::kernel_virtual_memory::root_table_physical();
    if (root_cr3 == 0U || root_cr3 > UINT32_MAX) {
        return Status::PageTableUnavailable;
    }
    const size_t trampoline_size = static_cast<size_t>(
        smp_trampoline_end - smp_trampoline_start);
    if (trampoline_size == 0U || trampoline_size > kTrampolineCapacity) {
        return Status::TrampolineTooLarge;
    }

    for (size_t index = 0U; index < MAXIMUM_CPUS; ++index) {
        g_cpus[index] = {};
    }
    g_discovered_count = 0U;
    __atomic_store_n(&g_online_count, size_t{0U}, __ATOMIC_RELEASE);

    const uint32_t bsp_apic_id = apic::current_apic_id();
    const acpi::Processor* bsp = nullptr;
    for (size_t index = 0U; index < topology.processor_count; ++index) {
        if (processor_enabled(topology.processors[index]) &&
            topology.processors[index].apic_id == bsp_apic_id) {
            bsp = &topology.processors[index];
            break;
        }
    }
    if (bsp == nullptr) return Status::InvalidTopology;

    g_cpus[0].apic_id = bsp->apic_id;
    g_cpus[0].acpi_id = bsp->acpi_id;
    g_cpus[0].discovered = 1U;
    g_cpus[0].online = 1U;
    g_discovered_count = 1U;
    __atomic_store_n(&g_online_count, size_t{1U}, __ATOMIC_RELEASE);

    for (size_t index = 0U; index < topology.processor_count; ++index) {
        const acpi::Processor& processor = topology.processors[index];
        if (!processor_enabled(processor) ||
            processor.apic_id == bsp_apic_id) {
            continue;
        }
        if (g_discovered_count >= MAXIMUM_CPUS) {
            return Status::InvalidTopology;
        }
        CpuState& state = g_cpus[g_discovered_count];
        state.apic_id = processor.apic_id;
        state.acpi_id = processor.acpi_id;
        state.discovered = 1U;
        ++g_discovered_count;
    }

    if (!interrupts::register_handler(TLB_SHOOTDOWN_VECTOR, tlb_ipi_handler) ||
        !interrupts::register_handler(WORK_VECTOR, work_ipi_handler)) {
        interrupts::unregister_handler(TLB_SHOOTDOWN_VECTOR);
        interrupts::unregister_handler(WORK_VECTOR);
        return Status::HandlerRegistrationFailed;
    }

    for (size_t cpu_index = 1U;
         cpu_index < g_discovered_count;
         ++cpu_index) {
        if (!install_trampoline(
                root_cr3,
                cpu_index,
                g_cpus[cpu_index].apic_id)) {
            return Status::TrampolineTooLarge;
        }
        if (apic::send_init(g_cpus[cpu_index].apic_id) != apic::Status::Ok) {
            return Status::ApStartupTimeout;
        }
        for (uint32_t delay = 0U; delay < UINT32_C(200000); ++delay) {
            pause_cpu();
        }
        if (apic::send_startup(
                g_cpus[cpu_index].apic_id,
                kStartupVector) != apic::Status::Ok) {
            return Status::ApStartupTimeout;
        }
        for (uint32_t delay = 0U; delay < UINT32_C(20000); ++delay) {
            pause_cpu();
        }
        if (__atomic_load_n(
                &g_cpus[cpu_index].online,
                __ATOMIC_ACQUIRE) == 0U) {
            static_cast<void>(apic::send_startup(
                g_cpus[cpu_index].apic_id,
                kStartupVector));
        }
        if (!wait_for_online(cpu_index)) {
            return Status::ApStartupTimeout;
        }
    }

    __atomic_store_n(&g_initialized, uint8_t{1U}, __ATOMIC_RELEASE);
    return Status::Ok;
}

bool initialized() {
    return __atomic_load_n(&g_initialized, __ATOMIC_ACQUIRE) != 0U;
}

size_t discovered_cpu_count() {
    return g_discovered_count;
}

size_t online_cpu_count() {
    return __atomic_load_n(&g_online_count, __ATOMIC_ACQUIRE);
}

uint32_t current_apic_id() {
    return apic::current_apic_id();
}

size_t current_cpu_index() {
    return find_cpu_by_apic(apic::current_apic_id());
}

Status run_on_all_cpus(
    WorkCallback callback,
    void* context,
    uint32_t spin_budget) {
    if (!initialized() || callback == nullptr || spin_budget == 0U) {
        return Status::InvalidTopology;
    }

    lock(&g_work_lock);
    const uint64_t generation = next_generation(g_work_generation);
    g_work_callback = callback;
    g_work_context = context;
    __atomic_store_n(&g_work_generation, generation, __ATOMIC_RELEASE);

    const size_t local_index = current_cpu_index();
    if (local_index >= g_discovered_count) {
        g_work_callback = nullptr;
        g_work_context = nullptr;
        unlock(&g_work_lock);
        return Status::InvalidTopology;
    }

    for (size_t index = 0U; index < g_discovered_count; ++index) {
        if (__atomic_load_n(&g_cpus[index].online, __ATOMIC_ACQUIRE) == 0U) {
            continue;
        }
        __atomic_store_n(
            &g_cpus[index].work_pending_generation,
            generation,
            __ATOMIC_RELEASE);
        if (index != local_index &&
            apic::send_ipi(
                g_cpus[index].apic_id,
                WORK_VECTOR) != apic::Status::Ok) {
            g_work_callback = nullptr;
            g_work_context = nullptr;
            unlock(&g_work_lock);
            return Status::WorkTimeout;
        }
    }

    callback(local_index, context);
    __atomic_store_n(
        &g_cpus[local_index].work_completed_generation,
        generation,
        __ATOMIC_RELEASE);

    const bool completed =
        wait_for_work_generation(generation, spin_budget);
    g_work_callback = nullptr;
    g_work_context = nullptr;
    unlock(&g_work_lock);
    return completed ? Status::Ok : Status::WorkTimeout;
}

Status shootdown_page(uintptr_t virtual_address, uint32_t spin_budget) {
    invalidate_local(virtual_address);
    if (!initialized() || online_cpu_count() <= 1U) {
        return Status::Ok;
    }
    if (spin_budget == 0U) return Status::ShootdownTimeout;

    lock(&g_tlb_lock);
    const uint64_t generation = next_generation(g_tlb_generation);
    __atomic_store_n(&g_tlb_address, virtual_address, __ATOMIC_RELEASE);
    __atomic_store_n(&g_tlb_generation, generation, __ATOMIC_RELEASE);

    const size_t local_index = current_cpu_index();
    if (local_index >= g_discovered_count) {
        unlock(&g_tlb_lock);
        return Status::InvalidTopology;
    }
    __atomic_store_n(
        &g_cpus[local_index].tlb_completed_generation,
        generation,
        __ATOMIC_RELEASE);

    for (size_t index = 0U; index < g_discovered_count; ++index) {
        if (index == local_index ||
            __atomic_load_n(&g_cpus[index].online, __ATOMIC_ACQUIRE) == 0U) {
            continue;
        }
        if (apic::send_ipi(
                g_cpus[index].apic_id,
                TLB_SHOOTDOWN_VECTOR) != apic::Status::Ok) {
            unlock(&g_tlb_lock);
            return Status::ShootdownTimeout;
        }
    }

    const bool completed =
        wait_for_tlb_generation(generation, spin_budget);
    unlock(&g_tlb_lock);
    return completed ? Status::Ok : Status::ShootdownTimeout;
}

bool qualify_parallel_dispatch() {
    if (!initialized() || online_cpu_count() == 0U) return false;
    uint64_t hits[MAXIMUM_CPUS]{};
    if (run_on_all_cpus(
            qualification_worker,
            hits) != Status::Ok) {
        return false;
    }

    size_t verified = 0U;
    for (size_t index = 0U; index < g_discovered_count; ++index) {
        if (__atomic_load_n(&g_cpus[index].online, __ATOMIC_ACQUIRE) == 0U) {
            continue;
        }
        if (__atomic_load_n(&hits[index], __ATOMIC_ACQUIRE) != UINT64_C(1)) {
            return false;
        }
        ++verified;
    }
    return verified == online_cpu_count();
}

bool qualify_tlb_shootdown() {
    if (!initialized()) return false;
    const uintptr_t address =
        reinterpret_cast<uintptr_t>(&qualify_tlb_shootdown);
    return shootdown_page(address) == Status::Ok;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "SMP already initialized";
        case Status::InvalidTopology: return "invalid SMP CPU topology";
        case Status::ApicUnavailable: return "Local APIC is unavailable";
        case Status::IdtUnavailable: return "IDT is unavailable";
        case Status::PageTableUnavailable:
            return "AP startup page-table root is unavailable";
        case Status::TrampolineTooLarge:
            return "AP startup trampoline is invalid or too large";
        case Status::HandlerRegistrationFailed:
            return "SMP IPI handler registration failed";
        case Status::ApStartupTimeout: return "AP startup timed out";
        case Status::WorkTimeout: return "SMP work dispatch timed out";
        case Status::ShootdownTimeout: return "TLB shootdown timed out";
    }
    return "unknown SMP status";
}

} // namespace arch::x86_64::smp

extern "C" [[noreturn]] void kurogane_smp_ap_entry(
    uint32_t cpu_index,
    uint32_t expected_apic_id) {
    using namespace arch::x86_64;
    if (cpu_index >= smp::discovered_cpu_count() ||
        !interrupts::load_current_cpu() ||
        apic::enable_local() != apic::Status::Ok ||
        apic::current_apic_id() != expected_apic_id) {
        interrupts::disable();
        for (;;) __asm__ volatile("hlt");
    }

    __atomic_store_n(
        &smp::g_cpus[cpu_index].online,
        uint8_t{1U},
        __ATOMIC_RELEASE);
    __atomic_fetch_add(
        &smp::g_online_count,
        size_t{1U},
        __ATOMIC_ACQ_REL);

    for (;;) {
        interrupts::disable();
        const uint64_t pending = __atomic_load_n(
            &smp::g_cpus[cpu_index].work_pending_generation,
            __ATOMIC_ACQUIRE);
        const uint64_t completed = __atomic_load_n(
            &smp::g_cpus[cpu_index].work_completed_generation,
            __ATOMIC_ACQUIRE);
        if (pending != 0U && pending != completed) {
            smp::WorkCallback callback =
                __atomic_load_n(&smp::g_work_callback, __ATOMIC_ACQUIRE);
            void* context =
                __atomic_load_n(&smp::g_work_context, __ATOMIC_ACQUIRE);
            if (callback != nullptr) {
                callback(cpu_index, context);
            }
            __atomic_store_n(
                &smp::g_cpus[cpu_index].work_completed_generation,
                pending,
                __ATOMIC_RELEASE);
            continue;
        }
        interrupts::enable();
        __asm__ volatile("hlt");
    }
}
