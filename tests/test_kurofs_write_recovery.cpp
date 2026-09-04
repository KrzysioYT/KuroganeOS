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
constexpr size_t PAYLOAD_SIZE = 700U;
constexpr size_t NEVER_FAIL = std::numeric_limits<size_t>::max();

uint8_t working_disk[DISK_SIZE]{};
uint8_t base_disk[DISK_SIZE]{};
uint8_t old_payload[PAYLOAD_SIZE]{};
uint8_t new_payload[PAYLOAD_SIZE]{};

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

bool load_file(
    fs::kurofs::FileSystem* filesystem,
    fs::kurofs::Inode* output) {
    using namespace fs::kurofs;
    Inode root{};
    DirectoryEntry entry{};
    return read_inode(filesystem, ROOT_INODE, &root) == Status::Ok &&
        directory_lookup(filesystem, &root, "file", &entry) == Status::Ok &&
        read_inode(filesystem, entry.inode_id, output) == Status::Ok &&
        output->generation == entry.inode_generation &&
        output->type == InodeType::Regular;
}

bool read_payload(
    fs::kurofs::FileSystem* filesystem,
    const fs::kurofs::Inode& inode,
    uint8_t* output) {
    size_t read = 0U;
    return fs::kurofs::read_inode_data(
               filesystem, &inode, 0U, output, PAYLOAD_SIZE, &read) ==
            fs::kurofs::Status::Ok &&
        read == PAYLOAD_SIZE;
}

bool build_fixture() {
    using namespace fs::kurofs;
    for (size_t index = 0U; index < PAYLOAD_SIZE; ++index) {
        old_payload[index] = static_cast<uint8_t>((index * 13U + 7U) & 0xffU);
        new_payload[index] = static_cast<uint8_t>((index * 29U + 3U) & 0xffU);
    }
    std::memset(working_disk, 0x5A, sizeof(working_disk));
    MemoryDevice memory{
        working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
    storage::block::Device device = make_device(&memory);
    FileSystem filesystem{};
    Inode root{};
    Inode file{};
    if (format(&device, 16U) != Status::Ok ||
        mount(&filesystem, &device) != Status::Ok ||
        read_inode(&filesystem, ROOT_INODE, &root) != Status::Ok ||
        directory_create(
            &filesystem, &root, "file", InodeType::Regular, &file) !=
            Status::Ok ||
        write_inode_data(
            &filesystem, &file, 0U, old_payload, PAYLOAD_SIZE) != Status::Ok) {
        return false;
    }
    FileSystem remounted{};
    Inode persisted{};
    uint8_t payload[PAYLOAD_SIZE]{};
    if (mount(&remounted, &device) != Status::Ok ||
        !load_file(&remounted, &persisted) ||
        !read_payload(&remounted, persisted, payload) ||
        std::memcmp(payload, old_payload, PAYLOAD_SIZE) != 0) {
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
            "interrupted regular write remains consistent")) {
        return false;
    }
    Inode file{};
    uint8_t payload[PAYLOAD_SIZE]{};
    if (!expect(
            load_file(&recovered, &file) &&
                read_payload(&recovered, file, payload),
            "read file after interrupted regular write")) {
        return false;
    }
    const bool old_visible =
        std::memcmp(payload, old_payload, PAYLOAD_SIZE) == 0;
    const bool new_visible =
        std::memcmp(payload, new_payload, PAYLOAD_SIZE) == 0;
    return expect(
        old_visible || new_visible,
        "interrupted regular write exposes complete old or new payload");
}

bool qualify_failures() {
    using namespace fs::kurofs;
    std::memcpy(working_disk, base_disk, sizeof(working_disk));
    MemoryDevice baseline{
        working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
    storage::block::Device baseline_device = make_device(&baseline);
    FileSystem filesystem{};
    Inode file{};
    if (mount(&filesystem, &baseline_device) != Status::Ok ||
        !load_file(&filesystem, &file)) {
        return false;
    }
    reset_calls(&baseline);
    if (write_inode_data(
            &filesystem, &file, 0U, new_payload, PAYLOAD_SIZE) != Status::Ok) {
        return false;
    }
    const size_t write_count = baseline.writes;
    const size_t flush_count = baseline.flushes;
    if (!expect(
            write_count != 0U && flush_count != 0U,
            "successful regular write has persistent phases")) {
        return false;
    }

    for (size_t failure = 0U; failure < write_count; ++failure) {
        std::memcpy(working_disk, base_disk, sizeof(working_disk));
        MemoryDevice memory{
            working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
        storage::block::Device device = make_device(&memory);
        FileSystem interrupted{};
        Inode interrupted_file{};
        if (mount(&interrupted, &device) != Status::Ok ||
            !load_file(&interrupted, &interrupted_file)) {
            return false;
        }
        reset_calls(&memory);
        memory.fail_write = failure;
        if (!expect(
                write_inode_data(
                    &interrupted, &interrupted_file, 0U,
                    new_payload, PAYLOAD_SIZE) == Status::BlockDeviceError,
                "surface interrupted regular-file write") ||
            !remount_is_old_or_new(
                &memory, &device,
                "remount after interrupted regular-file write")) {
            std::fprintf(stderr, "write failure index: %zu\n", failure);
            return false;
        }
    }

    for (size_t failure = 0U; failure < flush_count; ++failure) {
        std::memcpy(working_disk, base_disk, sizeof(working_disk));
        MemoryDevice memory{
            working_disk, 0U, 0U, NEVER_FAIL, NEVER_FAIL};
        storage::block::Device device = make_device(&memory);
        FileSystem interrupted{};
        Inode interrupted_file{};
        if (mount(&interrupted, &device) != Status::Ok ||
            !load_file(&interrupted, &interrupted_file)) {
            return false;
        }
        reset_calls(&memory);
        memory.fail_flush = failure;
        if (!expect(
                write_inode_data(
                    &interrupted, &interrupted_file, 0U,
                    new_payload, PAYLOAD_SIZE) == Status::BlockDeviceError,
                "surface interrupted regular-file flush") ||
            !remount_is_old_or_new(
                &memory, &device,
                "remount after interrupted regular-file flush")) {
            std::fprintf(stderr, "flush failure index: %zu\n", failure);
            return false;
        }
    }

    std::printf(
        "KuroFS regular write recovery: PASS (%zu writes, %zu flushes)\n",
        write_count, flush_count);
    return true;
}

} // namespace

int main() {
    if (!expect(build_fixture(), "build regular-write recovery fixture") ||
        !qualify_failures()) {
        return 1;
    }
    std::puts("KuroFS interrupted regular write tests passed");
    return 0;
}
