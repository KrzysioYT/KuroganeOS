#include "pic.hpp"

namespace drivers::pic {

namespace {

constexpr uint16_t MASTER_COMMAND = 0x20;
constexpr uint16_t MASTER_DATA = 0x21;
constexpr uint16_t SLAVE_COMMAND = 0xA0;
constexpr uint16_t SLAVE_DATA = 0xA1;

constexpr uint8_t ICW1_INITIALIZE = 0x10;
constexpr uint8_t ICW1_ICW4 = 0x01;
constexpr uint8_t ICW4_8086 = 0x01;
constexpr uint8_t PIC_EOI = 0x20;
constexpr uint8_t OCW3_READ_ISR = 0x0B;

static bool g_initialized = false;

inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
        : "memory");
}

inline uint8_t in8(uint16_t port) {
    uint8_t value = 0;
    __asm__ volatile(
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
        : "memory");
    return value;
}

inline void io_wait() {
    out8(0x80, 0);
}

uint8_t read_isr(uint16_t command_port) {
    out8(command_port, OCW3_READ_ISR);
    return in8(command_port);
}

bool spurious_irq(uint8_t irq) {
    if (irq == 7) {
        return (read_isr(MASTER_COMMAND) & 0x80) == 0;
    }
    if (irq == 15) {
        return (read_isr(SLAVE_COMMAND) & 0x80) == 0;
    }
    return false;
}

} // namespace

void initialize() {
    const bool restore_interrupts = []() {
        uint64_t flags = 0;
        __asm__ volatile("pushfq; popq %0" : "=r"(flags));
        __asm__ volatile("cli" : : : "memory");
        return (flags & (1ULL << 9)) != 0;
    }();

    out8(MASTER_COMMAND, ICW1_INITIALIZE | ICW1_ICW4);
    io_wait();
    out8(SLAVE_COMMAND, ICW1_INITIALIZE | ICW1_ICW4);
    io_wait();

    out8(MASTER_DATA, MASTER_VECTOR_OFFSET);
    io_wait();
    out8(SLAVE_DATA, SLAVE_VECTOR_OFFSET);
    io_wait();

    // Slave PIC is wired to master's IRQ2; slave identity is cascade line 2.
    out8(MASTER_DATA, 1u << 2);
    io_wait();
    out8(SLAVE_DATA, 2);
    io_wait();

    out8(MASTER_DATA, ICW4_8086);
    io_wait();
    out8(SLAVE_DATA, ICW4_8086);
    io_wait();

    mask_all();
    g_initialized = true;

    if (restore_interrupts) {
        __asm__ volatile("sti" : : : "memory");
    }
}

bool initialized() {
    return g_initialized;
}

void mask_all() {
    out8(MASTER_DATA, 0xFF);
    out8(SLAVE_DATA, 0xFF);
}

bool mask(uint8_t irq) {
    if (irq >= IRQ_COUNT) {
        return false;
    }

    if (irq < 8) {
        const uint8_t value = static_cast<uint8_t>(
            in8(MASTER_DATA) | (1u << irq));
        out8(MASTER_DATA, value);
        return true;
    }

    const uint8_t slave_irq = static_cast<uint8_t>(irq - 8);
    const uint8_t slave_mask = static_cast<uint8_t>(
        in8(SLAVE_DATA) | (1u << slave_irq));
    out8(SLAVE_DATA, slave_mask);

    if (slave_mask == 0xFF) {
        out8(
            MASTER_DATA,
            static_cast<uint8_t>(in8(MASTER_DATA) | (1u << 2)));
    }
    return true;
}

bool unmask(uint8_t irq) {
    if (irq >= IRQ_COUNT) {
        return false;
    }

    if (irq < 8) {
        const uint8_t value = static_cast<uint8_t>(
            in8(MASTER_DATA) & ~(1u << irq));
        out8(MASTER_DATA, value);
        return true;
    }

    const uint8_t slave_irq = static_cast<uint8_t>(irq - 8);
    out8(
        SLAVE_DATA,
        static_cast<uint8_t>(
            in8(SLAVE_DATA) & ~(1u << slave_irq)));
    out8(
        MASTER_DATA,
        static_cast<uint8_t>(in8(MASTER_DATA) & ~(1u << 2)));
    return true;
}

bool is_masked(uint8_t irq) {
    if (irq >= IRQ_COUNT) {
        return true;
    }

    const uint8_t mask_value =
        irq < 8 ? in8(MASTER_DATA) : in8(SLAVE_DATA);
    const uint8_t bit = static_cast<uint8_t>(
        irq < 8 ? irq : irq - 8);
    return (mask_value & (1u << bit)) != 0;
}

uint16_t current_mask() {
    return static_cast<uint16_t>(
        in8(MASTER_DATA) |
        (static_cast<uint16_t>(in8(SLAVE_DATA)) << 8));
}

bool begin_irq(uint8_t irq) {
    if (irq >= IRQ_COUNT) {
        return false;
    }

    if (!spurious_irq(irq)) {
        return true;
    }

    // A spurious slave IRQ still traversed the master's cascade input.
    if (irq == 15) {
        out8(MASTER_COMMAND, PIC_EOI);
    }
    return false;
}

void send_eoi(uint8_t irq) {
    if (irq >= IRQ_COUNT) {
        return;
    }

    if (irq >= 8) {
        out8(SLAVE_COMMAND, PIC_EOI);
    }
    out8(MASTER_COMMAND, PIC_EOI);
}

} // namespace drivers::pic
