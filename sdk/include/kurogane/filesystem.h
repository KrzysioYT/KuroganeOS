#ifndef KUROGANE_SDK_FILESYSTEM_H
#define KUROGANE_SDK_FILESYSTEM_H

#include <kurogane/syscall.h>

#define KU_FILE_NAME_CAPACITY 64U

typedef ku_handle_t ku_file_t;

enum ku_file_open_flags {
    KU_FILE_OPEN_READ = KU_OPEN_READ,
    KU_FILE_OPEN_WRITE = KU_OPEN_WRITE,
    KU_FILE_OPEN_APPEND = KU_OPEN_APPEND,
    KU_FILE_OPEN_DIRECTORY = KU_OPEN_DIRECTORY
};

enum ku_file_type {
    KU_FILE_TYPE_UNKNOWN = 0,
    KU_FILE_TYPE_REGULAR = 1,
    KU_FILE_TYPE_DIRECTORY = 2,
    KU_FILE_TYPE_DEVICE = 3,
    KU_FILE_TYPE_PIPE = 4,
    KU_FILE_TYPE_MOUNT_POINT = 5
};

enum ku_file_flags {
    KU_FILE_FLAG_NONE = 0,
    KU_FILE_FLAG_SEEKABLE = UINT32_C(1) << 0
};

enum ku_file_seek_origin {
    KU_FILE_SEEK_BEGIN = 0,
    KU_FILE_SEEK_CURRENT = 1,
    KU_FILE_SEEK_END = 2
};

typedef struct ku_file_stat {
    uint32_t structure_size;
    uint32_t type;
    uint64_t size;
    uint32_t flags;
    uint32_t reserved;
} ku_file_stat;

typedef struct ku_directory_entry {
    uint32_t structure_size;
    uint32_t type;
    uint64_t size;
    uint32_t flags;
    uint32_t name_length;
    char name[KU_FILE_NAME_CAPACITY];
} ku_directory_entry;

typedef struct ku_file_rename_request {
    uint32_t structure_size;
    uint32_t reserved;
    ku_string_view source;
    ku_string_view destination;
} ku_file_rename_request;

typedef struct ku_file_seek_request {
    uint32_t structure_size;
    uint32_t origin;
    ku_file_t file;
    int64_t offset;
    uint64_t new_offset;
    uint64_t reserved;
} ku_file_seek_request;

#if defined(__cplusplus)
static_assert(sizeof(ku_file_stat) == 24, "file stat ABI mismatch");
static_assert(sizeof(ku_directory_entry) == 88, "directory entry ABI mismatch");
static_assert(sizeof(ku_file_rename_request) == 40, "rename request ABI mismatch");
static_assert(sizeof(ku_file_seek_request) == 40, "seek request ABI mismatch");
#else
_Static_assert(sizeof(ku_file_stat) == 24, "file stat ABI mismatch");
_Static_assert(sizeof(ku_directory_entry) == 88, "directory entry ABI mismatch");
_Static_assert(sizeof(ku_file_rename_request) == 40, "rename request ABI mismatch");
_Static_assert(sizeof(ku_file_seek_request) == 40, "seek request ABI mismatch");
#endif

static inline ku_result_t ku_file_open_ex(
    const char* path, size_t size, uint64_t flags) {
    return ku_open(path, size, flags);
}

static inline ku_result_t ku_file_open(const char* path, size_t size) {
    return ku_file_open_ex(path, size, KU_FILE_OPEN_READ);
}

static inline ku_result_t ku_file_read(
    ku_file_t file, void* buffer, size_t size) {
    return ku_read(file, buffer, size);
}

static inline ku_result_t ku_file_write(
    ku_file_t file, const void* buffer, size_t size) {
    return ku_write(file, buffer, size);
}

static inline ku_status_t ku_file_close(ku_file_t file) {
    return ku_close(file);
}

static inline ku_status_t ku_file_stat_path(
    const char* path, size_t size, ku_file_stat* info) {
    if (info == NULL) return KU_STATUS_INVALID_ARGUMENT;
    info->structure_size = sizeof(*info);
    info->type = KU_FILE_TYPE_UNKNOWN;
    info->size = 0U;
    info->flags = KU_FILE_FLAG_NONE;
    info->reserved = 0U;
    return (ku_status_t)ku_syscall3(
        KU_SYS_FS_STAT,
        (uint64_t)(uintptr_t)path,
        (uint64_t)size,
        (uint64_t)(uintptr_t)info);
}

