#pragma once

#include <stdint.h>

namespace storage::dma {

constexpr uint64_t DMA32_ADDRESS_LIMIT = UINT64_C(0x100000000);

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    PhysicalMemoryUnavailable,
    InvalidFrameSize,
    OutOfMemory,
    AddressNotSupported,
    ReleaseFailed
};

struct Page {
    void* virtual_address;
    uint64_t physical_address;
    bool allocated;
};

// Allocates exactly one 4-KiB physical frame. On the current kernel paging
// backend the PMM's physical range remains identity mapped, but callers must
// use physical_address for hardware descriptors rather than deriving it from
// virtual_address. If supports_64_bit_addressing is false, the entire frame is
// guaranteed to reside below 4 GiB.
Status allocate_page(bool supports_64_bit_addressing, Page* output);
Status release_page(Page* page);

const char* status_message(Status status);

} // namespace storage::dma
