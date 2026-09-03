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

uint32_t test_crc32(const uint8_t* data, size_t size) {
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0U; index < size; ++index) {
        crc ^= static_cast<uint32_t>(data[index]);
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(
                -static_cast<int32_t>(crc & UINT32_C(1)));
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

void test_store_u32(uint8_t* bytes, uint32_t value) {
    for (size_t index = 0U; index < 4U; ++index) {
        bytes[index] = static_cast<uint8_t>((value >> (index * 8U)) & UINT32_C(0xff));
    }
}

uint32_t test_load_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8U) |
        (static_cast<uint32_t>(bytes[2]) << 16U) |
        (static_cast<uint32_t>(bytes[3]) << 24U);
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

    uint8_t hidden_tail[64];
    std::memset(hidden_tail, 0xE7, sizeof(hidden_tail));
    if (!expect(write_extent_data(
                    &remounted, extent, 2U, 850U,
                    hidden_tail, sizeof(hidden_tail)) == Status::Ok,
                "stage bytes beyond logical EOF")) return 1;
    Inode stale_resize = persisted;
    const uint32_t before_growth_revision = persisted.revision;
    if (!expect(resize_inode(&remounted, &persisted, 1300U) == Status::Ok &&
                persisted.extent_start == extent && persisted.extent_blocks == 3U &&
                persisted.size == 1300U &&
                persisted.revision == before_growth_revision + 1U,
                "grow regular file in place")) return 1;
    std::memset(output, 0xFF, sizeof(output));
    read = 0U;
    if (!expect(read_inode_data(
                    &remounted, &persisted, 0U, output, sizeof(output), &read) == Status::Ok &&
                read == sizeof(output) &&
                std::memcmp(output, payload, sizeof(payload)) == 0,
                "growth preserves published contents")) return 1;
    uint8_t grown_tail[600]{};
    read = 0U;
    if (!expect(read_inode_data(
                    &remounted, &persisted, sizeof(payload), grown_tail,
                    sizeof(grown_tail), &read) == Status::Ok &&
                read == sizeof(grown_tail),
                "read zero-filled growth")) return 1;
    for (size_t index = 0U; index < sizeof(grown_tail); ++index) {
        if (!expect(grown_tail[index] == 0U,
                    "growth must not expose staged tail bytes")) return 1;
    }
    if (!expect(resize_inode(&remounted, &stale_resize, 900U) == Status::StaleInode,
                "reject stale resize snapshot")) return 1;

    const uint32_t before_shrink_revision = persisted.revision;
    if (!expect(resize_inode(&remounted, &persisted, 100U) == Status::Ok &&
                persisted.extent_start == extent && persisted.extent_blocks == 1U &&
                persisted.size == 100U &&
                persisted.revision == before_shrink_revision + 1U,
                "shrink file and publish smaller extent")) return 1;
    uint64_t reclaimed = 0U;
    if (!expect(allocate_blocks(&remounted, 2U, &reclaimed) == Status::Ok &&
                reclaimed == extent + 1U,
                "trailing blocks reclaimed for allocator")) return 1;

    const uint32_t before_relocation_revision = persisted.revision;
    if (!expect(resize_inode(&remounted, &persisted, 700U) == Status::Ok &&
                persisted.extent_start != extent && persisted.extent_blocks == 2U &&
                persisted.size == 700U &&
                persisted.revision == before_relocation_revision + 1U,
                "relocate growth when adjacent blocks are occupied")) return 1;
    std::memset(output, 0xFF, sizeof(output));
    read = 0U;
    if (!expect(read_inode_data(
                    &remounted, &persisted, 0U, output, sizeof(output), &read) == Status::Ok &&
                read == 700U && std::memcmp(output, payload, 100U) == 0,
                "relocated growth preserves retained prefix")) return 1;
    for (size_t index = 100U; index < read; ++index) {
        if (!expect(output[index] == 0U,
                    "relocated growth zero-fills new range")) return 1;
    }
    uint64_t recycled_original = 0U;
    if (!expect(allocate_blocks(&remounted, 1U, &recycled_original) == Status::Ok &&
                recycled_original == extent,
                "relocation releases original extent")) return 1;
    const Inode before_no_space = persisted;
    if (!expect(resize_inode(&remounted, &persisted, UINT64_MAX) == Status::NoSpace &&
                persisted.extent_start == before_no_space.extent_start &&
                persisted.extent_blocks == before_no_space.extent_blocks &&
                persisted.size == before_no_space.size &&
                persisted.revision == before_no_space.revision,
                "no-space growth leaves inode unchanged")) return 1;

    FileSystem resized_mount{};
    if (!expect(mount(&resized_mount, &device) == Status::Ok,
                "remount resized filesystem")) return 1;
    Inode resized_inode{};
    if (!expect(read_inode(&resized_mount, inode_id, &resized_inode) == Status::Ok &&
                resized_inode.extent_start == persisted.extent_start &&
                resized_inode.extent_blocks == persisted.extent_blocks &&
                resized_inode.size == persisted.size &&
                resized_inode.revision == persisted.revision,
                "resized inode survives remount")) return 1;
    const uint64_t released_file_extent = resized_inode.extent_start;
    const uint32_t before_zero_revision = resized_inode.revision;
    if (!expect(resize_inode(&resized_mount, &resized_inode, 0U) == Status::Ok &&
                resized_inode.extent_start == 0U && resized_inode.extent_blocks == 0U &&
                resized_inode.size == 0U &&
                resized_inode.revision == before_zero_revision + 1U,
                "truncate file to an empty inode")) return 1;
    uint64_t reclaimed_file_extent = 0U;
    if (!expect(allocate_blocks(&resized_mount, 2U, &reclaimed_file_extent) == Status::Ok &&
                reclaimed_file_extent == released_file_extent,
                "truncate-to-zero reclaims the complete file extent")) return 1;
    FileSystem empty_mount{};
    Inode empty_inode{};
    if (!expect(mount(&empty_mount, &device) == Status::Ok &&
                read_inode(&empty_mount, inode_id, &empty_inode) == Status::Ok &&
                empty_inode.size == 0U && empty_inode.extent_start == 0U &&
                empty_inode.extent_blocks == 0U &&
                empty_inode.revision == resized_inode.revision,
                "empty truncation survives remount")) return 1;

    // Simulate an older KuroFS v1 inode whose formerly reserved revision
    // field is zero while preserving a valid inode checksum.
    const uint64_t root_byte = remounted.geometry.inode_table_start * SECTOR_SIZE;
    uint8_t* const legacy_root = storage_bytes + root_byte;
    test_store_u32(legacy_root + 48U, 0U);
    test_store_u32(legacy_root + INODE_SIZE - sizeof(uint32_t),
                   test_crc32(legacy_root, INODE_SIZE - sizeof(uint32_t)));
    FileSystem legacy_mount{};
    if (!expect(mount(&legacy_mount, &device) == Status::Ok, "mount legacy v1 revision-zero inode")) return 1;
    Inode legacy_root_inode{};
    if (!expect(read_inode(&legacy_mount, ROOT_INODE, &legacy_root_inode) == Status::Ok &&
                legacy_root_inode.revision == 1U, "normalize legacy revision")) return 1;
    if (!expect(update_inode(&legacy_mount, &legacy_root_inode) == Status::Ok &&
                legacy_root_inode.revision == 2U, "upgrade legacy inode on write")) return 1;
    if (!expect(test_load_u32(legacy_root + 48U) == 2U, "persist upgraded revision encoding")) return 1;

    Inode stale_read = persisted;
    stale_read.revision -= 1U;
    if (!expect(read_inode_data(&remounted, &stale_read, 0U, output, sizeof(output), &read) == Status::StaleInode,
                "reject stale read snapshot")) return 1;

    if (!expect(format(&device, 16U) == Status::Ok,
                "reformat copy-on-write write fixture")) return 1;
    FileSystem write_fs{};
    if (!expect(mount(&write_fs, &device) == Status::Ok,
                "mount copy-on-write write fixture")) return 1;
    uint64_t write_inode_id = 0U;
    if (!expect(allocate_inode(
                    &write_fs, InodeType::Regular, &write_inode_id) == Status::Ok,
                "allocate copy-on-write file")) return 1;
    Inode write_inode{};
    if (!expect(read_inode(&write_fs, write_inode_id, &write_inode) == Status::Ok,
                "read copy-on-write file")) return 1;
    static const uint8_t sparse_payload[] = {'A', 'B', 'C'};
    if (!expect(write_inode_data(
                    &write_fs, &write_inode, 100U,
                    sparse_payload, sizeof(sparse_payload)) == Status::Ok &&
                write_inode.size == 103U && write_inode.extent_blocks == 1U,
                "publish sparse copy-on-write write")) return 1;
    uint8_t sparse_output[103]{};
    read = 0U;
    if (!expect(read_inode_data(
                    &write_fs, &write_inode, 0U,
                    sparse_output, sizeof(sparse_output), &read) == Status::Ok &&
                read == sizeof(sparse_output) &&
                std::memcmp(sparse_output + 100U, sparse_payload,
                            sizeof(sparse_payload)) == 0,
                "read sparse copy-on-write result")) return 1;
    for (size_t index = 0U; index < 100U; ++index) {
        if (!expect(sparse_output[index] == 0U,
                    "sparse gap must be zero-filled")) return 1;
    }

    const uint64_t first_write_extent = write_inode.extent_start;
    Inode stale_write = write_inode;
    static const uint8_t replacement = 'z';
    if (!expect(write_inode_data(
                    &write_fs, &write_inode, 101U,
                    &replacement, sizeof(replacement)) == Status::Ok &&
                write_inode.extent_start != first_write_extent &&
                write_inode.size == sizeof(sparse_output),
                "copy-on-write overwrite relocates before publication")) return 1;
    if (!expect(write_inode_data(
                    &write_fs, &stale_write, 0U,
                    &replacement, sizeof(replacement)) == Status::StaleInode,
                "reject stale copy-on-write writer")) return 1;
    uint64_t reclaimed_write_extent = 0U;
    if (!expect(allocate_blocks(&write_fs, 1U, &reclaimed_write_extent) == Status::Ok &&
                reclaimed_write_extent == first_write_extent,
                "copy-on-write write reclaims prior extent")) return 1;
    FileSystem write_remount{};
    Inode persisted_write{};
    std::memset(sparse_output, 0, sizeof(sparse_output));
    if (!expect(mount(&write_remount, &device) == Status::Ok &&
                read_inode(&write_remount, write_inode_id, &persisted_write) == Status::Ok &&
                persisted_write.size == sizeof(sparse_output),
                "copy-on-write write survives remount")) return 1;
    read = 0U;
    if (!expect(read_inode_data(
                    &write_remount, &persisted_write, 0U,
                    sparse_output, sizeof(sparse_output), &read) == Status::Ok &&
                read == sizeof(sparse_output) &&
                sparse_output[100] == 'A' && sparse_output[101] == 'z' &&
                sparse_output[102] == 'C',
                "overwritten payload survives remount")) return 1;

    std::puts("KuroFS inode/data persistence tests passed");
    return 0;
}
