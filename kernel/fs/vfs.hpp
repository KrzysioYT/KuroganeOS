#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fs::vfs {

constexpr size_t MAX_NAME_LENGTH = 63;
constexpr size_t MAX_PATH_LENGTH = 255;
constexpr size_t MAX_PATH_DEPTH = 32;
constexpr size_t MAX_MOUNTS = 16;
constexpr size_t MAX_OPEN_FILES = 64;

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    InvalidFlags,
    InvalidPath,
    PathTooLong,
    NameTooLong,
    PathTooDeep,
    NoRootFilesystem,
    NotFound,
    AlreadyExists,
    NotDirectory,
    IsDirectory,
    DirectoryNotEmpty,
    RootProtected,
    ReadOnly,
    PermissionDenied,
    Unsupported,
    NotSeekable,
    EndOfDirectory,
    BufferTooSmall,
    InvalidHandle,
    StaleHandle,
    MountNotFound,
    Busy,
    MountTableFull,
    OpenFileTableFull,
    CrossDevice,
    OutOfRange,
    ArithmeticOverflow,
    NoSpace,
    OutOfMemory,
    IoError,
    CorruptFilesystem,
    BackendFailure,
};

enum class NodeType : uint8_t {
    Regular = 0,
    Directory,
    Device,
    Pipe,
    MountPoint,
};

enum class NodeFlags : uint32_t {
    None = 0,
    Seekable = UINT32_C(1) << 0,
};

