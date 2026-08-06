#pragma once

#include <stdint.h>

namespace drivers::pit {

constexpr uint32_t INPUT_FREQUENCY_HZ = 1193182;

// Programs channel 0 in square-wave mode, installs IRQ0 and unmasks it.
// Returns false when the requested rate cannot be represented by a 16-bit
// divisor or the IRQ handler cannot be registered.
bool initialize(uint32_t requested_hz);
void shutdown();
bool initialized();

uint32_t frequency_hz();
uint16_t divisor();
uint64_t ticks();
void reset_ticks();

// Registered with the central IRQ dispatcher; EOI is sent there.
void handle_irq();

} // namespace drivers::pit
