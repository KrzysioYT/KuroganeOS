#pragma once

#include <stddef.h>
#include <stdint.h>

namespace memory {

// Initializes a single contiguous physical-memory range using an internal
// bitmap. Invalid arguments or a range too large for the internal bitmap leave
// the allocator uninitialized.
void init_physical_memory(uintptr_t base, size_t size, size_t frame_size);

// Scalable variant for boot-time code that owns bitmap storage. Returns false
// without retaining a partially initialized state when validation fails.
bool init_physical_memory_with_bitmap(
    uintptr_t base,
    size_t size,
    size_t frame_size,
    void* bitmap_storage,
    size_t bitmap_storage_size);

void* alloc_frame();
// Allocates one complete frame whose last byte is below exclusive_limit.
// This is intended for devices with bounded DMA address widths. A failure
// does not consume or temporarily reserve frames outside the requested range.
void* alloc_frame_below(uintptr_t exclusive_limit);
void free_frame(void* frame);
bool try_free_frame(void* frame);
bool is_frame_allocated(void* frame);

bool physical_memory_initialized();
uintptr_t physical_memory_base();
size_t physical_frame_size();
size_t total_frames();
size_t used_frames();
size_t free_frames();
size_t reserved_frames();
size_t static_bitmap_capacity_frames();

} // namespace memory
