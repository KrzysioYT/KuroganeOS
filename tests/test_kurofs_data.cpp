#include "../kernel/fs/kurofs.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace system_metrics { void record_disk_blocks(uint64_t) {} }

namespace {
constexpr uint32_t SECTOR_SIZE = 512U;
constexpr uint64_t SECTOR_COUNT = 128U;
uint8_t storage_bytes[SECTOR_SIZE * SECTOR_COUNT]{};
struct MemoryDevice { uint64_t flushes; };

storage::block::Status read_cb(void*, uint64_t first, uint64_t count, void* destination) {
    if (destination == nullptr || first >= SECTOR_COUNT || count > SECTOR_COUNT - first)
        return storage::block::Status::OutOfRange;
    std::memcpy(destination, storage_bytes + first * SECTOR_SIZE,
                static_cast<size_t>(count) * SECTOR_SIZE);
    return storage::block::Status::Ok;
}
storage::block::Status write_cb(void*, uint64_t first, uint64_t count, const void* source) {
    if (source == nullptr || first >= SECTOR_COUNT || count > SECTOR_COUNT - first)
        return storage::block::Status::OutOfRange;
    std::memcpy(storage_bytes + first * SECTOR_SIZE, source,
                static_cast<size_t>(count) * SECTOR_SIZE);
    return storage::block::Status::Ok;
}
storage::block::Status flush_cb(void* context) {
    auto* memory = static_cast<MemoryDevice*>(context);
    ++memory->flushes;
    return storage::block::Status::Ok;
}

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}

int main() {
    using namespace fs::kurofs;
    std::memset(storage_bytes, 0xA5, sizeof(storage_bytes));
    MemoryDevice memory{};
    storage::block::Device device{&memory, SECTOR_SIZE, SECTOR_COUNT, read_cb, write_cb, flush_cb};
    if (!expect(format(&device, 16U) == Status::Ok, "format")) return 1;
    FileSystem fs{};
    if (!expect(mount(&fs, &device) == Status::Ok, "mount")) return 1;

    uint64_t extent = 0U;
    if (!expect(allocate_blocks(&fs, 2U, &extent) == Status::Ok, "allocate extent")) return 1;
    for (size_t index = 0U; index < 2U * SECTOR_SIZE; ++index) {
        if (!expect(storage_bytes[extent * SECTOR_SIZE + index] == 0U,
                    "allocated extent must be zeroed")) return 1;
    }

    uint64_t inode_id = 0U;
    if (!expect(allocate_inode(&fs, InodeType::Regular, &inode_id) == Status::Ok,
                "allocate inode")) return 1;
    Inode inode{};
    if (!expect(read_inode(&fs, inode_id, &inode) == Status::Ok, "read fresh inode")) return 1;
    const uint32_t original_generation = inode.generation;
    const uint32_t original_revision = inode.revision;

    uint8_t payload[700]{};
    for (size_t index = 0U; index < sizeof(payload); ++index)
        payload[index] = static_cast<uint8_t>((index * 13U + 7U) & 0xffU);
    if (!expect(write_extent_data(&fs, extent, 2U, 0U, payload, sizeof(payload)) == Status::Ok,
                "write extent data")) return 1;
    inode.extent_start = extent;
    inode.extent_blocks = 2U;
    inode.size = sizeof(payload);
    if (!expect(update_inode(&fs, &inode) == Status::Ok, "publish inode ownership")) return 1;
    if (!expect(inode.generation == original_generation, "incarnation generation stable")) return 1;
    if (!expect(inode.revision == original_revision + 1U, "metadata revision increment")) return 1;

    Inode stale = inode;
    stale.revision = original_revision;
    if (!expect(update_inode(&fs, &stale) == Status::StaleInode, "reject stale update")) return 1;

    Inode illegal = inode;
    illegal.extent_start = fs.geometry.data_start + 10U;
    illegal.extent_blocks = 1U;
    illegal.size = 1U;
    if (!expect(update_inode(&fs, &illegal) == Status::InvalidExtent,
                "reject unallocated extent ownership")) return 1;

    uint8_t patch[100]{};
    for (size_t index = 0U; index < sizeof(patch); ++index) patch[index] = 0xCCU;
    if (!expect(write_extent_data(&fs, extent, 2U, 500U, patch, sizeof(patch)) == Status::Ok,
                "cross-sector partial write")) return 1;
    for (size_t index = 0U; index < sizeof(patch); ++index) payload[500U + index] = 0xCCU;

    FileSystem remounted{};
    if (!expect(mount(&remounted, &device) == Status::Ok, "remount")) return 1;
    Inode persisted{};
    if (!expect(read_inode(&remounted, inode_id, &persisted) == Status::Ok,
                "read persisted inode")) return 1;
    if (!expect(persisted.extent_start == extent && persisted.extent_blocks == 2U &&
                persisted.size == sizeof(payload) && persisted.generation == inode.generation &&
                persisted.revision == inode.revision, "persisted inode fields")) return 1;

    uint8_t output[800]{};
    size_t read = 0U;
    if (!expect(read_inode_data(&remounted, &persisted, 0U, output, sizeof(output), &read) == Status::Ok,
                "read persisted data")) return 1;
    if (!expect(read == sizeof(payload), "EOF-bounded read")) return 1;
    if (!expect(std::memcmp(output, payload, sizeof(payload)) == 0, "payload persistence")) return 1;
    if (!expect(read_inode_data(&remounted, &persisted, persisted.size, output, sizeof(output), &read) == Status::Ok && read == 0U,
                "EOF zero read")) return 1;
    if (!expect(write_extent_data(&remounted, extent, 2U, 1000U, patch, sizeof(patch)) == Status::NoSpace,
                "reject extent overflow")) return 1;

    Inode stale_read = persisted;
    stale_read.revision -= 1U;
    if (!expect(read_inode_data(&remounted, &stale_read, 0U, output, sizeof(output), &read) == Status::StaleInode,
                "reject stale read snapshot")) return 1;

    std::puts("KuroFS inode/data persistence tests passed");
    return 0;
}
