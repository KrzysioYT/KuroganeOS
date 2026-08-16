#include "dma.hpp"

#include "../memory/physical_memory.hpp"
#include "../memory/virtual_memory.hpp"

namespace storage::dma {

Status allocate_page(bool supports_64_bit_addressing, Page* output) {
    if (output == nullptr) {
        return Status::InvalidArgument;
    }
    *output = {};
    if (!memory::physical_memory_initialized()) {
        return Status::PhysicalMemoryUnavailable;
    }
    if (memory::physical_frame_size() !=
        static_cast<size_t>(memory::virtual_memory::PAGE_SIZE)) {
        return Status::InvalidFrameSize;
    }

    void* const frame = supports_64_bit_addressing
        ? memory::alloc_frame()
        : memory::alloc_frame_below(
              static_cast<uintptr_t>(DMA32_ADDRESS_LIMIT));
    if (frame == nullptr) {
        return supports_64_bit_addressing
            ? Status::OutOfMemory
            : Status::AddressNotSupported;
    }

    const uintptr_t address = reinterpret_cast<uintptr_t>(frame);
    if (!supports_64_bit_addressing &&
        (address >= DMA32_ADDRESS_LIMIT ||
         memory::virtual_memory::PAGE_SIZE >
             DMA32_ADDRESS_LIMIT - static_cast<uint64_t>(address))) {
        static_cast<void>(memory::try_free_frame(frame));
        return Status::AddressNotSupported;
    }

    output->virtual_address = frame;
    output->physical_address = static_cast<uint64_t>(address);
    output->allocated = true;
    return Status::Ok;
}

Status release_page(Page* page) {
    if (page == nullptr || !page->allocated ||
        page->virtual_address == nullptr ||
        page->physical_address != static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(page->virtual_address))) {
        return Status::InvalidArgument;
    }
    if (!memory::try_free_frame(page->virtual_address)) {
        return Status::ReleaseFailed;
    }
    *page = {};
    return Status::Ok;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok:
            return "ok";
        case Status::InvalidArgument:
            return "invalid DMA page argument";
        case Status::PhysicalMemoryUnavailable:
            return "physical memory manager unavailable";
        case Status::InvalidFrameSize:
            return "physical frame size is not 4 KiB";
        case Status::OutOfMemory:
            return "no free DMA frame";
        case Status::AddressNotSupported:
            return "no DMA frame in the controller address range";
        case Status::ReleaseFailed:
            return "DMA frame release failed";
    }
    return "unknown DMA status";
}

} // namespace storage::dma
