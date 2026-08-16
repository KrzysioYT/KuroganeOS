#pragma once

#include "mouse_protocol.hpp"

#include <stddef.h>
#include <stdint.h>

namespace drivers::mouse {

constexpr size_t SAMPLE_BUFFER_CAPACITY = 128U;

bool initialize();
void shutdown();
bool initialized();
bool controller_configured();
bool wheel_enabled();
void handle_irq();
size_t poll();
void process_byte(uint8_t value);
bool try_read_sample(Sample* out_sample);
size_t pending_samples();
uint64_t dropped_samples();

} // namespace drivers::mouse
