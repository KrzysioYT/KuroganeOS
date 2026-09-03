#include "../kernel/fs/kurofs_volume.hpp"
#include "../kernel/fs/root_volume.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace system_metrics { void record_disk_blocks(uint64_t) {} }

namespace {

constexpr uint32_t SECTOR_SIZE = 512U;
constexpr uint64_t SECTOR_COUNT = 128U;
uint8_t disk[SECTOR_SIZE * SECTOR_COUNT]{};
bool root_is_mounted = false;
bool backend_was_mounted = false;

storage::block::Status read_blocks(
    void*, uint64_t first, uint64_t count, void* output) {
    if (output == nullptr || first >= SECTOR_COUNT ||
        count > SECTOR_COUNT - first) {
        return storage::block::Status::OutOfRange;
    }
    std::memcpy(
        output, disk + first * SECTOR_SIZE,
        static_cast<size_t>(count * SECTOR_SIZE));
    return storage::block::Status::Ok;
}

storage::block::Status write_blocks(
    void*, uint64_t first, uint64_t count, const void* source) {
    if (source == nullptr || first >= SECTOR_COUNT ||
        count > SECTOR_COUNT - first) {
        return storage::block::Status::OutOfRange;
    }
    std::memcpy(
        disk + first * SECTOR_SIZE, source,
        static_cast<size_t>(count * SECTOR_SIZE));
    return storage::block::Status::Ok;
}

storage::block::Status flush_device(void*) {
    return storage::block::Status::Ok;
}

storage::block::Device device{
    nullptr,
    SECTOR_SIZE,
    SECTOR_COUNT,
    read_blocks,
    write_blocks,
    flush_device,
};

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

} // namespace

namespace fs::root_volume {

bool mounted() { return root_is_mounted; }

vfs::Status mount_backend(
    const char* target,
    const vfs::FileSystem* filesystem,
    vfs::MountHandle* handle) {
    if (target == nullptr || std::strcmp(target, "/kuro") != 0 ||
        filesystem == nullptr || filesystem->read_only ||
        filesystem->operations.stat_path == nullptr) {
        return vfs::Status::InvalidArgument;
    }
    vfs::FileStat root{};
    if (filesystem->operations.stat_path(filesystem->context, "/", &root) !=
            vfs::Status::Ok ||
        root.type != vfs::NodeType::Directory) {
        return vfs::Status::BackendFailure;
    }
    backend_was_mounted = true;
    if (handle != nullptr) {
        handle->slot = 2U;
        handle->generation = 1U;
    }
    return vfs::Status::Ok;
}

} // namespace fs::root_volume

int main() {
    using namespace fs;
    std::memset(disk, 0, sizeof(disk));
    if (!expect(
            kurofs_volume::mount(&device) ==
                kurofs_volume::Status::RootUnavailable,
            "raw KuroFS waits for persistent root")) {
        return 1;
    }
    root_is_mounted = true;
    if (!expect(
            kurofs_volume::mount(&device) ==
                kurofs_volume::Status::KurofsMountFailed &&
            kurofs_volume::kurofs_detail_status() ==
                kurofs::Status::InvalidSuperblock,
            "unknown media is never formatted implicitly")) {
        return 1;
    }
    if (!expect(kurofs::format(&device, 16U) == kurofs::Status::Ok,
                "format explicit raw KuroFS fixture") ||
        !expect(
            kurofs_volume::mount(&device) == kurofs_volume::Status::Ok &&
            kurofs_volume::mounted() && backend_was_mounted,
            "mount raw KuroFS through production adapter") ||
        !expect(
            kurofs_volume::mount(&device) ==
                kurofs_volume::Status::AlreadyMounted,
            "reject duplicate raw KuroFS mount")) {
        return 1;
    }
    std::puts("KuroFS production volume adapter tests passed");
    return 0;
}
