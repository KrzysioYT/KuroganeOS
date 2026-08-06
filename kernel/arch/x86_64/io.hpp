#pragma once

#include <stdint.h>

namespace arch {

inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline void out16(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline uint16_t in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void io_wait() {
    out8(0x80, 0);
}

inline void disable_interrupts() {
    __asm__ volatile("cli" : : : "memory");
}

inline void enable_interrupts() {
    __asm__ volatile("sti" : : : "memory");
}

inline void halt() {
    __asm__ volatile("hlt");
}

inline void pause() {
    __asm__ volatile("pause");
}

inline uint64_t read_flags() {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return flags;
}

inline uint16_t read_cs() {
    uint16_t selector;
    __asm__ volatile("mov %%cs, %0" : "=r"(selector));
    return selector;
}

inline uint64_t read_tsc() {
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (static_cast<uint64_t>(high) << 32) | low;
}

} // namespace arch
