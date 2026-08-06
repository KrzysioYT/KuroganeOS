#pragma once

#include <stdint.h>

namespace drivers::pic {

constexpr uint8_t MASTER_VECTOR_OFFSET = 0x20;
constexpr uint8_t SLAVE_VECTOR_OFFSET = 0x28;
constexpr uint8_t IRQ_COUNT = 16;

// Remaps the legacy 8259 pair and leaves every IRQ masked.
void initialize();
bool initialized();

void mask_all();
bool mask(uint8_t irq);
bool unmask(uint8_t irq);
bool is_masked(uint8_t irq);
uint16_t current_mask();

// Returns false for a spurious IRQ7/IRQ15. The IRQ15 path performs the
// mandatory master-only EOI itself.
bool begin_irq(uint8_t irq);
void send_eoi(uint8_t irq);

} // namespace drivers::pic
