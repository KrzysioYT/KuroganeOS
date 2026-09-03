#include "../kernel/fs/kurofs.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace system_metrics { void record_disk_blocks(uint64_t) {} }

namespace {

constexpr uint32_t SECTOR_SIZE = 512U;
constexpr uint64_t SECTOR_COUNT = 256U;
constexpr size_t DISK_SIZE =
    static_cast<size_t>(SECTOR_SIZE * SECTOR_COUNT);
constexpr size_t NEVER_FAIL = std::numeric_limits<size_t>::max();

uint8_t working_disk[DISK_SIZE]{};
uint8_t base_orphan_disk[DISK_SIZE]{};

struct MemoryDevice {
    uint8_t* bytes;
    size_t writes;
    size_t flushes;
    size_t fail_write;
    size_t fail_flush;
};

storage::block::Status read_blocks(
    void* context,
    uint64_t first,
    uint64_t count,
    void* output) {
    auto* memory = static_cast<MemoryDevice*>(context);
    if (memory == nullptr || output == nullptr || first >= SECTOR_COUNT ||
        count > SECTOR_COUNT - first) {
        return storage::block::Status::OutOfRange;
    }
    std::memcpy(
        output,
        memory->bytes + static_cast<size_t>(first * SECTOR_SIZE),
        static_cast<size_t>(count * SECTOR_SIZE));
    return storage::block::Status::Ok;
}

storage::block::Status write_blocks(
    void* context,
    uint64_t first,
    uint64_t count,
    const void* source) {
    auto* memory = static_cast<MemoryDevice*>(context);
    if (memory == nullptr || source == nullptr || first >= SECTOR_COUNT ||
        count > SECTOR_COUNT - first) {
        return storage::block::Status::OutOfRange;
    }
    const size_t call = memory->writes++;
    if (call == memory->fail_write) return storage::block::Status::IoError;
    std::memcpy(
        memory->bytes + static_cast<size_t>(first * SECTOR_SIZE),
        source,
        static_cast<size_t>(count * SECTOR_SIZE));
    return storage::block::Status::Ok;
}

storage::block::Status flush_device(void* context) {
    auto* memory = static_cast<MemoryDevice*>(context);
    if (memory == nullptr) return storage::block::Status::InvalidArgument;
    const size_t call = memory->flushes++;
    return call == memory->fail_flush
        ? storage::block::Status::IoError
        : storage::block::Status::Ok;
}

storage::block::Device make_device(MemoryDevice* memory) {
    return {
        memory,
        SECTOR_SIZE,
        SECTOR_COUNT,
        read_blocks,
        write_blocks,
        flush_device,
    };
}

void reset_calls(MemoryDevice* memory) {
    memory->writes = 0U;
    memory->flushes = 0U;
    memory->fail_write = NEVER_FAIL;
    memory->fail_flush = NEVER_FAIL;
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

bool bitmap_is_set(
    const uint8_t* bytes,
    const fs::kurofs::Geometry& geometry,
    uint64_t block) {
    const uint64_t bits_per_block = SECTOR_SIZE * 8U;
    const uint64_t bitmap_index = block / bits_per_block;
    const uint64_t bit = block % bits_per_block;
    const uint64_t bitmap_block =
        geometry.allocation_bitmap_start + bitmap_index;
    const size_t byte_offset = static_cast<size_t>(
        bitmap_block * SECTOR_SIZE + bit / 8U);
    const uint8_t mask = static_cast<uint8_t>(
        UINT8_C(1) << static_cast<uint8_t>(bit % 8U));
    return (bytes[byte_offset] & mask) != 0U;
}

bool build_single_orphan(uint64_t* out_inode_id) {
    using namespace fs::kurofs;
    std::memset(working_disk, 0x5A, sizeof(working_disk));
    MemoryDevice memory{
        working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
    storage::block::Device device = make_device(&memory);
    FileSystem filesystem{};
    uint64_t inode_id = 0U;
    Inode inode{};
    uint8_t payload[700]{};
    for (size_t index = 0U; index < sizeof(payload); ++index) {
        payload[index] = static_cast<uint8_t>(index ^ 0xA5U);
    }
    if (format(&device, 16U) != Status::Ok ||
        mount(&filesystem, &device) != Status::Ok ||
        allocate_inode(&filesystem, InodeType::Regular, &inode_id) !=
            Status::Ok ||
        read_inode(&filesystem, inode_id, &inode) != Status::Ok ||
        write_inode_data(
            &filesystem, &inode, 0U, payload, sizeof(payload)) != Status::Ok) {
        return false;
    }
    FileSystem normalized{};
    if (mount(&normalized, &device) != Status::Ok ||
        !expect_ownership(
            &normalized, inode_id, InodeOwnership::Orphan,
            "normalize recovery fixture to an explicit orphan")) {
        return false;
    }
    std::memcpy(base_orphan_disk, working_disk, sizeof(base_orphan_disk));
    *out_inode_id = inode_id;
    return true;
}

bool recovered_state_is_safe(
    MemoryDevice* memory,
    storage::block::Device* device,
    uint64_t inode_id,
    const char* message) {
    using namespace fs::kurofs;
    reset_calls(memory);
    FileSystem recovered{};
    if (!expect(mount(&recovered, device) == Status::Ok, message) ||
        !expect(
            validate_consistency(&recovered) == Status::Ok,
            "interrupted orphan reclaim remains consistent")) {
        return false;
    }
    InodeOwnership ownership = InodeOwnership::Free;
    if (!expect(
            inode_ownership(&recovered, inode_id, &ownership) == Status::Ok &&
            (ownership == InodeOwnership::Orphan ||
             ownership == InodeOwnership::Tombstoned),
            "interrupted reclaim exposes only orphan or tombstone")) {
        return false;
    }
    if (ownership == InodeOwnership::Orphan) {
        OrphanReclaimSummary retry{};
        if (!expect(
                reclaim_orphans(&recovered, &retry) == Status::Ok &&
                retry.candidates == 1U && retry.reclaimed == 1U,
                "retry a pre-tombstone interrupted reclaim")) {
            return false;
        }
    }
    return expect_ownership(
        &recovered, inode_id, InodeOwnership::Tombstoned,
        "recovery leaves the orphan tombstoned");
}

bool qualify_interrupted_reclaim(uint64_t inode_id) {
    using namespace fs::kurofs;
    std::memcpy(working_disk, base_orphan_disk, sizeof(working_disk));
    MemoryDevice baseline{
        working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
    storage::block::Device baseline_device = make_device(&baseline);
    FileSystem filesystem{};
    if (mount(&filesystem, &baseline_device) != Status::Ok) return false;
    reset_calls(&baseline);
    OrphanReclaimSummary summary{};
    if (reclaim_orphans(&filesystem, &summary) != Status::Ok ||
        summary.candidates != 1U || summary.reclaimed != 1U ||
        summary.deferred_nonempty_directories != 0U) {
        return false;
    }
    const size_t write_count = baseline.writes;
    const size_t flush_count = baseline.flushes;
    if (!expect(
            write_count != 0U && flush_count != 0U,
            "successful reclaim has persistent phases")) {
        return false;
    }

    for (size_t failure = 0U; failure < write_count; ++failure) {
        std::memcpy(working_disk, base_orphan_disk, sizeof(working_disk));
        MemoryDevice memory{
            working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
        storage::block::Device device = make_device(&memory);
        FileSystem interrupted{};
        if (mount(&interrupted, &device) != Status::Ok) return false;
        reset_calls(&memory);
        memory.fail_write = failure;
        OrphanReclaimSummary interrupted_summary{};
        if (!expect(
                reclaim_orphans(&interrupted, &interrupted_summary) ==
                    Status::BlockDeviceError,
                "surface interrupted orphan-reclaim write") ||
            !recovered_state_is_safe(
                &memory, &device, inode_id,
                "remount after interrupted orphan-reclaim write")) {
            return false;
        }
    }

    for (size_t failure = 0U; failure < flush_count; ++failure) {
        std::memcpy(working_disk, base_orphan_disk, sizeof(working_disk));
        MemoryDevice memory{
            working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
        storage::block::Device device = make_device(&memory);
        FileSystem interrupted{};
        if (mount(&interrupted, &device) != Status::Ok) return false;
        reset_calls(&memory);
        memory.fail_flush = failure;
        OrphanReclaimSummary interrupted_summary{};
        if (!expect(
                reclaim_orphans(&interrupted, &interrupted_summary) ==
                    Status::BlockDeviceError,
                "surface interrupted orphan-reclaim flush") ||
            !recovered_state_is_safe(
                &memory, &device, inode_id,
                "remount after interrupted orphan-reclaim flush")) {
            return false;
        }
    }

    std::printf(
        "KuroFS orphan reclaim recovery: PASS (%zu writes, %zu flushes)\n",
        write_count, flush_count);
    return true;
}

bool qualify_bounded_reclaim_scope() {
    using namespace fs::kurofs;
    std::memset(working_disk, 0x5A, sizeof(working_disk));
    MemoryDevice memory{
        working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
    storage::block::Device device = make_device(&memory);
    FileSystem filesystem{};
    if (format(&device, 32U) != Status::Ok ||
        mount(&filesystem, &device) != Status::Ok) {
        return false;
    }

    uint64_t raw_extent = 0U;
    uint64_t regular_id = 0U;
    uint64_t empty_id = 0U;
    uint64_t tree_id = 0U;
    uint64_t leaf_id = 0U;
    Inode regular{};
    Inode tree{};
    const uint8_t payload[] = {'o', 'r', 'p', 'h', 'a', 'n'};
    if (allocate_blocks(&filesystem, 2U, &raw_extent) != Status::Ok ||
        allocate_inode(&filesystem, InodeType::Regular, &regular_id) !=
            Status::Ok ||
        read_inode(&filesystem, regular_id, &regular) != Status::Ok ||
        write_inode_data(
            &filesystem, &regular, 0U, payload, sizeof(payload)) != Status::Ok ||
        allocate_inode(&filesystem, InodeType::Directory, &empty_id) !=
            Status::Ok ||
        allocate_inode(&filesystem, InodeType::Directory, &tree_id) !=
            Status::Ok ||
        allocate_inode(&filesystem, InodeType::Regular, &leaf_id) !=
            Status::Ok ||
        read_inode(&filesystem, tree_id, &tree) != Status::Ok ||
        directory_append(&filesystem, &tree, "leaf", leaf_id) != Status::Ok) {
        return false;
    }

    FileSystem normalized{};
    if (!expect(mount(&normalized, &device) == Status::Ok,
                "mount bounded orphan fixture") ||
        !expect_ownership(
            &normalized, regular_id, InodeOwnership::Orphan,
            "regular reservation becomes orphan") ||
        !expect_ownership(
            &normalized, empty_id, InodeOwnership::Orphan,
            "empty directory reservation becomes orphan") ||
        !expect_ownership(
            &normalized, tree_id, InodeOwnership::Orphan,
            "detached tree root becomes orphan") ||
        !expect_ownership(
            &normalized, leaf_id, InodeOwnership::Live,
            "detached tree child retains local owner")) {
        return false;
    }

    OrphanReclaimSummary summary{};
    if (!expect(
            reclaim_orphans(&normalized, &summary) == Status::Ok &&
            summary.candidates == 3U && summary.reclaimed == 2U &&
            summary.deferred_nonempty_directories == 1U,
            "reclaim only regular and empty-directory orphans") ||
        !expect_ownership(
            &normalized, regular_id, InodeOwnership::Tombstoned,
            "regular orphan is tombstoned") ||
        !expect_ownership(
            &normalized, empty_id, InodeOwnership::Tombstoned,
            "empty orphan directory is tombstoned") ||
        !expect_ownership(
            &normalized, tree_id, InodeOwnership::Orphan,
            "non-empty orphan tree is deferred") ||
        !expect(
            bitmap_is_set(working_disk, normalized.geometry, raw_extent) &&
            bitmap_is_set(working_disk, normalized.geometry, raw_extent + 1U),
            "raw unowned block reservation remains untouched")) {
        return false;
    }

    summary = {};
    if (!expect(
            reclaim_orphans(&normalized, &summary) == Status::Ok &&
            summary.candidates == 1U && summary.reclaimed == 0U &&
            summary.deferred_nonempty_directories == 1U,
            "repeated reclaim keeps non-empty tree deferred")) {
        return false;
    }

    Inode current_tree{};
    if (!expect(
            read_inode(&normalized, tree_id, &current_tree) == Status::Ok &&
            directory_remove(&normalized, &current_tree, "leaf") == Status::Ok,
            "empty the deferred orphan tree through normal metadata rules")) {
        return false;
    }
    summary = {};
    if (!expect(
            reclaim_orphans(&normalized, &summary) == Status::Ok &&
            summary.candidates == 1U && summary.reclaimed == 1U &&
            summary.deferred_nonempty_directories == 0U,
            "reclaim orphan tree after it becomes empty") ||
        !expect_ownership(
            &normalized, tree_id, InodeOwnership::Tombstoned,
            "emptied orphan tree is tombstoned") ||
        !expect_ownership(
            &normalized, leaf_id, InodeOwnership::Tombstoned,
            "removed orphan-tree child is tombstoned") ||
        !expect(
            bitmap_is_set(working_disk, normalized.geometry, raw_extent) &&
            bitmap_is_set(working_disk, normalized.geometry, raw_extent + 1U),
            "reclaim never consumes ambiguous raw reservations")) {
        return false;
    }

    FileSystem remounted{};
    return expect(
        mount(&remounted, &device) == Status::Ok &&
        validate_consistency(&remounted) == Status::Ok,
        "bounded orphan reclaim persists across remount");
}

} // namespace

int main() {
    uint64_t inode_id = 0U;
    if (!expect(
            build_single_orphan(&inode_id),
            "build single-orphan recovery fixture") ||
        !qualify_interrupted_reclaim(inode_id) ||
        !qualify_bounded_reclaim_scope()) {
        return 1;
    }
    std::puts("KuroFS bounded orphan reclamation tests passed");
    return 0;
}
