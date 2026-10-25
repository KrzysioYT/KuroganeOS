#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../storage/block_device.hpp"
#include "../storage/gpt.hpp"
#include "vfs.hpp"

namespace fs::root_volume {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyAttempted,
    InvalidArgument,
    RootPartitionNotFound,
    InvalidPartitionRange,
    PartitionInitializationFailed,
    Fat32MountFailed,
    VfsAdapterFailed,
    VfsInitializationFailed,
    RootConfigurationMissing,
    RootConfigurationReadFailed,
    RootConfigurationInvalid,
    LivePackageInvalid,
};

Status initialize(
    const storage::block::Device* disk,
    const storage::gpt::Table* table);
Status initialize_live_package(const void* package_bytes, size_t package_size);

bool initialization_attempted();
bool mounted();
bool read_only();
bool live_media();
uint64_t first_lba();
uint64_t sector_count();
const char* volume_label();
const char* configuration();
size_t configuration_size();

// Path operations use the calling process working directory when a Ring-3
// process is active. Kernel callers without a process keep the root context.
vfs::Status chdir(const char* path);
vfs::Status getcwd(
    char* buffer,
    size_t capacity,
    size_t* required_size = nullptr);

vfs::Status stat(const char* path, vfs::FileStat* info);
vfs::Status read_file(
    const char* path,
    void* buffer,
    size_t capacity,
    size_t* bytes_read,
    uint64_t* file_size = nullptr);
vfs::Status open(
    const char* path,
    vfs::OpenFlags flags,
    vfs::OpenFileHandle* handle);
vfs::Status read(
    vfs::OpenFileHandle handle,
    void* buffer,
    size_t size,
    size_t* bytes_read = nullptr);
vfs::Status write(
    vfs::OpenFileHandle handle,
    const void* buffer,
    size_t size,
    size_t* bytes_written = nullptr);
vfs::Status seek(
    vfs::OpenFileHandle handle,
    int64_t offset,
    vfs::SeekOrigin origin,
    uint64_t* new_offset = nullptr);
vfs::Status readdir(
    vfs::OpenFileHandle handle,
    vfs::DirectoryEntry* entry);
vfs::Status close(vfs::OpenFileHandle handle);
vfs::Status create(const char* path);
vfs::Status unlink(const char* path);
vfs::Status rename(const char* source_path, const char* destination_path);
vfs::Status mkdir(const char* path);
vfs::Status rmdir(const char* path);
vfs::Status sync();

// Kernel-only extension point for mounting another production VFS backend
// below the persistent root. The target directory is created and synced when
// absent; the supplied backend and its context must outlive the mount.
vfs::Status mount_backend(
    const char* target,
    const vfs::FileSystem* filesystem,
    vfs::MountHandle* handle = nullptr);
Status initialization_status();
const char* status_message(Status status);
const char* detail_message();

} // namespace fs::root_volume