constexpr NodeFlags operator|(NodeFlags left, NodeFlags right) {
    return static_cast<NodeFlags>(
        static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr bool has_flag(NodeFlags flags, NodeFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

enum class OpenFlags : uint32_t {
    None = 0,
    Read = UINT32_C(1) << 0,
    Write = UINT32_C(1) << 1,
    Append = UINT32_C(1) << 2,
    Directory = UINT32_C(1) << 3,
};

constexpr OpenFlags operator|(OpenFlags left, OpenFlags right) {
    return static_cast<OpenFlags>(
        static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr OpenFlags operator&(OpenFlags left, OpenFlags right) {
    return static_cast<OpenFlags>(
        static_cast<uint32_t>(left) & static_cast<uint32_t>(right));
}

constexpr bool has_flag(OpenFlags flags, OpenFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

enum class SeekOrigin : uint8_t {
    Begin = 0,
    Current,
    End,
};

struct FileStat {
    NodeType type;
    NodeFlags flags;
    uint64_t size;
};

struct DirectoryEntry {
    char name[MAX_NAME_LENGTH + 1];
    size_t name_length;
    FileStat info;
};

// An opaque, trivially copyable token owned by a filesystem backend.
struct BackendFile {
    uintptr_t words[2];
};

struct Operations {
    Status (*stat_path)(void* context, const char* path, FileStat* info);
    Status (*open)(
        void* context,
        const char* path,
        OpenFlags flags,
        BackendFile* file);
    void (*close)(void* context, const BackendFile* file);
    Status (*read)(
        void* context,
        const BackendFile* file,
        uint64_t offset,
        void* buffer,
        size_t size,
        size_t* bytes_read);
    Status (*write)(
        void* context,
        const BackendFile* file,
        uint64_t offset,
        const void* buffer,
        size_t size,
        size_t* bytes_written);
    Status (*stat_open)(
        void* context,
        const BackendFile* file,
        FileStat* info);
    Status (*readdir)(
        void* context,
        const BackendFile* directory,
        uint64_t cookie,
        DirectoryEntry* entry,
        uint64_t* next_cookie);
    Status (*create)(void* context, const char* path);
    Status (*unlink)(void* context, const char* path);
    Status (*rename)(
        void* context,
        const char* source_path,
        const char* destination_path);
    Status (*mkdir)(void* context, const char* path);
    Status (*rmdir)(void* context, const char* path);
    Status (*sync)(void* context);
};

struct FileSystem {
    void* context;
    Operations operations;
    bool read_only;
};

struct MountHandle {
    uint16_t slot;
    uint32_t generation;
};

struct OpenFileHandle {
    uint16_t slot;
    uint32_t generation;
};

struct PathContext {
    // Both paths are canonical paths in the global VFS namespace. Callers
    // should modify them only through initialize_path_context, chdir/chroot.
    char root[MAX_PATH_LENGTH + 1];
    char cwd[MAX_PATH_LENGTH + 1];
};

struct Vnode {
    MountHandle mount;
    char backend_path[MAX_PATH_LENGTH + 1];
    FileStat info;
};

struct Mount {
    bool active;
    uint32_t generation;
    char path[MAX_PATH_LENGTH + 1];
    size_t path_length;
    FileSystem filesystem;
};

// OpenFile is VFS-owned state, indexed by a generation-checked handle. Its
// offset is also the directory cookie for directory handles.
struct OpenFile {
    bool active;
    uint32_t generation;
    uint32_t reference_count;
    OpenFlags flags;
    uint64_t offset;
    uint16_t mount_slot;
    uint32_t mount_generation;
    NodeType type;
    NodeFlags node_flags;
    BackendFile backend_file;
    char global_path[MAX_PATH_LENGTH + 1];
};

// All mutable VFS state is explicit and instance-local. External
// synchronization is required when one State can be used concurrently.
struct State {
    bool initialized;
    Mount mounts[MAX_MOUNTS];
    OpenFile open_files[MAX_OPEN_FILES];
};

// The root filesystem must expose stat_path and report "/" as a directory.
// initialize is transactional: on backend failure no initialized State is
// published. Backend contexts must outlive their mounts. Backend callbacks are
// synchronous, non-reentrant into this State, and failure-atomic for mutating
// operations.
Status initialize(State* state, const FileSystem* root_filesystem);

Status initialize_path_context(const State* state, PathContext* context);
Status chdir(
    State* state,
    PathContext* context,
    const char* path);
Status chroot(
    State* state,
    PathContext* context,
    const char* path);
Status getcwd(
    const PathContext* context,
    char* buffer,
    size_t capacity,
    size_t* required_size = nullptr);

Status lookup(
    State* state,
    const PathContext* context,
    const char* path,
    Vnode* vnode);
Status stat(
    State* state,
    const PathContext* context,
    const char* path,
    FileStat* info);

Status mount(
    State* state,
    const PathContext* context,
    const char* target,
    const FileSystem* filesystem,
    MountHandle* handle = nullptr);
Status unmount(State* state, MountHandle handle);

Status open(
    State* state,
    const PathContext* context,
    const char* path,
    OpenFlags flags,
    OpenFileHandle* handle);
Status retain(State* state, OpenFileHandle handle);
Status close(State* state, OpenFileHandle handle);
Status read(
    State* state,
    OpenFileHandle handle,
    void* buffer,
    size_t size,
    size_t* bytes_read = nullptr);
Status write(
    State* state,
    OpenFileHandle handle,
    const void* buffer,
    size_t size,
    size_t* bytes_written = nullptr);
Status seek(
    State* state,
    OpenFileHandle handle,
    int64_t offset,
    SeekOrigin origin,
    uint64_t* new_offset = nullptr);
Status readdir(
    State* state,
    OpenFileHandle handle,
    DirectoryEntry* entry);

Status create(
    State* state,
    const PathContext* context,
    const char* path);
Status unlink(
    State* state,
    const PathContext* context,
    const char* path);
Status rename(
    State* state,
    const PathContext* context,
    const char* source_path,
    const char* destination_path);
Status mkdir(
    State* state,
    const PathContext* context,
    const char* path);
Status rmdir(
    State* state,
    const PathContext* context,
    const char* path);
Status sync_all(State* state);

const char* status_message(Status status);

} // namespace fs::vfs
