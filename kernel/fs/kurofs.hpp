#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../storage/block_device.hpp"

namespace fs::kurofs {

constexpr uint32_t FORMAT_VERSION = 1U;
constexpr uint32_t SUPPORTED_SECTOR_SIZE = 512U;
constexpr uint32_t INODE_SIZE = 128U;
constexpr uint64_t ROOT_INODE = 1U;
constexpr uint32_t DEFAULT_INODE_COUNT = 1024U;
constexpr uint64_t FEATURE_NONE = 0U;

enum class Status : uint8_t {
    Ok = 0,
    NotMounted,
    InvalidArgument,
    UnsupportedSectorSize,
    DeviceTooSmall,
    ArithmeticOverflow,
    InvalidSuperblock,
    CorruptSuperblock,
    InvalidGeometry,
    InvalidRootInode,
    InvalidExtent,
    StaleInode,
    NoSpace,
    BlockDeviceError,
};

enum class InodeType : uint32_t {
    Regular = 1U,
    Directory = 2U,
};

struct Geometry {
    uint32_t sector_size;
    uint64_t total_blocks;
    uint64_t generation;
    uint32_t inode_count;
    uint32_t inode_size;
    uint64_t inode_table_start;
    uint64_t inode_table_blocks;
    uint64_t allocation_bitmap_start;
    uint64_t allocation_bitmap_blocks;
    uint64_t data_start;
    uint64_t root_inode;
    uint64_t feature_flags;
};

struct Inode {
    uint64_t id;
    InodeType type;
    uint32_t flags;
    uint64_t size;
    uint64_t extent_start;
    uint64_t extent_blocks;
    uint32_t link_count;
    // Stable incarnation identity. Future inode-slot reuse must advance this.
    uint32_t generation;
    // Optimistic metadata revision; every successful update_inode advances it.
    uint32_t revision;
};

struct FileSystem {
    const storage::block::Device* device;
    Geometry geometry;
    bool mounted;
};

// Formats the complete bounded block device as KuroFS v1. Metadata publication
// is ordered so superblocks are written only after the inode table and bitmap.
Status format(
    const storage::block::Device* device,
    uint32_t inode_count = DEFAULT_INODE_COUNT);

// Mount accepts either redundant superblock. When both are valid it selects the
// highest generation and rejects same-generation geometry disagreement.
Status mount(FileSystem* output, const storage::block::Device* device);

bool is_mounted(const FileSystem* filesystem);
Status get_geometry(const FileSystem* filesystem, Geometry* output);
Status read_inode(FileSystem* filesystem, uint64_t inode_id, Inode* output);

// Reserve one persistent contiguous data extent. Metadata blocks are never
// considered candidates. Successful bitmap publication is flushed before
// returning; an I/O failure may conservatively leak reserved blocks but can
// never hand the same block to two successful callers.
Status allocate_blocks(
    FileSystem* filesystem, uint64_t block_count, uint64_t* out_first_block);

// Allocate and persist an empty non-root inode. This intentionally does not
// attach data blocks yet: later file creation can reserve+flush its extent
// first and publish the inode only after data ownership is durable.
Status allocate_inode(
    FileSystem* filesystem, InodeType type, uint64_t* out_inode_id);

// Persist an update to an already allocated inode. The caller supplies the
// generation it read; success increments that generation, making stale
// snapshots fail deterministically. Any published extent must already be
// fully allocated in the persistent bitmap.
Status update_inode(FileSystem* filesystem, Inode* inode);

// Write bytes into an already allocated extent. This is deliberately lower
// level than inode publication: callers can make data durable first and only
// then publish size/ownership through update_inode().
Status write_extent_data(
    FileSystem* filesystem,
    uint64_t first_block,
    uint64_t block_count,
    uint64_t offset,
    const void* source,
    size_t size);

// Read at most the persisted inode size. EOF is reported as zero bytes. The
// supplied inode snapshot must still match the on-disk generation.
Status read_inode_data(
    FileSystem* filesystem,
    const Inode* inode,
    uint64_t offset,
    void* destination,
    size_t capacity,
    size_t* out_read);

const char* status_message(Status status);

} // namespace fs::kurofs
