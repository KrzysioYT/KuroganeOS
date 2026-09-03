#include "../kernel/fs/kurofs.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace system_metrics { void record_disk_blocks(uint64_t) {} }

namespace {

constexpr uint32_t SECTOR_SIZE = 512U;
constexpr uint64_t SECTOR_COUNT = 256U;
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

uint8_t* raw_inode(
    const fs::kurofs::Geometry& geometry, uint64_t inode_id) {
    const uint64_t offset = geometry.inode_table_start * SECTOR_SIZE +
        (inode_id - 1U) * fs::kurofs::INODE_SIZE;
    return disk + offset;
}

void set_inode_flags(
    const fs::kurofs::Geometry& geometry,
    uint64_t inode_id,
    uint32_t flags) {
    uint8_t* const inode = raw_inode(geometry, inode_id);
    store_u32(inode + 12U, flags);
    store_u32(
        inode + fs::kurofs::INODE_SIZE - sizeof(uint32_t),
        crc32(inode, fs::kurofs::INODE_SIZE - sizeof(uint32_t)));
}

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

bool expect_ownership(
    fs::kurofs::FileSystem* filesystem,
    uint64_t inode_id,
    fs::kurofs::InodeOwnership expected,
    const char* message) {
    fs::kurofs::InodeOwnership actual = fs::kurofs::InodeOwnership::Free;
    return expect(
        fs::kurofs::inode_ownership(filesystem, inode_id, &actual) ==
            fs::kurofs::Status::Ok &&
        actual == expected,
        message);
}

} // namespace

int main() {
    using namespace fs::kurofs;
    std::memset(disk, 0x5A, sizeof(disk));
    if (!expect(format(&device, 32U) == Status::Ok, "format ownership fixture")) {
        return 1;
    }
    FileSystem filesystem{};
    if (!expect(
            mount(&filesystem, &device) == Status::Ok &&
            (filesystem.geometry.feature_flags & FEATURE_INODE_OWNERSHIP) != 0U,
            "mount ownership-aware filesystem")) {
        return 1;
    }
    InodeOwnershipSummary summary{};
    if (!expect(
            scan_inode_ownership(&filesystem, &summary) == Status::Ok &&
            summary.live == 1U && summary.free == 31U &&
            summary.pending == 0U && summary.tombstoned == 0U &&
            summary.orphan == 0U,
            "classify fresh inode table")) {
        return 1;
    }

    uint64_t tree_id = 0U;
    uint64_t leaf_id = 0U;
    if (!expect(
            allocate_inode(&filesystem, InodeType::Directory, &tree_id) ==
                Status::Ok &&
            allocate_inode(&filesystem, InodeType::Regular, &leaf_id) ==
                Status::Ok,
            "persist pending inode reservations") ||
        !expect_ownership(
            &filesystem, tree_id, InodeOwnership::Pending,
            "unattached directory is pending") ||
        !expect_ownership(
            &filesystem, leaf_id, InodeOwnership::Pending,
            "unattached file is pending")) {
        return 1;
    }

    Inode tree{};
    if (!expect(
            read_inode(&filesystem, tree_id, &tree) == Status::Ok &&
            directory_append(&filesystem, &tree, "leaf", leaf_id) == Status::Ok,
            "attach pending leaf to detached tree") ||
        !expect_ownership(
            &filesystem, tree_id, InodeOwnership::Pending,
            "detached tree root remains pending") ||
        !expect_ownership(
            &filesystem, leaf_id, InodeOwnership::Live,
            "child with one namespace owner is live")) {
        return 1;
    }

    FileSystem orphan_mount{};
    if (!expect(
            mount(&orphan_mount, &device) == Status::Ok,
            "normalize interrupted detached tree") ||
        !expect_ownership(
            &orphan_mount, tree_id, InodeOwnership::Orphan,
            "unattached pending root becomes orphan") ||
        !expect_ownership(
            &orphan_mount, leaf_id, InodeOwnership::Live,
            "owned descendant remains live")) {
        return 1;
    }

    Inode root{};
    if (!expect(
            read_inode(&orphan_mount, ROOT_INODE, &root) == Status::Ok &&
            directory_append(&orphan_mount, &root, "tree", tree_id) == Status::Ok,
            "reattach orphan tree through normal namespace publication") ||
        !expect_ownership(
            &orphan_mount, tree_id, InodeOwnership::Live,
            "reattached tree becomes live")) {
        return 1;
    }

    set_inode_flags(orphan_mount.geometry, tree_id, INODE_FLAG_PENDING);
    FileSystem recovered{};
    if (!expect(
            mount(&recovered, &device) == Status::Ok,
            "recover attached pending inode") ||
        !expect_ownership(
            &recovered, tree_id, InodeOwnership::Live,
            "attached pending inode normalizes to live")) {
        return 1;
    }

    Inode recovered_root{};
    Inode recovered_tree{};
    const uint32_t old_tree_generation = tree.generation;
    if (!expect(
            read_inode(&recovered, ROOT_INODE, &recovered_root) == Status::Ok &&
            read_inode(&recovered, tree_id, &recovered_tree) == Status::Ok &&
            directory_remove(&recovered, &recovered_tree, "leaf") == Status::Ok &&
            directory_remove(&recovered, &recovered_root, "tree") == Status::Ok,
            "retire live subtree from leaves upward") ||
        !expect_ownership(
            &recovered, tree_id, InodeOwnership::Tombstoned,
            "removed directory is tombstoned") ||
        !expect_ownership(
            &recovered, leaf_id, InodeOwnership::Tombstoned,
            "removed file is tombstoned")) {
        return 1;
    }

    uint64_t reused_id = 0U;
    Inode reused{};
    if (!expect(
            allocate_inode(&recovered, InodeType::Regular, &reused_id) ==
                Status::Ok &&
            reused_id == tree_id &&
            read_inode(&recovered, reused_id, &reused) == Status::Ok &&
            reused.generation == old_tree_generation + 1U,
            "reuse tombstone as a new pending incarnation") ||
        !expect_ownership(
            &recovered, reused_id, InodeOwnership::Pending,
            "reused tombstone is pending")) {
        return 1;
    }

    summary = {};
    if (!expect(
            scan_inode_ownership(&recovered, &summary) == Status::Ok &&
            summary.live == 1U && summary.pending == 1U &&
            summary.tombstoned == 1U && summary.free == 29U &&
            summary.orphan == 0U,
            "summarize live, pending, tombstoned and free states")) {
        return 1;
    }

    std::puts("KuroFS persistent inode ownership classification tests passed");
    return 0;
}
