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

// Create an independent VFS path namespace view. Contexts start at root and
// can later move cwd without mutating the kernel/global path context.
vfs::Status initialize_path_context(vfs::PathContext* context);
vfs::Status chdir(vfs::PathContext* context, const char* path);
vfs::Status getcwd(
    const vfs::PathContext* context,
    char* buffer,
    size_t capacity,
    size_t* required_size = nullptr);

// Explicit-context operations are used by Ring-3 processes. The overloads
// without a PathContext keep existing kernel callers rooted at the global cwd.
vfs::Status stat(
    const vfs::PathContext* context,
    const char* path,
    vfs::FileStat* info);
vfs::Status stat(const char* path, vfs::FileStat* info);

vfs::Status read_file(
    const char* path,
    void* buffer,
    size_t capacity,
    size_t* bytes_read,
    uint64_t* file_size = nullptr);

vfs::Status open(
    const vfs::PathContext* context,
    const char* path,
    vfs::OpenFlags flags,
    vfs::OpenFileHandle* handle);
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

vfs::Status create(const vfs::PathContext* context, const char* path);
vfs::Status create(const char* path);
vfs::Status unlink(const vfs::PathContext* context, const char* path);
vfs::Status unlink(const char* path);
vfs::Status rename(
    const vfs::PathContext* context,
    const char* source_path,
    const char* destination_path);
vfs::Status rename(const char* source_path, const char* destination_path);
vfs::Status mkdir(const vfs::PathContext* context, const char* path);
vfs::Status mkdir(const char* path);
vfs::Status rmdir(const vfs::PathContext* context, const char* path);
vfs::Status rmdir(const char* path);
vfs::Status sync();

Status initialization_status();
const char* status_message(Status status);
const char* detail_message();

} // namespace fs::root_volume
