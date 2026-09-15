#pragma once

#include <stddef.h>
#include <stdint.h>

namespace arch::x86_64::io_apic {

constexpr uint8_t FIRST_HARDWARE_VECTOR = 0x40U;
constexpr uint8_t LAST_HARDWARE_VECTOR = 0xEFU;
constexpr uint8_t RESERVED_SYSCALL_VECTOR = 0x80U;

enum class TriggerMode : uint8_t {
    Edge = 0,
    Level = 1,
};

enum class Polarity : uint8_t {
    ActiveHigh = 0,
    ActiveLow = 1,
};

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidVector,
    UnsupportedDeliveryMode,
    UnsupportedDestinationMode,
};

struct RedirectionSpan {
    uint32_t global_base;
    uint16_t entry_count;
};

struct Route {
    uint8_t vector;
    uint8_t destination_apic_id;
    TriggerMode trigger;
    Polarity polarity;
    bool masked;
};

bool span_valid(const RedirectionSpan& span);
bool spans_overlap(const RedirectionSpan& left, const RedirectionSpan& right);
bool validate_non_overlapping(const RedirectionSpan* spans, size_t count);

bool validate(const Route& route);
uint32_t encode_low(const Route& route);
uint32_t encode_high(const Route& route);
bool decode(uint32_t low, uint32_t high, Route* output);
const char* status_message(Status status);

} // namespace arch::x86_64::io_apic
