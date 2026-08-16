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

// Mounts the GPT partition named "Kurogane Root" through PartitionDevice,
// FAT32 and VFS, then proves the complete read path with /etc/system.cfg.
Status initialize(
    const storage::block::Device* disk,
    const storage::gpt::Table* table);

// 3.3 dev live-media path. The installer package becomes a read-only root
// filesystem so ISO/IMG media can enter the regular Ring-3 desktop without
// first writing anything to a target disk.
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

// Reads from the mounted root through the active VFS adapter. The helper never
// truncates silently: BufferTooSmall is returned when the complete file does
// not fit in capacity. file_size is populated after a successful stat.
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
vfs::Status close(vfs::OpenFileHandle handle);
vfs::Status create(const char* path);
vfs::Status unlink(const char* path);
vfs::Status rename(const char* source_path, const char* destination_path);
vfs::Status mkdir(const char* path);
vfs::Status rmdir(const char* path);
vfs::Status sync();
Status initialization_status();
const char* status_message(Status status);
const char* detail_message();

} // namespace fs::root_volume
