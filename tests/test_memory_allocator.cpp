#include "../kernel/memory/allocator.hpp"
#include "../kernel/memory/physical_memory.hpp"

namespace {

int test_kernel_heap() {
    alignas(64) unsigned char storage[32771];

    memory::init_kernel_heap(nullptr, sizeof(storage));
    if (memory::kernel_heap_initialized() ||
        memory::kmalloc(8) != nullptr) {
        return 1;
    }

    memory::init_kernel_heap(storage, 1);
    if (memory::kernel_heap_initialized()) {
        return 2;
    }

    // Deliberately use an unaligned beginning; init must align it safely.
    memory::init_kernel_heap(storage + 1, sizeof(storage) - 1);
    if (!memory::kernel_heap_initialized() ||
        memory::total_bytes() == 0 ||
        memory::used_bytes() != 0 ||
        memory::allocation_count() != 0) {
        return 3;
    }

    if (memory::kmalloc(0) != nullptr ||
        memory::kmalloc(8, 0) != nullptr ||
        memory::kmalloc(8, 3) != nullptr ||
        memory::kmalloc(SIZE_MAX, 16) != nullptr) {
        return 4;
    }

    void* first = memory::kmalloc(200, 16);
    void* second = memory::kmalloc(300, 64);
    void* third = memory::kmalloc(400, 8);

    if (!first || !second || !third ||
        first == second || second == third || first == third) {
        return 5;
    }

    if ((reinterpret_cast<uintptr_t>(first) % 16) != 0 ||
        (reinterpret_cast<uintptr_t>(second) % 64) != 0 ||
        (reinterpret_cast<uintptr_t>(third) % 8) != 0) {
        return 6;
    }

    if (memory::used_bytes() != 900 ||
        memory::allocation_count() != 3) {
        return 7;
    }

    memory::kfree(second);
    if (memory::used_bytes() != 600 ||
        memory::allocation_count() != 2) {
        return 8;
    }

    // Double-free and a foreign pointer must be harmless.
    memory::kfree(second);
    unsigned char foreign = 0;
    memory::kfree(&foreign);
    if (memory::used_bytes() != 600 ||
        memory::allocation_count() != 2) {
        return 9;
    }

    memory::kfree(first);
    if (memory::used_bytes() != 400 ||
        memory::allocation_count() != 1) {
        return 10;
    }

    // This request only fits after the adjacent first and second blocks have
    // coalesced.
    void* coalesced = memory::kmalloc(450, 64);
    if (!coalesced ||
        (reinterpret_cast<uintptr_t>(coalesced) % 64) != 0 ||
        memory::used_bytes() != 850 ||
        memory::allocation_count() != 2) {
        return 11;
    }

    memory::kfree(third);
    memory::kfree(coalesced);
    if (memory::used_bytes() != 0 ||
        memory::allocation_count() != 0) {
        return 12;
    }

    // Full coalescing should make a large allocation possible again.
    void* large = memory::kmalloc(20000, 256);
    if (!large ||
        (reinterpret_cast<uintptr_t>(large) % 256) != 0) {
        return 13;
    }
    memory::kfree(large);

    if (memory::used_bytes() != 0 ||
        memory::allocation_count() != 0 ||
        memory::free_bytes() != memory::total_bytes()) {
        return 14;
    }

    return 0;
}

int test_physical_memory() {
    constexpr size_t frame_size = 4096;
    constexpr size_t frame_count = 17;
    constexpr uintptr_t base = 0x100000000ULL;

    memory::init_physical_memory(base + 1, frame_count * frame_size, frame_size);
    if (memory::physical_memory_initialized()) {
        return 20;
    }

    memory::init_physical_memory(base, frame_count * frame_size, 3000);
    if (memory::physical_memory_initialized()) {
        return 21;
    }

    memory::init_physical_memory(base, frame_count * frame_size, frame_size);
    if (!memory::physical_memory_initialized() ||
        memory::physical_memory_base() != base ||
        memory::physical_frame_size() != frame_size ||
        memory::total_frames() != frame_count ||
        memory::used_frames() != 0 ||
        memory::free_frames() != frame_count ||
        memory::reserved_frames() != 0) {
        return 22;
    }

    void* frames[frame_count];
    for (size_t i = 0; i < frame_count; ++i) {
        frames[i] = memory::alloc_frame();
        if (reinterpret_cast<uintptr_t>(frames[i]) !=
                base + i * frame_size ||
            !memory::is_frame_allocated(frames[i])) {
            return 23;
        }
    }

    if (memory::alloc_frame() != nullptr ||
        memory::used_frames() != frame_count ||
        memory::free_frames() != 0) {
        return 24;
    }

    void* unaligned = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(frames[5]) + 1);
    void* outside = reinterpret_cast<void*>(base - frame_size);
    if (memory::try_free_frame(unaligned) ||
        memory::try_free_frame(outside) ||
        memory::used_frames() != frame_count) {
        return 25;
    }

    if (!memory::try_free_frame(frames[5]) ||
        memory::used_frames() != frame_count - 1 ||
        memory::try_free_frame(frames[5]) ||
        memory::used_frames() != frame_count - 1) {
        return 26;
    }

    void* reused = memory::alloc_frame();
    if (reused != frames[5] ||
        memory::used_frames() != frame_count) {
        return 27;
    }

    for (size_t i = 0; i < frame_count; ++i) {
        memory::free_frame(frames[i]);
    }
    if (memory::used_frames() != 0 ||
        memory::free_frames() != frame_count) {
        return 28;
    }

    // 17 frames need ceil(17 / 8) == 3 bitmap bytes. Sentinels prove that
    // initialization does not write beyond the required bytes.
    unsigned char external_bitmap[5] =
        {0xAA, 0xAA, 0xAA, 0x5A, 0xC3};
    if (memory::init_physical_memory_with_bitmap(
            base,
            frame_count * frame_size,
            frame_size,
            external_bitmap,
            2) ||
        memory::physical_memory_initialized()) {
        return 29;
    }

    if (!memory::init_physical_memory_with_bitmap(
            base,
            frame_count * frame_size,
            frame_size,
            external_bitmap,
            3) ||
        external_bitmap[3] != 0x5A ||
        external_bitmap[4] != 0xC3) {
        return 30;
    }

    // When the bitmap is inside its own managed range, its frames must never
    // be handed out or freed.
    unsigned char overlapping_bitmap[8];
    const uintptr_t overlapping_base =
        reinterpret_cast<uintptr_t>(overlapping_bitmap);
    if (!memory::init_physical_memory_with_bitmap(
            overlapping_base,
            sizeof(overlapping_bitmap),
            1,
            overlapping_bitmap,
            1) ||
        memory::reserved_frames() != 1 ||
        memory::used_frames() != 1 ||
        memory::alloc_frame() != overlapping_bitmap + 1 ||
        memory::try_free_frame(overlapping_bitmap)) {
        return 31;
    }

    // Physical frame zero is permanently reserved because nullptr denotes
    // allocation failure in the preserved API.
    memory::init_physical_memory(0, 3 * frame_size, frame_size);
    if (!memory::physical_memory_initialized() ||
        memory::reserved_frames() != 1 ||
        memory::used_frames() != 1 ||
        reinterpret_cast<uintptr_t>(memory::alloc_frame()) != frame_size ||
        memory::try_free_frame(nullptr)) {
        return 32;
    }

    return 0;
}

} // namespace

int main() {
    const int heap_result = test_kernel_heap();
    if (heap_result != 0) {
        return heap_result;
    }

    return test_physical_memory();
}
