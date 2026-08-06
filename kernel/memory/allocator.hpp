#pragma once

#include <stddef.h>
#include <stdint.h>

namespace memory {

struct BlockHeader {
    size_t size;
    bool used;
    BlockHeader* next;
    BlockHeader* previous;
};

void init_kernel_heap(void* start, size_t size);
void* kmalloc(size_t size, size_t alignment = 16);
void kfree(void* ptr);

bool kernel_heap_initialized();
size_t total_bytes();
size_t used_bytes();
size_t free_bytes();
size_t allocation_count();

} // namespace memory
