#include "physical_memory.hpp"

namespace memory {

namespace {
constexpr size_t STATIC_BITMAP_BYTES = 128 * 1024;
alignas(uint64_t) static unsigned char
    g_static_bitmap[STATIC_BITMAP_BYTES];

static unsigned char* g_bitmap = nullptr;
static size_t g_bitmap_bytes = 0;
static uintptr_t g_base = 0;
static uintptr_t g_end = 0;
static size_t g_frame_count = 0;
static size_t g_frame_size = 4096;
static size_t g_used_frames = 0;
static size_t g_reserved_frames = 0;
static size_t g_bitmap_reserved_first = 0;
static size_t g_bitmap_reserved_count = 0;
static bool g_zero_frame_reserved = false;

bool is_power_of_two(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

void reset_state() {
    g_bitmap = nullptr;
    g_bitmap_bytes = 0;
    g_base = 0;
    g_end = 0;
    g_frame_count = 0;
    g_frame_size = 4096;
    g_used_frames = 0;
    g_reserved_frames = 0;
    g_bitmap_reserved_first = 0;
    g_bitmap_reserved_count = 0;
    g_zero_frame_reserved = false;
}

bool bitmap_bit(size_t frame_index) {
    return (g_bitmap[frame_index / 8] &
        static_cast<unsigned char>(1u << (frame_index % 8))) != 0;
}

void set_bitmap_bit(size_t frame_index) {
    g_bitmap[frame_index / 8] |=
        static_cast<unsigned char>(1u << (frame_index % 8));
}

void clear_bitmap_bit(size_t frame_index) {
    g_bitmap[frame_index / 8] &=
        static_cast<unsigned char>(
            ~(static_cast<unsigned char>(1u << (frame_index % 8))));
}

void reserve_frame(size_t frame_index) {
    if (!bitmap_bit(frame_index)) {
        set_bitmap_bit(frame_index);
        ++g_used_frames;
    }
}

bool is_reserved_frame(size_t frame_index) {
    if (g_zero_frame_reserved && frame_index == 0) {
        return true;
    }

    return g_bitmap_reserved_count != 0 &&
        frame_index >= g_bitmap_reserved_first &&
        frame_index - g_bitmap_reserved_first <
            g_bitmap_reserved_count;
}

bool initialize(
    uintptr_t base,
    size_t size,
    size_t frame_size,
    void* bitmap_storage,
    size_t bitmap_storage_size) {
    reset_state();

    if (bitmap_storage == nullptr ||
        frame_size == 0 ||
        !is_power_of_two(frame_size) ||
        frame_size > UINTPTR_MAX ||
        (base & static_cast<uintptr_t>(frame_size - 1)) != 0 ||
        size < frame_size) {
        return false;
    }

    const size_t frame_count = size / frame_size;
    if (frame_count == 0 || frame_count > SIZE_MAX - 7) {
        return false;
    }

    const size_t bitmap_bytes = (frame_count + 7) / 8;
    if (bitmap_storage_size < bitmap_bytes) {
        return false;
    }

    const size_t managed_size = frame_count * frame_size;
    if (managed_size > UINTPTR_MAX - base) {
        return false;
    }

    const uintptr_t bitmap_start =
        reinterpret_cast<uintptr_t>(bitmap_storage);
    if (bitmap_bytes > UINTPTR_MAX - bitmap_start) {
        return false;
    }
    const uintptr_t bitmap_end = bitmap_start + bitmap_bytes;

    unsigned char* bitmap =
        static_cast<unsigned char*>(bitmap_storage);
    for (size_t i = 0; i < bitmap_bytes; ++i) {
        bitmap[i] = 0;
    }

    g_bitmap = bitmap;
    g_bitmap_bytes = bitmap_bytes;
    g_base = base;
    g_end = base + managed_size;
    g_frame_count = frame_count;
    g_frame_size = frame_size;

    // If bitmap storage itself lies in the managed physical range, keep all
    // overlapping frames permanently reserved.
    const uintptr_t overlap_start =
        bitmap_start > g_base ? bitmap_start : g_base;
    const uintptr_t overlap_end =
        bitmap_end < g_end ? bitmap_end : g_end;
    if (overlap_start < overlap_end) {
        g_bitmap_reserved_first =
            static_cast<size_t>((overlap_start - g_base) / g_frame_size);
        const size_t last_frame =
            static_cast<size_t>(
                ((overlap_end - 1) - g_base) / g_frame_size);
        g_bitmap_reserved_count =
            last_frame - g_bitmap_reserved_first + 1;

        for (size_t i = 0; i < g_bitmap_reserved_count; ++i) {
            reserve_frame(g_bitmap_reserved_first + i);
        }
    }

    // A successful allocation must never be indistinguishable from nullptr.
    if (g_base == 0) {
        g_zero_frame_reserved = true;
        reserve_frame(0);
    }

    g_reserved_frames = g_bitmap_reserved_count;
    if (g_zero_frame_reserved &&
        !(g_bitmap_reserved_count != 0 &&
          g_bitmap_reserved_first == 0)) {
        ++g_reserved_frames;
    }

    return true;
}

bool frame_index_for_address(
    uintptr_t address,
    size_t& frame_index) {
    if (g_bitmap == nullptr ||
        address < g_base ||
        address >= g_end) {
        return false;
    }

    const uintptr_t offset = address - g_base;
    if (offset % g_frame_size != 0) {
        return false;
    }

    frame_index = static_cast<size_t>(offset / g_frame_size);
    return frame_index < g_frame_count;
}

} // namespace

void init_physical_memory(uintptr_t base, size_t size, size_t frame_size) {
    initialize(
        base,
        size,
        frame_size,
        g_static_bitmap,
        sizeof(g_static_bitmap));
}

bool init_physical_memory_with_bitmap(
    uintptr_t base,
    size_t size,
    size_t frame_size,
    void* bitmap_storage,
    size_t bitmap_storage_size) {
    return initialize(
        base,
        size,
        frame_size,
        bitmap_storage,
        bitmap_storage_size);
}

void* alloc_frame() {
    if (!g_bitmap) {
        return nullptr;
    }

    for (size_t i = 0; i < g_frame_count; ++i) {
        size_t byte_index = i / 8;
        size_t bit_index = i % 8;
        unsigned char bit = 1u << bit_index;

        if ((g_bitmap[byte_index] & bit) == 0) {
            g_bitmap[byte_index] |= bit;
            ++g_used_frames;
            const uintptr_t address =
                g_base + i * g_frame_size;
            return reinterpret_cast<void*>(address);
        }
    }

    return nullptr;
}

void free_frame(void* frame) {
    try_free_frame(frame);
}

bool try_free_frame(void* frame) {
    uintptr_t address = reinterpret_cast<uintptr_t>(frame);
    size_t frame_index = 0;
    if (!frame_index_for_address(address, frame_index) ||
        is_reserved_frame(frame_index) ||
        !bitmap_bit(frame_index)) {
        return false;
    }

    clear_bitmap_bit(frame_index);
    if (g_used_frames > 0) {
        --g_used_frames;
    }
    return true;
}

bool is_frame_allocated(void* frame) {
    size_t frame_index = 0;
    return frame_index_for_address(
        reinterpret_cast<uintptr_t>(frame),
        frame_index) &&
        bitmap_bit(frame_index);
}

bool physical_memory_initialized() {
    return g_bitmap != nullptr;
}

uintptr_t physical_memory_base() {
    return g_base;
}

size_t physical_frame_size() {
    return g_frame_size;
}

size_t total_frames() {
    return g_frame_count;
}

size_t used_frames() {
    return g_used_frames;
}

size_t free_frames() {
    return g_frame_count - g_used_frames;
}

size_t reserved_frames() {
    return g_reserved_frames;
}

size_t static_bitmap_capacity_frames() {
    return STATIC_BITMAP_BYTES * 8;
}

} // namespace memory
