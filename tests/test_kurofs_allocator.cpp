#include "../kernel/fs/kurofs.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace system_metrics {
void record_disk_blocks(uint64_t) {}
}

namespace {
constexpr uint32_t SECTOR_SIZE = 512U;
constexpr uint64_t SECTOR_COUNT = 128U;
uint8_t g_storage[SECTOR_SIZE * SECTOR_COUNT]{};

struct MemoryDevice {
    bool fail_reads;
    bool fail_writes;
    bool fail_flush;
    uint64_t flushes;
};

storage::block::Status read_blocks(
    void* context, uint64_t first, uint64_t count, void* destination) {
    auto* memory = static_cast<MemoryDevice*>(context);
    if (memory == nullptr || destination == nullptr) return storage::block::Status::InvalidArgument;
    if (memory->fail_reads) return storage::block::Status::IoError;
    if (first >= SECTOR_COUNT || count > SECTOR_COUNT - first) return storage::block::Status::OutOfRange;
    std::memcpy(destination, g_storage + first * SECTOR_SIZE,
                static_cast<size_t>(count) * SECTOR_SIZE);
    return storage::block::Status::Ok;
}

storage::block::Status write_blocks(
    void* context, uint64_t first, uint64_t count, const void* source) {
    auto* memory = static_cast<MemoryDevice*>(context);
    if (memory == nullptr || source == nullptr) return storage::block::Status::InvalidArgument;
    if (memory->fail_writes) return storage::block::Status::IoError;
    if (first >= SECTOR_COUNT || count > SECTOR_COUNT - first) return storage::block::Status::OutOfRange;
    std::memcpy(g_storage + first * SECTOR_SIZE, source,
                static_cast<size_t>(count) * SECTOR_SIZE);
    return storage::block::Status::Ok;
}

storage::block::Status flush(void* context) {
    auto* memory = static_cast<MemoryDevice*>(context);
    if (memory == nullptr) return storage::block::Status::InvalidArgument;
    ++memory->flushes;
    return memory->fail_flush ? storage::block::Status::IoError : storage::block::Status::Ok;
}
}

int main() {
    using namespace fs::kurofs;
    std::memset(g_storage, 0, sizeof(g_storage));
    MemoryDevice memory{};
    storage::block::Device device{
        &memory, SECTOR_SIZE, SECTOR_COUNT, read_blocks, write_blocks, flush
    };

    if (format(&device, 16U) != Status::Ok || memory.flushes < 2U) return 1;
    FileSystem filesystem{};
    if (mount(&filesystem, &device) != Status::Ok) return 2;
    Geometry geometry{};
    if (get_geometry(&filesystem, &geometry) != Status::Ok ||
        geometry.data_start >= geometry.total_blocks) return 3;

    uint64_t first = UINT64_MAX;
    if (allocate_blocks(&filesystem, 0U, &first) != Status::InvalidArgument ||
        allocate_blocks(&filesystem, SECTOR_COUNT, &first) != Status::NoSpace) return 4;
    if (allocate_blocks(&filesystem, 3U, &first) != Status::Ok ||
        first != geometry.data_start) return 5;
    uint64_t second = UINT64_MAX;
    if (allocate_blocks(&filesystem, 5U, &second) != Status::Ok || second != first + 3U) return 6;

    FileSystem remounted{};
    if (mount(&remounted, &device) != Status::Ok) return 7;
    uint64_t third = UINT64_MAX;
    if (allocate_blocks(&remounted, 1U, &third) != Status::Ok || third != second + 5U) return 8;

    uint64_t regular_id = 0U;
    uint64_t directory_id = 0U;
    if (allocate_inode(&remounted, InodeType::Regular, &regular_id) != Status::Ok ||
        regular_id != 2U ||
        allocate_inode(&remounted, InodeType::Directory, &directory_id) != Status::Ok ||
        directory_id != 3U) return 9;
    Inode inode{};
    if (read_inode(&remounted, regular_id, &inode) != Status::Ok ||
        inode.type != InodeType::Regular || inode.size != 0U ||
        inode.extent_blocks != 0U || inode.link_count != 1U) return 10;

    FileSystem second_mount{};
    if (mount(&second_mount, &device) != Status::Ok) return 11;
    uint64_t next_inode = 0U;
    if (allocate_inode(&second_mount, InodeType::Regular, &next_inode) != Status::Ok ||
        next_inode != 4U) return 12;
    for (uint64_t expected = 5U; expected <= 16U; ++expected) {
        uint64_t allocated = 0U;
        if (allocate_inode(&second_mount, InodeType::Regular, &allocated) != Status::Ok ||
            allocated != expected) return 13;
    }
    uint64_t exhausted = 0U;
    if (allocate_inode(&second_mount, InodeType::Regular, &exhausted) != Status::NoSpace) return 14;
    if (allocate_inode(&second_mount, static_cast<InodeType>(99U), &exhausted) !=
        Status::InvalidArgument) return 15;

    // I/O failures never publish a successful allocation to the caller.
    memory.fail_writes = true;
    uint64_t failed = UINT64_MAX;
    if (allocate_blocks(&second_mount, 1U, &failed) != Status::BlockDeviceError ||
        failed != UINT64_MAX) return 16;
    memory.fail_writes = false;
    memory.fail_flush = true;
    uint64_t uncertain = UINT64_MAX;
    if (allocate_blocks(&second_mount, 1U, &uncertain) != Status::BlockDeviceError ||
        uncertain != UINT64_MAX) return 17;
    memory.fail_flush = false;

    std::puts("KuroFS persistent allocator tests passed");
    return 0;
}
