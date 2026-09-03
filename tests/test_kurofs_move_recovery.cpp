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
uint8_t base_file_disk[DISK_SIZE]{};
uint8_t base_directory_disk[DISK_SIZE]{};

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

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

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

void downgrade_move_feature(uint8_t* disk) {
    for (uint64_t superblock = 0U; superblock < 2U; ++superblock) {
        uint8_t* const sector = disk + superblock * SECTOR_SIZE;
        std::memset(sector + 88U, 0, sizeof(uint64_t));
        store_u32(
            sector + SECTOR_SIZE - sizeof(uint32_t),
            crc32(sector, SECTOR_SIZE - sizeof(uint32_t)));
    }
}

bool lookup_inode(
    fs::kurofs::FileSystem* filesystem,
    const fs::kurofs::Inode& parent,
    const char* name,
    fs::kurofs::Inode* output) {
    fs::kurofs::DirectoryEntry entry{};
    if (fs::kurofs::directory_lookup(
            filesystem, &parent, name, &entry) != fs::kurofs::Status::Ok) {
        return false;
    }
    return fs::kurofs::read_inode(filesystem, entry.inode_id, output) ==
            fs::kurofs::Status::Ok &&
        output->generation == entry.inode_generation &&
        output->type == entry.type;
}

bool make_base_image(bool directory_child, uint8_t* output) {
    std::memset(working_disk, 0xA5, sizeof(working_disk));
    MemoryDevice memory{
        working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
    storage::block::Device device = make_device(&memory);
    using namespace fs::kurofs;
    if (format(&device, 32U) != Status::Ok) return false;
    FileSystem filesystem{};
    Inode root{};
    Inode source{};
    Inode destination{};
    Inode child{};
    if (mount(&filesystem, &device) != Status::Ok ||
        read_inode(&filesystem, ROOT_INODE, &root) != Status::Ok ||
        directory_create(
            &filesystem, &root, "source", InodeType::Directory,
            &source) != Status::Ok ||
        directory_create(
            &filesystem, &root, "destination", InodeType::Directory,
            &destination) != Status::Ok ||
        directory_create(
            &filesystem, &source, "item",
            directory_child ? InodeType::Directory : InodeType::Regular,
            &child) != Status::Ok) {
        return false;
    }
    if (directory_child) {
        Inode leaf{};
        if (directory_create(
                &filesystem, &child, "leaf", InodeType::Regular,
                &leaf) != Status::Ok) {
            return false;
        }
    } else {
        static const uint8_t payload[] = {'s', 't', 'e', 'e', 'l'};
        if (write_inode_data(
                &filesystem, &child, 0U,
                payload, sizeof(payload)) != Status::Ok) {
            return false;
        }
    }
    std::memcpy(output, working_disk, sizeof(working_disk));
    return true;
}

bool extents_overlap(
    const fs::kurofs::Inode& left,
    const fs::kurofs::Inode& right) {
    if (left.extent_blocks == 0U || right.extent_blocks == 0U) return false;
    const uint64_t left_end = left.extent_start + left.extent_blocks;
    const uint64_t right_end = right.extent_start + right.extent_blocks;
    return left.extent_start < right_end && right.extent_start < left_end;
}

bool validate_live_extent_ownership(fs::kurofs::FileSystem* filesystem) {
    using namespace fs::kurofs;
    for (uint64_t left_id = ROOT_INODE;
         left_id <= filesystem->geometry.inode_count;
         ++left_id) {
        Inode left{};
        Status status = read_inode(filesystem, left_id, &left);
        if (status == Status::NotFound) continue;
        if (status != Status::Ok) return false;
        for (uint64_t right_id = left_id + 1U;
             right_id <= filesystem->geometry.inode_count;
             ++right_id) {
            Inode right{};
            status = read_inode(filesystem, right_id, &right);
            if (status == Status::NotFound) continue;
            if (status != Status::Ok || extents_overlap(left, right)) {
                return false;
            }
        }
    }
    return true;
}

bool verify_recovered_namespace(
    storage::block::Device* device,
    bool directory_child) {
    using namespace fs::kurofs;
    FileSystem filesystem{};
    Inode root{};
    Inode source{};
    Inode destination{};
    if (mount(&filesystem, device) != Status::Ok ||
        read_inode(&filesystem, ROOT_INODE, &root) != Status::Ok ||
        !lookup_inode(&filesystem, root, "source", &source) ||
        !lookup_inode(&filesystem, root, "destination", &destination)) {
        return false;
    }
    DirectoryEntry old_entry{};
    DirectoryEntry new_entry{};
    const Status old_status = directory_lookup(
        &filesystem, &source, "item", &old_entry);
    const Status new_status = directory_lookup(
        &filesystem, &destination, "moved", &new_entry);
    if (!((old_status == Status::Ok && new_status == Status::NotFound) ||
          (old_status == Status::NotFound && new_status == Status::Ok))) {
        return false;
    }
    const DirectoryEntry& live = old_status == Status::Ok
        ? old_entry : new_entry;
    Inode child{};
    if (read_inode(&filesystem, live.inode_id, &child) != Status::Ok ||
        child.generation != live.inode_generation ||
        child.type != live.type ||
        child.type != (directory_child
            ? InodeType::Directory : InodeType::Regular)) {
        return false;
    }
    if (directory_child) {
        DirectoryEntry leaf{};
        if (directory_lookup(&filesystem, &child, "leaf", &leaf) !=
            Status::Ok) {
            return false;
        }
    } else {
        uint8_t payload[5]{};
        size_t read = 0U;
        if (read_inode_data(
                &filesystem, &child, 0U,
                payload, sizeof(payload), &read) != Status::Ok ||
            read != sizeof(payload) ||
            std::memcmp(payload, "steel", sizeof(payload)) != 0) {
            return false;
        }
    }
    return validate_live_extent_ownership(&filesystem);
}

bool run_move(
    storage::block::Device* device,
    fs::kurofs::Status* out_status) {
    using namespace fs::kurofs;
    FileSystem filesystem{};
    Inode root{};
    Inode source{};
    Inode destination{};
    if (mount(&filesystem, device) != Status::Ok ||
        read_inode(&filesystem, ROOT_INODE, &root) != Status::Ok ||
        !lookup_inode(&filesystem, root, "source", &source) ||
        !lookup_inode(&filesystem, root, "destination", &destination)) {
        return false;
    }
    *out_status = directory_move(
        &filesystem, &source, "item", &destination, "moved");
    return true;
}

bool qualify_failure_points(bool directory_child) {
    using fs::kurofs::Status;
    const uint8_t* const base = directory_child
        ? base_directory_disk : base_file_disk;

    std::memcpy(working_disk, base, sizeof(working_disk));
    MemoryDevice measure{
        working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
    storage::block::Device measure_device = make_device(&measure);
    Status move_status = Status::InvalidArgument;
    if (!run_move(&measure_device, &move_status) || move_status != Status::Ok ||
        !verify_recovered_namespace(&measure_device, directory_child)) {
        return false;
    }
    const size_t write_count = measure.writes;
    const size_t flush_count = measure.flushes;

    for (size_t failure = 0U; failure < write_count; ++failure) {
        std::memcpy(working_disk, base, sizeof(working_disk));
        MemoryDevice memory{
            working_disk, 0U, 0U, failure, NEVER_FAIL};
        storage::block::Device device = make_device(&memory);
        move_status = Status::InvalidArgument;
        if (!run_move(&device, &move_status) ||
            move_status != Status::BlockDeviceError) {
            std::fprintf(stderr, "write failure point %zu was not reported\n", failure);
            return false;
        }
        memory.fail_write = NEVER_FAIL;
        if (!verify_recovered_namespace(&device, directory_child)) {
            std::fprintf(stderr, "write recovery failed at point %zu\n", failure);
            return false;
        }
    }

    for (size_t failure = 0U; failure < flush_count; ++failure) {
        std::memcpy(working_disk, base, sizeof(working_disk));
        MemoryDevice memory{
            working_disk, 0U, 0U, NEVER_FAIL, failure};
        storage::block::Device device = make_device(&memory);
        move_status = Status::InvalidArgument;
        if (!run_move(&device, &move_status) ||
            move_status != Status::BlockDeviceError) {
            std::fprintf(stderr, "flush failure point %zu was not reported\n", failure);
            return false;
        }
        memory.fail_flush = NEVER_FAIL;
        if (!verify_recovered_namespace(&device, directory_child)) {
            std::fprintf(stderr, "flush recovery failed at point %zu\n", failure);
            return false;
        }
    }

    std::printf(
        "KuroFS %s move recovery: PASS (%zu writes, %zu flushes)\n",
        directory_child ? "directory" : "file",
        write_count,
        flush_count);
    return true;
}

bool qualify_legacy_feature_upgrade() {
    using namespace fs::kurofs;
    std::memcpy(working_disk, base_file_disk, sizeof(working_disk));
    downgrade_move_feature(working_disk);
    MemoryDevice memory{
        working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
    storage::block::Device device = make_device(&memory);
    FileSystem before{};
    if (mount(&before, &device) != Status::Ok ||
        before.geometry.feature_flags != FEATURE_INODE_OWNERSHIP ||
        before.geometry.generation != 2U) {
        return false;
    }
    Status move_status = Status::InvalidArgument;
    if (!run_move(&device, &move_status) || move_status != Status::Ok ||
        !verify_recovered_namespace(&device, false)) {
        return false;
    }
    FileSystem after{};
    return mount(&after, &device) == Status::Ok &&
        (after.geometry.feature_flags & FEATURE_MOVE_INTENT) != 0U &&
        (after.geometry.feature_flags & FEATURE_INODE_OWNERSHIP) != 0U &&
        after.geometry.generation == 3U;
}

} // namespace

int main() {
    if (!expect(make_base_image(false, base_file_disk),
                "build file-move base image") ||
        !expect(make_base_image(true, base_directory_disk),
                "build directory-move base image") ||
        !expect(qualify_failure_points(false),
                "qualify file move failure points") ||
        !expect(qualify_failure_points(true),
                "qualify directory move failure points") ||
        !expect(qualify_legacy_feature_upgrade(),
                "upgrade legacy KuroFS move feature")) {
        return 1;
    }
    std::puts("KuroFS interrupted move recovery tests passed");
    return 0;
}
