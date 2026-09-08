#pragma once

#include <stddef.h>
#include <stdint.h>

namespace arch::x86_64::hardware_vectors {

// Vectors 0x20-0x2f remain owned by the legacy PIC during migration. 0x80 is
// the public Ring-3 syscall gate and can never be leased to hardware.
constexpr uint8_t FIRST_VECTOR = 0x40U;
constexpr uint8_t LAST_VECTOR = 0xEFU;
constexpr uint8_t RESERVED_SYSCALL_VECTOR = 0x80U;
constexpr size_t VECTOR_CAPACITY =
    static_cast<size_t>(LAST_VECTOR - FIRST_VECTOR + 1U) - 1U;

struct Lease {
    uint8_t vector;
    uint64_t generation;
};

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    NotInitialized,
    Exhausted,
    StaleLease,
};

// initialize() is a boot-time operation. Interrupt setup calls it while
// interrupts are disabled, before any driver can own a vector.
void initialize();
bool initialized();

// Allocation is bounded and allocation-free. The generation changes before a
// released vector can be reused, so an old owner cannot free a later lease.
Status allocate(Lease* output);
Status release(const Lease& lease);
bool owns(const Lease& lease);
bool claimed(uint8_t vector);
bool is_allocatable(uint8_t vector);

size_t capacity();
size_t allocated_count();
const char* status_name(Status status);

} // namespace arch::x86_64::hardware_vectors
