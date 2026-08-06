#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fs {

// Public, deterministic limits. Operations fail before crossing them.
static constexpr size_t RAMFS_MAX_NAME_LENGTH = 63;
static constexpr size_t RAMFS_MAX_PATH_LENGTH = 255;
static constexpr size_t RAMFS_MAX_PATH_DEPTH = 32;
static constexpr size_t RAMFS_MAX_CHILDREN = 64;
static constexpr size_t RAMFS_MAX_NODES = 256;
static constexpr size_t RAMFS_MAX_FILE_SIZE = 64 * 1024;
static constexpr size_t RAMFS_MAX_TOTAL_FILE_BYTES = 1024 * 1024;

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    InvalidArgument,
    InvalidPath,
    PathTooLong,
    NameTooLong,
    PathTooDeep,
    NotFound,
    AlreadyExists,
    NotDirectory,
    IsDirectory,
    DirectoryNotEmpty,
    RootProtected,
    FileTooLarge,
    TooManyChildren,
    TooManyNodes,
    StorageLimitReached,
    OutOfMemory,
    BufferTooSmall,
    WouldCreateCycle,
    IterationStopped
};

enum class EntryType : uint8_t {
    File = 0,
    Directory
};

struct FileEntry {
    char name[RAMFS_MAX_NAME_LENGTH + 1];
    size_t name_length;
    char* content;
    size_t size;
    size_t capacity;
    bool is_directory;
    FileEntry** children;
    size_t child_count;
    size_t child_capacity;
    FileEntry* parent;
};

struct FileStat {
    EntryType type;
    size_t size;
    size_t child_count;
};

// Returning false from a callback stops iteration with IterationStopped.
typedef bool (*ListCallback)(const char* name, const FileStat* info, void* context);

Status initialize_ramfs();
Status create_file_at(const char* path, FileEntry** out_entry = nullptr);
Status create_directory_at(const char* path, FileEntry** out_entry = nullptr);
Status read_file_data(const char* path, void* buffer, size_t capacity, size_t* bytes_read);
Status write_file_data(
    const char* path,
    const void* data,
    size_t size,
    bool create_if_missing = true
);
Status stat_path(const char* path, FileStat* out_stat);
Status list_directory(const char* path, ListCallback callback, void* context);
Status remove_path(const char* path, bool recursive = false);
// Copies one regular file. The destination must not already exist. The new
// entry becomes visible only after its complete contents have been allocated.
Status copy_file(const char* source_path, const char* destination_path);
// Moves a file or directory to an unused destination path. This also performs
// an in-place rename when both paths have the same parent. Directory moves are
// rejected when they would create a cycle or violate path limits.
Status move_path(const char* source_path, const char* destination_path);
// Explicit spelling for callers that use the operation only as a rename.
Status rename_path(const char* source_path, const char* destination_path);
Status last_status();
const char* status_message(Status status);

// Compatibility API retained for existing kernel callers. Relative names are
// resolved from the RAMFS root; last_status() exposes failures.
void init_ramfs();
FileEntry* create_file(const char* name);
FileEntry* create_directory(const char* name);
const char* read_file(const char* name);
void write_file(const char* name, const char* content);
void list_files();

} // namespace fs
