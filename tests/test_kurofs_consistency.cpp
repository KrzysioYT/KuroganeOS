#include "../kernel/fs/kurofs.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace system_metrics { void record_disk_blocks(uint64_t) {} }

namespace {

constexpr uint32_t SECTOR_SIZE = 512U;
constexpr uint64_t SECTOR_COUNT = 128U;
uint8_t disk[SECTOR_SIZE * SECTOR_COUNT]{};

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

uint32_t crc32(const uint8_t* bytes, size_t size) {
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0U; index < size; ++index) {
        crc ^= static_cast<uint32_t>(bytes[index]);
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(
                -static_cast<int32_t>(crc & UINT32_C(1)));
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

void store_u32(uint8_t* bytes, uint32_t value) {
    for (size_t index = 0U; index < sizeof(value); ++index) {
        bytes[index] = static_cast<uint8_t>(
            (value >> (index * 8U)) & UINT32_C(0xff));
    }
}

void store_u64(uint8_t* bytes, uint64_t value) {
    for (size_t index = 0U; index < sizeof(value); ++index) {
        bytes[index] = static_cast<uint8_t>(
            (value >> (index * 8U)) & UINT64_C(0xff));
    }
}

struct Fixture {
    fs::kurofs::Geometry geometry;
    fs::kurofs::Inode root;
    fs::kurofs::Inode first;
    fs::kurofs::Inode second;
};

bool reset_fixture(Fixture* output) {
    using namespace fs::kurofs;
    std::memset(disk, 0x5A, sizeof(disk));
    Status status = format(&device, 16U);
    if (status != Status::Ok) {
        std::fprintf(stderr, "FAIL: consistency fixture format: %s\n",
                     status_message(status));
        return false;
    }
    FileSystem filesystem{};
    Fixture fixture{};
    status = mount(&filesystem, &device);
    if (status == Status::Ok) {
        status = get_geometry(&filesystem, &fixture.geometry);
    }
    if (status == Status::Ok) {
        status = read_inode(&filesystem, ROOT_INODE, &fixture.root);
    }
    if (status == Status::Ok) {
        status = directory_create(
            &filesystem, &fixture.root, "first", InodeType::Regular,
            &fixture.first);
    }
    if (status == Status::Ok) {
        status = directory_create(
            &filesystem, &fixture.root, "second", InodeType::Regular,
            &fixture.second);
    }
    if (status != Status::Ok) {
        std::fprintf(stderr, "FAIL: consistency fixture namespace: %s\n",
                     status_message(status));
        return false;
    }
    static const uint8_t first_payload[] = {'a', 'b', 'c'};
    static const uint8_t second_payload[] = {'x', 'y', 'z'};
    status = write_inode_data(
            &filesystem, &fixture.first, 0U,
            first_payload, sizeof(first_payload));
    if (status == Status::Ok) {
        status = write_inode_data(
            &filesystem, &fixture.second, 0U,
            second_payload, sizeof(second_payload));
    }
    if (status != Status::Ok) {
        std::fprintf(stderr, "FAIL: consistency fixture data: %s\n",
                     status_message(status));
        return false;
    }
    *output = fixture;
    return true;
}

uint8_t* raw_inode(const Fixture& fixture, uint64_t inode_id) {
    const uint64_t byte_offset =
        fixture.geometry.inode_table_start * SECTOR_SIZE +
        (inode_id - 1U) * fs::kurofs::INODE_SIZE;
    return disk + byte_offset;
}

void update_inode_crc(uint8_t* inode) {
    store_u32(
        inode + fs::kurofs::INODE_SIZE - sizeof(uint32_t),
        crc32(inode, fs::kurofs::INODE_SIZE - sizeof(uint32_t)));
}

void update_directory_crc(uint8_t* record) {
    store_u32(
        record + fs::kurofs::DIRECTORY_ENTRY_SIZE - sizeof(uint32_t),
        crc32(
            record,
            fs::kurofs::DIRECTORY_ENTRY_SIZE - sizeof(uint32_t)));
}

bool expect_mount(fs::kurofs::Status expected, const char* message) {
    fs::kurofs::FileSystem filesystem{};
    const fs::kurofs::Status status = fs::kurofs::mount(&filesystem, &device);
    if (status == expected) return true;
    std::fprintf(
        stderr, "FAIL: %s (expected=%s actual=%s)\n",
        message,
        fs::kurofs::status_message(expected),
        fs::kurofs::status_message(status));
    return false;
}

} // namespace

int main() {
    using namespace fs::kurofs;
    Fixture fixture{};
    if (!reset_fixture(&fixture) ||
        !expect_mount(Status::Ok, "clean metadata mounts")) {
        return 1;
    }

    if (!reset_fixture(&fixture)) return 1;
    uint8_t* inode = raw_inode(fixture, fixture.first.id);
    store_u32(inode + 12U, UINT32_C(1) << 8U);
    update_inode_crc(inode);
    if (!expect_mount(
            Status::InvalidInodeMetadata,
            "unknown live inode flags are rejected")) {
        return 1;
    }

    if (!reset_fixture(&fixture)) return 1;
    inode = raw_inode(fixture, fixture.second.id);
    store_u64(inode + 16U, fixture.first.size);
    store_u64(inode + 24U, fixture.first.extent_start);
    store_u64(inode + 32U, fixture.first.extent_blocks);
    update_inode_crc(inode);
    if (!expect_mount(
            Status::OverlappingExtents,
            "overlapping live inode extents are rejected")) {
        return 1;
    }

    if (!reset_fixture(&fixture)) return 1;
    const uint64_t bitmap_index = fixture.first.extent_start / 4096U;
    const uint64_t bitmap_bit = fixture.first.extent_start % 4096U;
    uint8_t* const bitmap = disk +
        (fixture.geometry.allocation_bitmap_start + bitmap_index) * SECTOR_SIZE;
    bitmap[bitmap_bit / 8U] = static_cast<uint8_t>(
        bitmap[bitmap_bit / 8U] &
        ~static_cast<uint8_t>(UINT8_C(1) << (bitmap_bit % 8U)));
    if (!expect_mount(
            Status::InvalidExtent,
            "live inode reference to a free block is rejected")) {
        return 1;
    }

    if (!reset_fixture(&fixture)) return 1;
    uint8_t* const first_record =
        disk + fixture.root.extent_start * SECTOR_SIZE;
    uint8_t* const second_record = first_record + DIRECTORY_ENTRY_SIZE;
    std::memcpy(second_record, first_record, DIRECTORY_ENTRY_SIZE);
    if (!expect_mount(
            Status::CorruptDirectory,
            "duplicate directory name and child are rejected")) {
        return 1;
    }

    if (!reset_fixture(&fixture)) return 1;
    uint8_t* const stale_record =
        disk + fixture.root.extent_start * SECTOR_SIZE;
    store_u32(stale_record + 8U, fixture.first.generation + 1U);
    update_directory_crc(stale_record);
    if (!expect_mount(
            Status::CorruptDirectory,
            "stale directory child generation is rejected")) {
        return 1;
    }

    if (!reset_fixture(&fixture)) return 1;
    inode = raw_inode(fixture, fixture.second.id);
    store_u32(inode + 40U, 2U);
    update_inode_crc(inode);
    if (!expect_mount(
            Status::InvalidInodeMetadata,
            "unsupported live link count is rejected")) {
        return 1;
    }

    if (!reset_fixture(&fixture)) return 1;
    FileSystem alias_filesystem{};
    Inode alias_root{};
    Inode alias_directory{};
    if (mount(&alias_filesystem, &device) != Status::Ok ||
        read_inode(&alias_filesystem, ROOT_INODE, &alias_root) != Status::Ok ||
        directory_create(
            &alias_filesystem, &alias_root, "aliases", InodeType::Directory,
            &alias_directory) != Status::Ok ||
        directory_append(
            &alias_filesystem, &alias_directory,
            "duplicate", fixture.first.id) != Status::AlreadyExists ||
        !expect_mount(
            Status::Ok,
            "cross-parent child aliases are rejected before publication")) {
        return 1;
    }

    std::puts("KuroFS mount consistency validation tests passed");
    return 0;
}
