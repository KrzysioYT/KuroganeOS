#include "allocator.hpp"

namespace memory {

namespace {
static uint8_t* g_heap_start = nullptr;
static uintptr_t g_heap_end = 0;
static size_t g_heap_size = 0;
static BlockHeader* g_first_block = nullptr;
static size_t g_used_bytes = 0;
static size_t g_allocation_count = 0;

constexpr uintptr_t ALLOCATION_MAGIC =
    static_cast<uintptr_t>(0x4B55524F47414E45ULL); // "KUROGANE"

struct AllocationPrefix {
    uintptr_t magic;
    BlockHeader* block;
    size_t requested_size;
};

bool is_power_of_two(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

bool checked_add(uintptr_t value, size_t increment, uintptr_t& result) {
    if (increment > UINTPTR_MAX - value) {
        return false;
    }

    result = value + increment;
    return true;
}

bool align_up(uintptr_t value, size_t alignment, uintptr_t& result) {
    if (!is_power_of_two(alignment)) {
        return false;
    }

    const uintptr_t mask = static_cast<uintptr_t>(alignment - 1);
    if (value > UINTPTR_MAX - mask) {
        return false;
    }

    result = (value + mask) & ~mask;
    return true;
}

void reset_heap_state() {
    g_heap_start = nullptr;
    g_heap_end = 0;
    g_heap_size = 0;
    g_first_block = nullptr;
    g_used_bytes = 0;
    g_allocation_count = 0;
}

uintptr_t block_payload_start(const BlockHeader* block) {
    return reinterpret_cast<uintptr_t>(block) + sizeof(BlockHeader);
}

bool block_is_known(const BlockHeader* target) {
    const BlockHeader* current = g_first_block;
    while (current != nullptr) {
        if (current == target) {
            return true;
        }
        current = current->next;
    }
    return false;
}

bool calculate_layout(
    BlockHeader* block,
    size_t requested_size,
    size_t alignment,
    uintptr_t& user_address,
    size_t& consumed_payload) {
    const size_t effective_alignment =
        alignment < alignof(AllocationPrefix)
            ? alignof(AllocationPrefix)
            : alignment;

    const uintptr_t payload_start = block_payload_start(block);
    uintptr_t prefix_end = 0;
    if (!checked_add(payload_start, sizeof(AllocationPrefix), prefix_end) ||
        !align_up(prefix_end, effective_alignment, user_address)) {
        return false;
    }

    uintptr_t requested_end = 0;
    if (!checked_add(user_address, requested_size, requested_end)) {
        return false;
    }

    uintptr_t block_end = 0;
    if (!checked_add(payload_start, block->size, block_end) ||
        requested_end > block_end) {
        return false;
    }

    uintptr_t split_address = 0;
    if (align_up(requested_end, alignof(BlockHeader), split_address) &&
        split_address <= block_end) {
        consumed_payload = static_cast<size_t>(split_address - payload_start);
    } else {
        // The request fits, but alignment of a following header does not.
        // Consume the tail instead of creating an out-of-bounds header.
        consumed_payload = block->size;
    }

    return true;
}

BlockHeader* find_free_block(
    size_t size,
    size_t alignment,
    uintptr_t& user_address,
    size_t& consumed_payload) {
    BlockHeader* current = g_first_block;
    while (current != nullptr) {
        if (!current->used &&
            calculate_layout(
                current,
                size,
                alignment,
                user_address,
                consumed_payload)) {
            return current;
        }
        current = current->next;
    }
    return nullptr;
}

bool blocks_are_adjacent(const BlockHeader* first, const BlockHeader* second) {
    uintptr_t first_end = 0;
    return first != nullptr &&
        second != nullptr &&
        checked_add(block_payload_start(first), first->size, first_end) &&
        first_end == reinterpret_cast<uintptr_t>(second);
}

void merge_with_next(BlockHeader* block) {
    BlockHeader* next = block->next;
    if (next == nullptr || next->used || !blocks_are_adjacent(block, next)) {
        return;
    }

    block->size += sizeof(BlockHeader) + next->size;
    block->next = next->next;
    if (block->next != nullptr) {
        block->next->previous = block;
    }
}

} // namespace

void init_kernel_heap(void* start, size_t size) {
    reset_heap_state();

    if (start == nullptr || size == 0) {
        return;
    }

    const uintptr_t original_start = reinterpret_cast<uintptr_t>(start);
    uintptr_t original_end = 0;
    uintptr_t aligned_start = 0;
    if (!checked_add(original_start, size, original_end) ||
        !align_up(original_start, alignof(BlockHeader), aligned_start) ||
        aligned_start >= original_end) {
        return;
    }

    const size_t adjusted_size =
        static_cast<size_t>(original_end - aligned_start);
    const size_t minimum_size =
        sizeof(BlockHeader) + sizeof(AllocationPrefix) + 1;
    if (adjusted_size < minimum_size) {
        return;
    }

    g_heap_start = reinterpret_cast<uint8_t*>(aligned_start);
    g_heap_end = original_end;
    g_heap_size = adjusted_size;
    g_first_block = reinterpret_cast<BlockHeader*>(g_heap_start);
    g_first_block->size = adjusted_size - sizeof(BlockHeader);
    g_first_block->used = false;
    g_first_block->next = nullptr;
    g_first_block->previous = nullptr;
}

void* kmalloc(size_t size, size_t alignment) {
    if (!g_heap_start ||
        size == 0 ||
        !is_power_of_two(alignment)) {
        return nullptr;
    }

    uintptr_t user_address = 0;
    size_t consumed_payload = 0;
    BlockHeader* block =
        find_free_block(size, alignment, user_address, consumed_payload);
    if (!block) {
        return nullptr;
    }

    const size_t original_size = block->size;
    const size_t remainder = original_size - consumed_payload;
    const size_t minimum_free_payload = sizeof(AllocationPrefix) + 1;
    if (remainder >= sizeof(BlockHeader) + minimum_free_payload) {
        const uintptr_t split_address =
            block_payload_start(block) + consumed_payload;
        BlockHeader* split =
            reinterpret_cast<BlockHeader*>(split_address);
        split->size = remainder - sizeof(BlockHeader);
        split->used = false;
        split->next = block->next;
        split->previous = block;
        if (split->next != nullptr) {
            split->next->previous = split;
        }

        block->size = consumed_payload;
        block->next = split;
    }

    AllocationPrefix* prefix = reinterpret_cast<AllocationPrefix*>(
        user_address - sizeof(AllocationPrefix));
    prefix->magic = ALLOCATION_MAGIC;
    prefix->block = block;
    prefix->requested_size = size;

    block->used = true;
    g_used_bytes += size;
    ++g_allocation_count;
    return reinterpret_cast<void*>(user_address);
}

void kfree(void* ptr) {
    if (!ptr || !g_heap_start) {
        return;
    }

    const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t heap_start = reinterpret_cast<uintptr_t>(g_heap_start);
    if (address < heap_start + sizeof(BlockHeader) + sizeof(AllocationPrefix) ||
        address >= g_heap_end ||
        address < sizeof(AllocationPrefix)) {
        return;
    }

    const uintptr_t prefix_address =
        address - sizeof(AllocationPrefix);
    if ((prefix_address & (alignof(AllocationPrefix) - 1)) != 0) {
        return;
    }

    AllocationPrefix* prefix =
        reinterpret_cast<AllocationPrefix*>(prefix_address);
    BlockHeader* block = prefix->block;
    if (prefix->magic != ALLOCATION_MAGIC ||
        block == nullptr ||
        !block_is_known(block) ||
        !block->used) {
        return;
    }

    const uintptr_t payload_start = block_payload_start(block);
    uintptr_t payload_end = 0;
    uintptr_t allocation_end = 0;
    if (!checked_add(payload_start, block->size, payload_end) ||
        !checked_add(address, prefix->requested_size, allocation_end) ||
        prefix_address < payload_start ||
        allocation_end > payload_end) {
        return;
    }

    const size_t released_size = prefix->requested_size;
    prefix->magic = 0;
    prefix->block = nullptr;
    prefix->requested_size = 0;

    block->used = false;
    if (released_size <= g_used_bytes) {
        g_used_bytes -= released_size;
    } else {
        g_used_bytes = 0;
    }
    if (g_allocation_count > 0) {
        --g_allocation_count;
    }

    merge_with_next(block);
    if (block->previous != nullptr && !block->previous->used) {
        block = block->previous;
        merge_with_next(block);
    }
}

bool kernel_heap_initialized() {
    return g_first_block != nullptr;
}

size_t total_bytes() {
    return g_heap_size > sizeof(BlockHeader)
        ? g_heap_size - sizeof(BlockHeader)
        : 0;
}

size_t used_bytes() {
    return g_used_bytes;
}

size_t free_bytes() {
    size_t result = 0;
    BlockHeader* current = g_first_block;
    while (current != nullptr) {
        if (!current->used) {
            result += current->size;
        }
        current = current->next;
    }
    return result;
}

size_t allocation_count() {
    return g_allocation_count;
}

} // namespace memory
