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
constexpr uint64_t FEATURE_MOVE_INTENT = UINT64_C(1) << 0U;
constexpr uint64_t FEATURE_INODE_OWNERSHIP = UINT64_C(1) << 1U;
constexpr uint32_t INODE_FLAG_PENDING = UINT32_C(1) << 0U;
constexpr uint32_t INODE_FLAG_ORPHAN = UINT32_C(1) << 1U;
constexpr uint32_t DIRECTORY_ENTRY_SIZE = 128U;
constexpr size_t MAX_DIRECTORY_NAME = 63U;

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
    InvalidInodeMetadata,
    InvalidExtent,
    OverlappingExtents,
    StaleInode,
    NotFound,
    AlreadyExists,
    NotDirectory,
    DirectoryNotEmpty,
    WouldCreateCycle,
    NameTooLong,
    CorruptDirectory,
    NoSpace,
    BlockDeviceError,
};

enum class InodeType : uint32_t {
    Regular = 1U,
    Directory = 2U,
};

enum class InodeOwnership : uint8_t {
    Free = 0,
    Pending,
    Live,
    Tombstoned,
    Orphan,
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

struct InodeOwnershipSummary {
    uint64_t free;
    uint64_t pending;
    uint64_t live;
    uint64_t tombstoned;
    uint64_t orphan;
};

struct FileSystem {
    const storage::block::Device* device;
    Geometry geometry;
    bool mounted;
};

struct DirectoryEntry {
    char name[MAX_DIRECTORY_NAME + 1U];
    size_t name_length;
    uint64_t inode_id;
    uint32_t inode_generation;
    InodeType type;
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
// incarnation generation and metadata revision it read; success preserves
// generation and increments revision, making stale writes deterministic.
// Any published extent must already be fully allocated in the bitmap.
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
// supplied inode snapshot must still match on-disk generation and revision.
Status read_inode_data(
    FileSystem* filesystem,
    const Inode* inode,
    uint64_t offset,
    void* destination,
    size_t capacity,
    size_t* out_read);

// Publication-atomic regular-file write. The complete resulting file is
// prepared in a replacement extent and made visible by one revision-checked
// inode publication; the previous extent is reclaimed only afterwards.
Status write_inode_data(
    FileSystem* filesystem,
    Inode* inode,
    uint64_t offset,
    const void* source,
    size_t size);

// Resize a regular file while preserving its current contents. Growth exposes
// zero-filled bytes and first tries to extend the contiguous extent in place.
// Shrink publishes the smaller inode before releasing trailing bitmap blocks,
// so interruption can leak space but cannot create duplicate ownership.
Status resize_inode(
    FileSystem* filesystem, Inode* inode, uint64_t new_size);

// Directory records are fixed-size, CRC-protected and append-only until
// transactional unlink is introduced. Copy-on-grow releases the previous
// extent only after publishing the replacement inode. Child inode generation
// is bound into each record so stale aliases become corruption, not access.
Status directory_entry_at(
    FileSystem* filesystem, const Inode* directory,
    uint64_t index, DirectoryEntry* output);
Status directory_lookup(
    FileSystem* filesystem, const Inode* directory,
    const char* name, DirectoryEntry* output);
Status directory_append(
    FileSystem* filesystem, Inode* directory,
    const char* name, uint64_t child_inode_id);
// Allocate an empty inode and attach it to one directory. If namespace
// publication fails before the parent revision advances, the new inode is
// retired before the original failure is returned.
Status directory_create(
    FileSystem* filesystem,
    Inode* directory,
    const char* name,
    InodeType type,
    Inode* out_child);
// Remove one name and retire its single-link inode. Directory contents are
// replaced copy-on-write before the child tombstone is published. Non-empty
// directories are refused until recursive removal is explicitly implemented.
Status directory_remove(
    FileSystem* filesystem, Inode* directory, const char* name);
// Rename one entry without changing child identity. This operation is scoped
// to a single parent and publishes a copy-on-write directory replacement.
Status directory_rename(
    FileSystem* filesystem,
    Inode* directory,
    const char* old_name,
    const char* new_name);
// Move one entry between distinct parents. Replacement directory images are
// durable before a redundant superblock intent is published. Mount either
// aborts an unpublished intent or completes a partially published move, so an
// interrupted operation never exposes a mounted namespace with zero or two
// owners for the child. Both caller snapshots advance on committed success.
Status directory_move(
    FileSystem* filesystem,
    Inode* source_directory,
    const char* source_name,
    Inode* destination_directory,
    const char* destination_name);

// Report durable inode ownership without reclaiming anything. Pending inodes
// are allocated but not yet attached. Live inodes have exactly one namespace
// parent; an orphan is the unattached root of a detached inode or subtree.
// Free and tombstoned slots remain distinct so generation history is visible.
Status inode_ownership(
    FileSystem* filesystem, uint64_t inode_id, InodeOwnership* output);
Status scan_inode_ownership(
    FileSystem* filesystem, InodeOwnershipSummary* output);

// Validate all allocated inode metadata, extent ownership and directory
// records. Unattached low-level reservations remain legal, but no two
// allocated inodes may own the same block and every published record must be
// generation-correct. mount() runs this check before exposing a filesystem.
Status validate_consistency(FileSystem* filesystem);

const char* status_message(Status status);

} // namespace fs::kurofs
