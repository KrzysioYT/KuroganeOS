#pragma once

#include <stddef.h>
#include <stdint.h>

#include "kurofs.hpp"
#include "vfs.hpp"

namespace fs::kurofs_vfs {

struct OpenSlot {
    bool active;
    uint32_t generation;
    kurofs::Inode inode;
};

struct Adapter {
    kurofs::FileSystem* filesystem;
    bool initialized;
    OpenSlot open_slots[vfs::MAX_OPEN_FILES];
};

// Expose an already mounted KuroFS instance through the common VFS contract.
// Regular-file writes and same-directory namespace mutations are backed by
// KuroFS revision-checked publication primitives.
vfs::Status initialize(
    Adapter* adapter,
    kurofs::FileSystem* filesystem,
    vfs::FileSystem* output_filesystem);

} // namespace fs::kurofs_vfs
