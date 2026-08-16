#pragma once

#include <stddef.h>
#include <stdint.h>

#include "fat32.hpp"
#include "vfs.hpp"

namespace fs::fat32_vfs {

// The adapter owns only open-handle bookkeeping. The FAT32 mount and its
// block device must outlive the adapter. All storage is fixed-size so opening
// a file never allocates from the kernel heap.
struct OpenSlot {
    bool active;
    uint32_t generation;
    fat32::Node node;
    char path[vfs::MAX_PATH_LENGTH + 1U];
};

struct Adapter {
    fat32::FileSystem* filesystem;
    bool initialized;
    OpenSlot open_slots[vfs::MAX_OPEN_FILES];
};

// Produces a writable VFS backend over an already mounted FAT32 filesystem.
// The operation is transactional with respect to output_filesystem.
vfs::Status initialize(
    Adapter* adapter,
    fat32::FileSystem* filesystem,
    vfs::FileSystem* output_filesystem);

} // namespace fs::fat32_vfs
