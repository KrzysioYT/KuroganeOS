#ifndef KUROGANE_SDK_MEMORY_H
#define KUROGANE_SDK_MEMORY_H

#include <kurogane/syscall.h>

static inline void* ku_memory_allocate(size_t size) { return ku_alloc(size); }
static inline ku_status_t ku_memory_release(void* memory) {
    return ku_free(memory);
}

#endif
