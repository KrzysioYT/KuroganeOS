#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../memory/virtual_memory.hpp"

namespace user::elf {

constexpr uint64_t USER_REGION_BASE = UINT64_C(0x0000400000000000);
constexpr uint64_t USER_REGION_SIZE = UINT64_C(64) * 1024U * 1024U;
constexpr uint64_t USER_REGION_END = USER_REGION_BASE + USER_REGION_SIZE;
constexpr size_t MAX_IMAGE_PAGES = 256U;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    ImageTooSmall,
    InvalidMagic,
    UnsupportedClass,
    UnsupportedEncoding,
    UnsupportedType,
    UnsupportedMachine,
    InvalidHeader,
    InvalidProgramTable,
    UnsupportedProgram,
    InvalidSegment,
    SegmentOutOfRange,
    SegmentOverlap,
    WritableExecutableSegment,
    InvalidEntry,
    TooManyPages,
    OutOfMemory,
    MappingFailed,
    UnmapFailed
};

struct Page {
    uint64_t virtual_address;
    void* physical_frame;
};

struct Image {
    memory::virtual_memory::AddressSpace* address_space;
    uint64_t entry;
    Page pages[MAX_IMAGE_PAGES];
    size_t page_count;
    bool loaded;
};

// Performs all structural and range validation without allocating memory.
Status validate(const void* bytes, size_t size, uint64_t* entry = nullptr);

// Maps PT_LOAD segments into address_space with exact user/write/NX flags.
// The image bytes are copied into freshly zeroed PMM frames.
Status load(
    const void* bytes,
    size_t size,
    memory::virtual_memory::AddressSpace* address_space,
    Image* output);

// Adds one zero-filled or caller-initialized page owned by image. Used for
// guarded stacks and the kernel-provided fault-exit trampoline.
Status map_anonymous_page(
    Image* image,
    uint64_t virtual_address,
    memory::virtual_memory::MapFlags flags,
    const void* initial_data = nullptr,
    size_t initial_size = 0U);

// Removes exactly one page previously owned by Image. The page must have
// been created by this loader; arbitrary mappings cannot be released here.
Status unmap_owned_page(Image* image, uint64_t virtual_address);

Status unload(Image* image);
const char* status_message(Status status);

} // namespace user::elf
