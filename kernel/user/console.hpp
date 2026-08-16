#pragma once

#include <stddef.h>
#include <stdint.h>

namespace user::console {

constexpr size_t INPUT_CAPACITY = 256U;

void initialize();
void shutdown();
bool active();
bool push(char character);
bool try_read(char* character);
size_t pending();
uint64_t dropped();

} // namespace user::console
