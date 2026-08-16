#include "root_volume.hpp"

#include "fat32.hpp"
#include "fat32_vfs.hpp"
#include "vfs.hpp"
#include "../storage/partition_device.hpp"

namespace fs::root_volume {

namespace {

constexpr char kPartitionName[] = "Kurogane Root";
constexpr char kConfigurationPath[] = "/etc/system.cfg";
constexpr char kRequiredConfigurationKey[] = "HOSTNAME=";
constexpr size_t kConfigurationCapacity = 512U;

bool g_attempted = false;
Status g_status = Status::InvalidArgument;
bool g_mounted = false;
storage::partition::Device g_partition{};
fat32::FileSystem g_fat32{};
fat32_vfs::Adapter g_adapter{};
vfs::FileSystem g_backend{};
vfs::State g_vfs{};
vfs::PathContext g_path_context{};
char g_configuration[kConfigurationCapacity]{};
size_t g_configuration_size = 0U;
vfs::Status g_detail_status = vfs::Status::Ok;
alignas(1) uint8_t g_vfs_lock = 0U;

void lock_vfs() {
    while (__atomic_test_and_set(&g_vfs_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
}

void unlock_vfs() {
    __atomic_clear(&g_vfs_lock, __ATOMIC_RELEASE);
}

struct VfsGuard {
    VfsGuard() { lock_vfs(); }
    ~VfsGuard() { unlock_vfs(); }
};

bool partition_name_equals(const storage::gpt::Partition& partition) {
    constexpr size_t expected_length = sizeof(kPartitionName) - 1U;
    if (partition.name_length != expected_length) {
        return false;
    }
    for (size_t index = 0U; index < expected_length; ++index) {
        if (partition.name[index] !=
            static_cast<uint16_t>(
                static_cast<unsigned char>(kPartitionName[index]))) {
            return false;
        }
    }
    return true;
}

bool contains_required_key(const char* buffer, size_t size) {
    constexpr size_t key_size = sizeof(kRequiredConfigurationKey) - 1U;
    if (buffer == nullptr || size < key_size) {
        return false;
    }
    for (size_t start = 0U; start <= size - key_size; ++start) {
        bool equal = true;
        for (size_t index = 0U; index < key_size; ++index) {
            if (buffer[start + index] != kRequiredConfigurationKey[index]) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return true;
        }
    }
    return false;
}

Status fail(Status status) {
    g_status = status;
    return status;
}

} // namespace

Status initialize(
    const storage::block::Device* disk,
    const storage::gpt::Table* table) {
    if (g_attempted) {
        return Status::AlreadyAttempted;
    }
    if (disk == nullptr || table == nullptr) {
        return fail(Status::InvalidArgument);
    }

    const storage::gpt::Partition* root = nullptr;
    for (size_t index = 0U; index < table->partition_count; ++index) {
        if (partition_name_equals(table->partitions[index])) {
            root = &table->partitions[index];
            break;
        }
    }
    if (root == nullptr) {
        return fail(Status::RootPartitionNotFound);
    }
    g_attempted = true;
    if (root->last_lba < root->first_lba ||
        root->last_lba == UINT64_MAX) {
        return fail(Status::InvalidPartitionRange);
    }
    const uint64_t sectors = root->last_lba - root->first_lba + 1U;
    if (storage::partition::initialize(
            &g_partition, disk, root->first_lba, sectors) !=
        storage::block::Status::Ok) {
        return fail(Status::PartitionInitializationFailed);
    }
    if (fat32::mount(
            &g_fat32,
            storage::partition::as_block_device(&g_partition)) !=
        fat32::Status::Ok) {
        return fail(Status::Fat32MountFailed);
    }
    if (fat32_vfs::initialize(&g_adapter, &g_fat32, &g_backend) !=
        vfs::Status::Ok) {
        return fail(Status::VfsAdapterFailed);
    }
    if (vfs::initialize(&g_vfs, &g_backend) != vfs::Status::Ok ||
        vfs::initialize_path_context(&g_vfs, &g_path_context) !=
            vfs::Status::Ok) {
        return fail(Status::VfsInitializationFailed);
    }

    vfs::OpenFileHandle handle{};
    const vfs::Status open_status = vfs::open(
        &g_vfs,
        &g_path_context,
        kConfigurationPath,
        vfs::OpenFlags::Read,
        &handle);
    g_detail_status = open_status;
    if (open_status == vfs::Status::NotFound) {
        return fail(Status::RootConfigurationMissing);
    }
    if (open_status != vfs::Status::Ok) {
        return fail(Status::RootConfigurationReadFailed);
    }

    size_t bytes_read = 0U;
    const vfs::Status read_status = vfs::read(
        &g_vfs,
        handle,
        g_configuration,
        sizeof(g_configuration) - 1U,
        &bytes_read);
    const vfs::Status close_status = vfs::close(&g_vfs, handle);
    g_detail_status = read_status != vfs::Status::Ok
        ? read_status
        : close_status;
    if (read_status != vfs::Status::Ok ||
        close_status != vfs::Status::Ok) {
        return fail(Status::RootConfigurationReadFailed);
    }
    g_configuration[bytes_read] = '\0';
    g_configuration_size = bytes_read;
    if (!contains_required_key(g_configuration, bytes_read)) {
        return fail(Status::RootConfigurationInvalid);
    }

    g_mounted = true;
    g_status = Status::Ok;
    return Status::Ok;
}

bool initialization_attempted() {
    return g_attempted;
}

bool mounted() {
    return g_mounted;
}

bool read_only() {
    return false;
}

uint64_t first_lba() {
    return g_mounted ? g_partition.first_lba : 0U;
}

uint64_t sector_count() {
    return g_mounted ? g_partition.block_device.sector_count : 0U;
}

const char* volume_label() {
    return g_mounted ? fat32::volume_label(&g_fat32) : nullptr;
}

const char* configuration() {
    return g_mounted ? g_configuration : nullptr;
}

size_t configuration_size() {
    return g_mounted ? g_configuration_size : 0U;
}

vfs::Status stat(const char* path, vfs::FileStat* info) {
    if (!g_mounted) {
        return vfs::Status::NotInitialized;
    }
    VfsGuard guard{};
    return vfs::stat(&g_vfs, &g_path_context, path, info);
}

vfs::Status read_file(
    const char* path,
    void* buffer,
    size_t capacity,
    size_t* bytes_read,
    uint64_t* file_size) {
    if (bytes_read != nullptr) {
        *bytes_read = 0U;
    }
    if (file_size != nullptr) {
        *file_size = 0U;
    }
    if (!g_mounted) {
        return vfs::Status::NotInitialized;
    }
    if (path == nullptr || bytes_read == nullptr ||
        (capacity != 0U && buffer == nullptr)) {
        return vfs::Status::InvalidArgument;
    }

    VfsGuard guard{};

    vfs::FileStat info{};
    vfs::Status status =
        vfs::stat(&g_vfs, &g_path_context, path, &info);
    if (status != vfs::Status::Ok) {
        return status;
    }
    if (info.type != vfs::NodeType::Regular) {
        return vfs::Status::IsDirectory;
    }
    if (file_size != nullptr) {
        *file_size = info.size;
    }
    if (info.size > capacity) {
        return vfs::Status::BufferTooSmall;
    }

    vfs::OpenFileHandle handle{};
    status = vfs::open(
        &g_vfs, &g_path_context, path, vfs::OpenFlags::Read, &handle);
    if (status != vfs::Status::Ok) {
        return status;
    }
    status = vfs::read(&g_vfs, handle, buffer, capacity, bytes_read);
    const vfs::Status close_status = vfs::close(&g_vfs, handle);
    if (status != vfs::Status::Ok) {
        return status;
    }
    if (close_status != vfs::Status::Ok) {
        return close_status;
    }
    return *bytes_read == info.size
        ? vfs::Status::Ok
        : vfs::Status::IoError;
}

vfs::Status open(
    const char* path,
    vfs::OpenFlags flags,
    vfs::OpenFileHandle* handle) {
    if (!g_mounted) return vfs::Status::NotInitialized;
    VfsGuard guard{};
    return vfs::open(&g_vfs, &g_path_context, path, flags, handle);
}

vfs::Status read(
    vfs::OpenFileHandle handle,
    void* buffer,
    size_t size,
    size_t* bytes_read) {
    if (!g_mounted) return vfs::Status::NotInitialized;
    VfsGuard guard{};
    return vfs::read(&g_vfs, handle, buffer, size, bytes_read);
}

vfs::Status write(
    vfs::OpenFileHandle handle,
    const void* buffer,
    size_t size,
    size_t* bytes_written) {
    if (!g_mounted) return vfs::Status::NotInitialized;
    VfsGuard guard{};
    return vfs::write(&g_vfs, handle, buffer, size, bytes_written);
}

vfs::Status close(vfs::OpenFileHandle handle) {
    if (!g_mounted) return vfs::Status::NotInitialized;
    VfsGuard guard{};
    return vfs::close(&g_vfs, handle);
}

vfs::Status create(const char* path) {
    if (!g_mounted) return vfs::Status::NotInitialized;
    VfsGuard guard{};
    return vfs::create(&g_vfs, &g_path_context, path);
}

vfs::Status unlink(const char* path) {
    if (!g_mounted) return vfs::Status::NotInitialized;
    VfsGuard guard{};
    return vfs::unlink(&g_vfs, &g_path_context, path);
}

vfs::Status rename(const char* source_path, const char* destination_path) {
    if (!g_mounted) return vfs::Status::NotInitialized;
    VfsGuard guard{};
    return vfs::rename(
        &g_vfs, &g_path_context, source_path, destination_path);
}

vfs::Status mkdir(const char* path) {
    if (!g_mounted) return vfs::Status::NotInitialized;
    VfsGuard guard{};
    return vfs::mkdir(&g_vfs, &g_path_context, path);
}

vfs::Status rmdir(const char* path) {
    if (!g_mounted) return vfs::Status::NotInitialized;
    VfsGuard guard{};
    return vfs::rmdir(&g_vfs, &g_path_context, path);
}

vfs::Status sync() {
    if (!g_mounted) return vfs::Status::NotInitialized;
    VfsGuard guard{};
    return vfs::sync_all(&g_vfs);
}

Status initialization_status() {
    return g_status;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyAttempted: return "root mount already attempted";
        case Status::InvalidArgument: return "invalid root-volume argument";
        case Status::RootPartitionNotFound:
            return "GPT partition named Kurogane Root was not found";
        case Status::InvalidPartitionRange:
            return "root partition range is invalid";
        case Status::PartitionInitializationFailed:
            return "partition block device initialization failed";
        case Status::Fat32MountFailed: return "FAT32 root mount failed";
        case Status::VfsAdapterFailed: return "FAT32 VFS adapter failed";
        case Status::VfsInitializationFailed: return "VFS initialization failed";
        case Status::RootConfigurationMissing:
            return "/etc/system.cfg is missing on persistent root";
        case Status::RootConfigurationReadFailed:
            return "cannot read /etc/system.cfg through VFS";
        case Status::RootConfigurationInvalid:
            return "/etc/system.cfg lacks required HOSTNAME key";
    }
    return "unknown root-volume status";
}

const char* detail_message() {
    return vfs::status_message(g_detail_status);
}

} // namespace fs::root_volume
