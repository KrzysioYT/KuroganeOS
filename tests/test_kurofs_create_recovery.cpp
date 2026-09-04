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
uint8_t base_disk[DISK_SIZE]{};

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

bool build_fixture() {
    using namespace fs::kurofs;
    std::memset(working_disk, 0x5A, sizeof(working_disk));
    MemoryDevice memory{
        working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
    storage::block::Device device = make_device(&memory);
    FileSystem filesystem{};
    if (format(&device, 16U) != Status::Ok ||
        mount(&filesystem, &device) != Status::Ok) {
        return false;
    }
    std::memcpy(base_disk, working_disk, sizeof(base_disk));
    return true;
}

bool remount_is_old_or_new(
    MemoryDevice* memory,
    storage::block::Device* device,
    const char* mount_message) {
    using namespace fs::kurofs;
    reset_calls(memory);
    FileSystem recovered{};
    if (!expect(mount(&recovered, device) == Status::Ok, mount_message) ||
        !expect(
            validate_consistency(&recovered) == Status::Ok,
            "interrupted create remains consistent")) {
        return false;
    }
    Inode root{};
    if (read_inode(&recovered, ROOT_INODE, &root) != Status::Ok) return false;
    DirectoryEntry entry{};
    const Status lookup = directory_lookup(
        &recovered, &root, "created", &entry);
    if (lookup == Status::NotFound) {
        InodeOwnershipSummary ownership{};
        return expect(
            scan_inode_ownership(&recovered, &ownership) == Status::Ok &&
                ownership.live == 1U,
            "pre-publication create leaves only root live");
    }
    if (!expect(lookup == Status::Ok,
                "interrupted create exposes old or new namespace")) {
        return false;
    }
    Inode child{};
    InodeOwnership ownership = InodeOwnership::Free;
    return expect(
        read_inode(&recovered, entry.inode_id, &child) == Status::Ok &&
            child.generation == entry.inode_generation &&
            child.type == InodeType::Regular && child.flags == 0U &&
            inode_ownership(&recovered, child.id, &ownership) == Status::Ok &&
            ownership == InodeOwnership::Live,
        "published create retains a live generation-matched child");
}

bool qualify_failures() {
    using namespace fs::kurofs;
    std::memcpy(working_disk, base_disk, sizeof(working_disk));
    MemoryDevice baseline{
        working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
    storage::block::Device baseline_device = make_device(&baseline);
    FileSystem filesystem{};
    Inode root{};
    Inode child{};
    if (mount(&filesystem, &baseline_device) != Status::Ok ||
        read_inode(&filesystem, ROOT_INODE, &root) != Status::Ok) {
        return false;
    }
    reset_calls(&baseline);
    if (directory_create(
            &filesystem, &root, "created", InodeType::Regular, &child) !=
            Status::Ok) {
        return false;
    }
    const size_t write_count = baseline.writes;
    const size_t flush_count = baseline.flushes;
    if (!expect(
            write_count != 0U && flush_count != 0U,
            "successful create has persistent phases")) {
        return false;
    }

    for (size_t failure = 0U; failure < write_count; ++failure) {
        std::memcpy(working_disk, base_disk, sizeof(working_disk));
        MemoryDevice memory{
            working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
        storage::block::Device device = make_device(&memory);
        FileSystem interrupted{};
        Inode interrupted_root{};
        if (mount(&interrupted, &device) != Status::Ok ||
            read_inode(&interrupted, ROOT_INODE, &interrupted_root) !=
                Status::Ok) {
            return false;
        }
        reset_calls(&memory);
        memory.fail_write = failure;
        Inode interrupted_child{};
        const Status status = directory_create(
            &interrupted, &interrupted_root, "created",
            InodeType::Regular, &interrupted_child);
        if (!expect(
                status == Status::Ok || status == Status::BlockDeviceError,
                "surface or complete interrupted create write") ||
            !remount_is_old_or_new(
                &memory, &device, "remount after interrupted create write")) {
            std::fprintf(stderr, "create write failure index: %zu\n", failure);
            return false;
        }
    }

    for (size_t failure = 0U; failure < flush_count; ++failure) {
        std::memcpy(working_disk, base_disk, sizeof(working_disk));
        MemoryDevice memory{
            working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
        storage::block::Device device = make_device(&memory);
        FileSystem interrupted{};
        Inode interrupted_root{};
        if (mount(&interrupted, &device) != Status::Ok ||
            read_inode(&interrupted, ROOT_INODE, &interrupted_root) !=
                Status::Ok) {
            return false;
        }
        reset_calls(&memory);
        memory.fail_flush = failure;
        Inode interrupted_child{};
        const Status status = directory_create(
            &interrupted, &interrupted_root, "created",
            InodeType::Regular, &interrupted_child);
        if (!expect(
                status == Status::Ok || status == Status::BlockDeviceError,
                "surface or complete interrupted create flush") ||
            !remount_is_old_or_new(
                &memory, &device, "remount after interrupted create flush")) {
            std::fprintf(stderr, "create flush failure index: %zu\n", failure);
            return false;
        }
    }

    std::printf(
        "KuroFS create recovery: PASS (%zu writes, %zu flushes)\n",
        write_count, flush_count);
    return true;
}

} // namespace

int main() {
    if (!expect(build_fixture(), "build create recovery fixture") ||
        !qualify_failures()) {
        return 1;
    }
    std::puts("KuroFS interrupted create tests passed");
    return 0;
}
