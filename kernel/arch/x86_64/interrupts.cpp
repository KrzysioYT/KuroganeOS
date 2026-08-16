#include "interrupts.hpp"
#include "gdt.hpp"

#include "../../drivers/pic.hpp"

extern "C" void (*interrupt_stub_table[256])();

namespace arch::x86_64::interrupts {

namespace {

struct [[gnu::packed]] IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct [[gnu::packed]] IdtRegister {
    uint16_t limit;
    uint64_t base;
};

static_assert(sizeof(IdtEntry) == 16, "x86-64 IDT entries are 16 bytes");
static_assert(sizeof(IdtRegister) == 10, "x86-64 IDTR operand is 10 bytes");
static_assert(
    IRQ_VECTOR_BASE == drivers::pic::MASTER_VECTOR_OFFSET &&
        IRQ_COUNT == drivers::pic::IRQ_COUNT,
    "IDT IRQ layout must match the remapped PIC");

constexpr uint8_t kPresentInterruptGate = 0x8E;
constexpr uint8_t kPresentTrapGate = 0x8F;
constexpr uint8_t kGateDplShift = 5;
constexpr uint8_t kFirstSoftwareVector = IRQ_VECTOR_BASE + IRQ_COUNT;

alignas(16) static IdtEntry g_idt[IDT_ENTRY_COUNT];
static InterruptHandler g_handlers[IDT_ENTRY_COUNT];
static IrqHandler g_irq_handlers[IRQ_COUNT];
static IrqScheduleHook g_irq_schedule_hook = nullptr;
alignas(8) static uint64_t g_interrupt_counts[IDT_ENTRY_COUNT];

static bool g_initialized = false;
static uint8_t g_last_exception_vector = 0xFF;
static uint64_t g_last_exception_error_code = 0;
static uintptr_t g_last_page_fault_address = 0;

uint16_t code_segment_selector() {
    uint16_t selector = 0;
    __asm__ volatile("mov %%cs, %0" : "=r"(selector));
    return selector;
}

uintptr_t read_cr2() {
    uintptr_t value = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

void set_gate(
    uint8_t vector,
    void (*entry)(),
    uint16_t selector) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(entry);
    IdtEntry& gate = g_idt[vector];
    gate.offset_low = static_cast<uint16_t>(address & 0xFFFF);
    gate.selector = selector;
    if (vector == 8) {
        gate.ist = gdt::DOUBLE_FAULT_IST;
    } else if (vector == 2) {
        gate.ist = gdt::NMI_IST;
    } else if (vector == 18) {
        gate.ist = gdt::MACHINE_CHECK_IST;
    } else {
        gate.ist = 0;
    }
    gate.type_attributes = kPresentInterruptGate;
    gate.offset_middle =
        static_cast<uint16_t>((address >> 16) & 0xFFFF);
    gate.offset_high =
        static_cast<uint32_t>((address >> 32) & 0xFFFFFFFF);
    gate.reserved = 0;
}

void load_idt() {
    const IdtRegister descriptor = {
        static_cast<uint16_t>(sizeof(g_idt) - 1),
        reinterpret_cast<uint64_t>(&g_idt[0])
    };
    __asm__ volatile("lidt %0" : : "m"(descriptor) : "memory");
}

} // namespace

void initialize() {
    disable();

    const uint16_t selector = code_segment_selector();
    for (size_t i = 0; i < IDT_ENTRY_COUNT; ++i) {
        set_gate(
            static_cast<uint8_t>(i),
            ::interrupt_stub_table[i],
            selector);
        g_handlers[i] = nullptr;
        g_interrupt_counts[i] = 0;
    }

    for (size_t i = 0; i < IRQ_COUNT; ++i) {
        g_irq_handlers[i] = nullptr;
    }
    g_irq_schedule_hook = nullptr;

    g_last_exception_vector = 0xFF;
    g_last_exception_error_code = 0;
    g_last_page_fault_address = 0;
    load_idt();
    g_initialized = true;
}

bool initialized() {
    return g_initialized;
}

bool register_handler(uint8_t vector, InterruptHandler handler) {
    if (handler == nullptr) {
        return false;
    }

    __atomic_store_n(&g_handlers[vector], handler, __ATOMIC_RELEASE);
    return true;
}

void unregister_handler(uint8_t vector) {
    if (g_initialized) {
        // Revoke ring-3 access before removing the handler. Store ordering on
        // x86 keeps the IDT update visible before the null handler slot.
        __atomic_store_n(
            &g_idt[vector].type_attributes,
            kPresentInterruptGate,
            __ATOMIC_RELEASE);
    }
    __atomic_store_n(
        &g_handlers[vector],
        static_cast<InterruptHandler>(nullptr),
        __ATOMIC_RELEASE);
}

bool set_gate_privilege(uint8_t vector, GatePrivilege privilege) {
    if (!g_initialized) {
        return false;
    }

    const uint8_t level = static_cast<uint8_t>(privilege);
    if (level != static_cast<uint8_t>(GatePrivilege::Kernel) &&
        level != static_cast<uint8_t>(GatePrivilege::User)) {
        return false;
    }

    if (level == static_cast<uint8_t>(GatePrivilege::User)) {
        if (vector < kFirstSoftwareVector ||
            __atomic_load_n(&g_handlers[vector], __ATOMIC_ACQUIRE) == nullptr) {
            return false;
        }
    }

    const uint8_t attributes = static_cast<uint8_t>(
        kPresentInterruptGate | (level << kGateDplShift));
    __atomic_store_n(
        &g_idt[vector].type_attributes,
        attributes,
        __ATOMIC_RELEASE);
    return true;
}

bool set_gate_type(uint8_t vector, GateType type) {
    if (!g_initialized || vector < kFirstSoftwareVector ||
        __atomic_load_n(&g_handlers[vector], __ATOMIC_ACQUIRE) == nullptr) {
        return false;
    }
    const uint8_t current = __atomic_load_n(
        &g_idt[vector].type_attributes, __ATOMIC_ACQUIRE);
    const uint8_t type_bits = type == GateType::Trap
        ? kPresentTrapGate
        : kPresentInterruptGate;
    const uint8_t attributes = static_cast<uint8_t>(
        (current & UINT8_C(0x60)) | (type_bits & UINT8_C(0x9F)));
    __atomic_store_n(
        &g_idt[vector].type_attributes, attributes, __ATOMIC_RELEASE);
    return true;
}

bool register_irq_handler(uint8_t irq, IrqHandler handler) {
    if (irq >= IRQ_COUNT || handler == nullptr) {
        return false;
    }

    g_irq_handlers[irq] = handler;
    return true;
}

void unregister_irq_handler(uint8_t irq) {
    if (irq < IRQ_COUNT) {
        g_irq_handlers[irq] = nullptr;
    }
}

bool register_irq_schedule_hook(IrqScheduleHook hook) {
    if (!g_initialized || hook == nullptr || g_irq_schedule_hook != nullptr) {
        return false;
    }
    g_irq_schedule_hook = hook;
    return true;
}

void unregister_irq_schedule_hook(IrqScheduleHook hook) {
    if (hook != nullptr && g_irq_schedule_hook == hook) {
        g_irq_schedule_hook = nullptr;
    }
}

void enable() {
    __asm__ volatile("sti" : : : "memory");
}

void disable() {
    __asm__ volatile("cli" : : : "memory");
}

bool enabled() {
    uint64_t flags = 0;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return (flags & (1ULL << 9)) != 0;
}

uint64_t interrupt_count(uint8_t vector) {
    return __atomic_load_n(
        &g_interrupt_counts[vector],
        __ATOMIC_RELAXED);
}

uint8_t last_exception_vector() {
    return g_last_exception_vector;
}

uint64_t last_exception_error_code() {
    return g_last_exception_error_code;
}

uintptr_t last_page_fault_address() {
    return g_last_page_fault_address;
}

[[noreturn]] void halt() {
    disable();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

} // namespace arch::x86_64::interrupts

extern "C" arch::x86_64::interrupts::InterruptFrame*
x86_64_interrupt_dispatch(
    arch::x86_64::interrupts::InterruptFrame* frame) {
    using namespace arch::x86_64::interrupts;

    if (frame == nullptr || frame->vector >= IDT_ENTRY_COUNT) {
        halt();
    }

    const uint8_t vector = static_cast<uint8_t>(frame->vector);
    __atomic_fetch_add(
        &g_interrupt_counts[vector],
        static_cast<uint64_t>(1),
        __ATOMIC_RELAXED);

    if (vector < IRQ_VECTOR_BASE) {
        g_last_exception_vector = vector;
        g_last_exception_error_code = frame->error_code;
        if (vector == 14) {
            g_last_page_fault_address = read_cr2();
        }

        InterruptHandler handler =
            __atomic_load_n(&g_handlers[vector], __ATOMIC_ACQUIRE);
        if (handler != nullptr) {
            handler(*frame);
            return frame;
        }

        halt();
    }

    if (vector < IRQ_VECTOR_BASE + IRQ_COUNT) {
        const uint8_t irq =
            static_cast<uint8_t>(vector - IRQ_VECTOR_BASE);
        if (!drivers::pic::begin_irq(irq)) {
            return frame;
        }

        IrqHandler handler = g_irq_handlers[irq];
        if (handler != nullptr) {
            handler();
        }

        drivers::pic::send_eoi(irq);
        IrqScheduleHook schedule_hook = g_irq_schedule_hook;
        if (schedule_hook != nullptr) {
            InterruptFrame* selected = schedule_hook(irq, *frame);
            if (selected != nullptr) {
                return selected;
            }
        }
        return frame;
    }

    InterruptHandler handler =
        __atomic_load_n(&g_handlers[vector], __ATOMIC_ACQUIRE);
    if (handler != nullptr) {
        handler(*frame);
    }
    return frame;
}
