#pragma once

#include <stddef.h>
#include <stdint.h>

uint64_t k_fnv1a64(const void* data, size_t size);