static inline ku_status_t ku_file_readdir(
    ku_file_t directory, ku_directory_entry* entry) {
    if (entry == NULL) return KU_STATUS_INVALID_ARGUMENT;
    entry->structure_size = sizeof(*entry);
    entry->type = KU_FILE_TYPE_UNKNOWN;
    entry->size = 0U;
    entry->flags = KU_FILE_FLAG_NONE;
    entry->name_length = 0U;
    entry->name[0] = '\0';
    return (ku_status_t)ku_syscall3(
        KU_SYS_FS_READDIR,
        directory,
        (uint64_t)(uintptr_t)entry,
        sizeof(*entry));
}

static inline ku_status_t ku_file_seek(
    ku_file_t file,
    int64_t offset,
    uint32_t origin,
    uint64_t* new_offset) {
    if (origin > KU_FILE_SEEK_END) return KU_STATUS_INVALID_ARGUMENT;
    ku_file_seek_request request;
    request.structure_size = sizeof(request);
    request.origin = origin;
    request.file = file;
    request.offset = offset;
    request.new_offset = 0U;
    request.reserved = 0U;
    const ku_status_t status = (ku_status_t)ku_syscall3(
        KU_SYS_FS_SEEK,
        (uint64_t)(uintptr_t)&request,
        sizeof(request),
        0U);
    if (status == KU_STATUS_OK && new_offset != NULL) {
        *new_offset = request.new_offset;
    }
    return status;
}

static inline ku_status_t ku_file_create(const char* path, size_t size) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_FS_CREATE,
        (uint64_t)(uintptr_t)path,
        (uint64_t)size,
        0U);
}

static inline ku_status_t ku_file_unlink(const char* path, size_t size) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_FS_UNLINK,
        (uint64_t)(uintptr_t)path,
        (uint64_t)size,
        0U);
}

static inline ku_status_t ku_file_rename(
    const char* source,
    size_t source_size,
    const char* destination,
    size_t destination_size) {
    ku_file_rename_request request;
    request.structure_size = sizeof(request);
    request.reserved = 0U;
    request.source.data = source;
    request.source.size = source_size;
    request.destination.data = destination;
    request.destination.size = destination_size;
    return (ku_status_t)ku_syscall3(
        KU_SYS_FS_RENAME,
        (uint64_t)(uintptr_t)&request,
        sizeof(request),
        0U);
}

static inline ku_status_t ku_file_mkdir(const char* path, size_t size) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_FS_MKDIR,
        (uint64_t)(uintptr_t)path,
        (uint64_t)size,
        0U);
}

static inline ku_status_t ku_file_rmdir(const char* path, size_t size) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_FS_RMDIR,
        (uint64_t)(uintptr_t)path,
        (uint64_t)size,
        0U);
}

static inline ku_status_t ku_file_sync(void) {
    return (ku_status_t)ku_syscall3(KU_SYS_FS_SYNC, 0U, 0U, 0U);
}

static inline ku_status_t ku_file_chdir(const char* path, size_t size) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_FS_CHDIR,
        (uint64_t)(uintptr_t)path,
        (uint64_t)size,
        0U);
}

/*
 * Gets the calling process working directory. Passing buffer=NULL/capacity=0
 * is allowed when required_size is non-NULL and can be used as a size query.
 * required_size includes the trailing NUL byte.
 */
static inline ku_status_t ku_file_getcwd(
    char* buffer,
    size_t capacity,
    size_t* required_size) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_FS_GETCWD,
        (uint64_t)(uintptr_t)buffer,
        (uint64_t)capacity,
        (uint64_t)(uintptr_t)required_size);
}

static inline ku_status_t ku_chdir(const char* path, size_t size) {
    return ku_file_chdir(path, size);
}

static inline ku_status_t ku_getcwd(
    char* buffer,
    size_t capacity,
    size_t* required_size) {
    return ku_file_getcwd(buffer, capacity, required_size);
}

#endif
