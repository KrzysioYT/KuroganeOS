#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../storage/block_device.hpp"

namespace fs::fat32 {

// The backend uses caller-owned or fixed-size storage and supports a bounded
// ASCII 8.3 mutation subset. Existing LFN entries remain readable; creating
// names that require a new LFN is rejected instead of silently mangling them.
static constexpr size_t MAX_NAME_LENGTH = 255U;
static constexpr size_t MAX_PATH_LENGTH = 255U;
static constexpr size_t MAX_PATH_DEPTH = 32U;
static constexpr uint32_t SUPPORTED_SECTOR_SIZE = 512U;

enum class Status : uint8_t {
    Ok = 0,
    NotMounted,
    InvalidArgument,
    InvalidPath,
    PathTooLong,
    NameTooLong,
    PathTooDeep,
    PathEscapesRoot,
    NotFound,
    NotDirectory,
    IsDirectory,
    EndOfDirectory,
    UnsupportedSectorSize,
    UnsupportedGeometry,
    InvalidBootSector,
    CorruptFsInfo,
    CorruptBackupBoot,
    FatMirrorMismatch,
    CorruptFat,
    CorruptDirectory,
    CorruptChain,
    ChainCycle,
    TruncatedChain,
    UnsupportedNameEncoding,
    ArithmeticOverflow,
    BlockDeviceError,
    ReadOnly,
    AlreadyExists,
    DirectoryNotEmpty,
    NoSpace,
    Unsupported
};

enum class EntryType : uint8_t {
    File = 0,
    Directory
};

struct Geometry {
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t sectors_per_fat;
    uint64_t total_sectors;
    uint64_t first_fat_sector;
    uint64_t first_data_sector;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint32_t max_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    bool fat_mirroring;
    uint8_t active_fat;
};

struct FileSystem {
    const storage::block::Device* device;
    Geometry geometry;
    char volume_label[12];
    uint32_t allocation_hint;
    bool fs_info_hints_invalidated;
    bool is_mounted;
};

struct Node {
    EntryType type;
    uint8_t attributes;
    uint32_t first_cluster;
    uint64_t size;
};

struct Stat {
    EntryType type;
    uint8_t attributes;
    uint32_t first_cluster;
    uint64_t size;
};

struct DirectoryEntry {
    char name[MAX_NAME_LENGTH + 1U];
    size_t name_length;
    Stat info;
};

// Creates an empty, mirrored FAT32 volume on an already bounded block device.
// The operation writes only filesystem metadata and the root cluster; callers
// are responsible for partitioning and for ensuring destructive authorization.
Status format(
    const storage::block::Device* device,
    const char* volume_label,
    uint8_t sectors_per_cluster,
    uint32_t hidden_sectors = 0U);

// Transactional: a failed mount never changes *output.  Version 1 accepts
// only 512-byte logical sectors and validates the primary BPB, FSInfo, backup
// boot geometry, FAT32 range, and all enabled FAT mirrors before publishing
// the mount.
Status mount(FileSystem* output, const storage::block::Device* device);

bool mounted(const FileSystem* filesystem);
Status get_geometry(const FileSystem* filesystem, Geometry* output);
const char* volume_label(const FileSystem* filesystem);

// Paths are rooted at the mounted filesystem.  Relative spelling is accepted
// as root-relative; '.', repeated '/', and '..' are canonicalized lexically.
// Attempting to cross above root is an error.  Lookup is ASCII
// case-insensitive and never invents/mangles an on-disk name.
Status lookup(
    FileSystem* filesystem,
    const char* path,
    Node* output);
Status stat(
    FileSystem* filesystem,
    const char* path,
    Stat* output);

// Successful reads may be short only at EOF.  A chain ending before the
// directory entry's declared size is TruncatedChain.  Cycles, bad/reserved/
// free clusters, mirror divergence, and backend failures are reported rather
// than treated as EOF.
Status read(
    FileSystem* filesystem,
    const char* path,
    uint64_t offset,
    void* destination,
    size_t capacity,
    size_t* bytes_read);
Status read_node(
    FileSystem* filesystem,
    const Node* node,
    uint64_t offset,
    void* destination,
    size_t capacity,
    size_t* bytes_read);

// cookie is a zero-based visible-entry index and is advanced after a returned
// entry.  Malformed LFN chains are discarded and the valid 8.3 alias is
// returned.  Structurally valid LFNs containing non-ASCII UTF-16 are explicit:
// UnsupportedNameEncoding is returned and the cookie advances past that
// entry, so callers can continue without an infinite retry.
Status readdir(
    FileSystem* filesystem,
    const char* directory_path,
    uint64_t* cookie,
    DirectoryEntry* output);

// Mutations are synchronous at the block layer. sync() issues the device
// flush command so acknowledged metadata and file data survive a reboot.
Status sync(FileSystem* filesystem);
Status create(FileSystem* filesystem, const char* path);
Status write(
    FileSystem* filesystem,
    const char* path,
    uint64_t offset,
    const void* source,
    size_t size);
Status unlink(FileSystem* filesystem, const char* path);
Status rename(
    FileSystem* filesystem,
    const char* source_path,
    const char* destination_path);
Status mkdir(FileSystem* filesystem, const char* path);
Status rmdir(FileSystem* filesystem, const char* path);

const char* status_message(Status status);

} // namespace fs::fat32
