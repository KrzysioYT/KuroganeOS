#include "../kernel/fs/fat32.hpp"
#include "../kernel/fs/fat32_vfs.hpp"
#include "../kernel/fs/vfs.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using fs::fat32::DirectoryEntry;
using fs::fat32::EntryType;
using fs::fat32::FileSystem;
using fs::fat32::Geometry;
using fs::fat32::Node;
using fs::fat32::Stat;
using fs::fat32::Status;
using storage::block::Device;
using BlockStatus = storage::block::Status;

constexpr uint32_t SECTOR_SIZE = 512U;
constexpr uint32_t RESERVED_SECTORS = 32U;
constexpr uint32_t FAT_COUNT = 2U;
constexpr uint32_t FAT_SECTORS = 512U;
constexpr uint32_t DATA_CLUSTERS = 65525U;
constexpr uint32_t FIRST_DATA_SECTOR =
    RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS;
constexpr uint32_t TOTAL_SECTORS = FIRST_DATA_SECTOR + DATA_CLUSTERS;
constexpr uint32_t ROOT_CLUSTER = 2U;
constexpr size_t ROOT_BIG_SLOT = 1U;
constexpr size_t ROOT_LFN_SLOT = 2U;
constexpr size_t ROOT_CONFIG_SLOT = 3U;

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "       \
                      << #condition << '\n';                                    \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

void put_u16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value & 0xFFU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value & 0xFFU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    bytes[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    bytes[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

uint8_t checksum_short_name(const uint8_t* name) {
    uint8_t checksum = 0U;
    for (size_t index = 0U; index < 11U; ++index) {
        checksum = static_cast<uint8_t>(
            ((checksum & 1U) != 0U ? 0x80U : 0U) +
            (checksum >> 1U) + name[index]);
    }
    return checksum;
}

struct MemoryBackend {
    std::vector<uint8_t> bytes;
    uint64_t failed_sector = std::numeric_limits<uint64_t>::max();
    BlockStatus failure = BlockStatus::IoError;
};

BlockStatus memory_read(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    void* destination) {
    auto* backend = static_cast<MemoryBackend*>(context);
    if (backend == nullptr || destination == nullptr || block_count == 0U) {
        return BlockStatus::InvalidArgument;
    }
    const uint64_t sector_count = backend->bytes.size() / SECTOR_SIZE;
    if (first_block >= sector_count || block_count > sector_count - first_block) {
        return BlockStatus::OutOfRange;
    }
    if (backend->failed_sector >= first_block &&
        backend->failed_sector - first_block < block_count) {
        return backend->failure;
    }
    const size_t offset = static_cast<size_t>(first_block * SECTOR_SIZE);
    const size_t size = static_cast<size_t>(block_count * SECTOR_SIZE);
    std::memcpy(destination, backend->bytes.data() + offset, size);
    return BlockStatus::Ok;
}

BlockStatus memory_write(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    const void* source) {
    auto* backend = static_cast<MemoryBackend*>(context);
    if (backend == nullptr || source == nullptr || block_count == 0U) {
        return BlockStatus::InvalidArgument;
    }
    const uint64_t sector_count = backend->bytes.size() / SECTOR_SIZE;
    if (first_block >= sector_count || block_count > sector_count - first_block) {
        return BlockStatus::OutOfRange;
    }
    if (backend->failed_sector >= first_block &&
        backend->failed_sector - first_block < block_count) {
        return backend->failure;
    }
    const size_t offset = static_cast<size_t>(first_block * SECTOR_SIZE);
    const size_t size = static_cast<size_t>(block_count * SECTOR_SIZE);
    std::memcpy(backend->bytes.data() + offset, source, size);
    return BlockStatus::Ok;
}

BlockStatus memory_flush(void*) {
    return BlockStatus::Ok;
}

struct TestImage {
    MemoryBackend backend;
    Device device{};

    TestImage() {
        backend.bytes.resize(
            static_cast<size_t>(TOTAL_SECTORS) * SECTOR_SIZE,
            0U);
        device.context = &backend;
        device.sector_size = SECTOR_SIZE;
        device.sector_count = TOTAL_SECTORS;
        device.read = memory_read;
        device.write = memory_write;
        device.flush = memory_flush;
        format();
    }

    uint8_t* sector(uint32_t index) {
        return backend.bytes.data() + static_cast<size_t>(index) * SECTOR_SIZE;
    }

    uint8_t* cluster(uint32_t index) {
        return sector(FIRST_DATA_SECTOR + index - 2U);
    }

    uint8_t* root_entry(size_t slot) {
        return cluster(ROOT_CLUSTER) + slot * 32U;
    }

    void set_fat(uint32_t cluster_index, uint32_t value) {
        for (uint32_t copy = 0U; copy < FAT_COUNT; ++copy) {
            uint8_t* fat = sector(RESERVED_SECTORS + copy * FAT_SECTORS);
            put_u32(fat + static_cast<size_t>(cluster_index) * 4U, value);
        }
    }

    void set_short_entry(
        uint8_t* entry,
        const char name[12],
        uint8_t attributes,
        uint32_t first_cluster,
        uint32_t size) {
        std::memset(entry, 0, 32U);
        std::memcpy(entry, name, 11U);
        entry[11] = attributes;
        put_u16(entry + 20U, static_cast<uint16_t>(first_cluster >> 16U));
        put_u16(entry + 26U, static_cast<uint16_t>(first_cluster & 0xFFFFU));
        put_u32(entry + 28U, size);
    }

    void set_lfn_entry(
        uint8_t* entry,
        const char* ascii_name,
        uint8_t checksum) {
        std::memset(entry, 0xFF, 32U);
        entry[0] = 0x41U;
        entry[11] = 0x0FU;
        entry[12] = 0U;
        entry[13] = checksum;
        put_u16(entry + 26U, 0U);
        constexpr uint8_t offsets[13] = {
            1U, 3U, 5U, 7U, 9U,
            14U, 16U, 18U, 20U, 22U, 24U,
            28U, 30U
        };
        size_t length = std::strlen(ascii_name);
        for (size_t index = 0U; index < 13U; ++index) {
            uint16_t unit = 0xFFFFU;
            if (index < length) {
                unit = static_cast<uint8_t>(ascii_name[index]);
            } else if (index == length) {
                unit = 0U;
            }
            put_u16(entry + offsets[index], unit);
        }
    }

    void format() {
        uint8_t* boot = sector(0U);
        boot[0] = 0xEBU;
        boot[1] = 0x58U;
        boot[2] = 0x90U;
        std::memcpy(boot + 3U, "KUROGANE", 8U);
        put_u16(boot + 11U, SECTOR_SIZE);
        boot[13] = 1U;
        put_u16(boot + 14U, RESERVED_SECTORS);
        boot[16] = FAT_COUNT;
        put_u16(boot + 17U, 0U);
        put_u16(boot + 19U, 0U);
        boot[21] = 0xF8U;
        put_u16(boot + 22U, 0U);
        put_u16(boot + 24U, 63U);
        put_u16(boot + 26U, 255U);
        put_u32(boot + 28U, 0U);
        put_u32(boot + 32U, TOTAL_SECTORS);
        put_u32(boot + 36U, FAT_SECTORS);
        put_u16(boot + 40U, 0U);
        put_u16(boot + 42U, 0U);
        put_u32(boot + 44U, ROOT_CLUSTER);
        put_u16(boot + 48U, 1U);
        put_u16(boot + 50U, 6U);
        boot[64] = 0x80U;
        boot[66] = 0x29U;
        put_u32(boot + 67U, 0x4B55524FU);
        std::memcpy(boot + 71U, "KUROGANE   ", 11U);
        std::memcpy(boot + 82U, "FAT32   ", 8U);
        boot[510] = 0x55U;
        boot[511] = 0xAAU;
        std::memcpy(sector(6U), boot, SECTOR_SIZE);

        uint8_t* fs_info = sector(1U);
        put_u32(fs_info, 0x41615252U);
        put_u32(fs_info + 484U, 0x61417272U);
        put_u32(fs_info + 488U, 0xFFFFFFFFU);
        put_u32(fs_info + 492U, 0xFFFFFFFFU);
        put_u32(fs_info + 508U, 0xAA550000U);

        set_fat(0U, 0x0FFFFFF8U);
        set_fat(1U, 0x0FFFFFFFU);
        set_fat(2U, 0x0FFFFFFFU);
        set_fat(3U, 4U);
        set_fat(4U, 0x0FFFFFFFU);
        set_fat(5U, 0x0FFFFFFFU);
        set_fat(6U, 0x0FFFFFFFU);
        set_fat(7U, 0x0FFFFFFFU);

        set_short_entry(root_entry(0U), "KUROGANE   ", 0x08U, 0U, 0U);
        set_short_entry(root_entry(ROOT_BIG_SLOT), "BIG     BIN", 0x20U, 3U, 700U);
        set_short_entry(
            root_entry(ROOT_CONFIG_SLOT),
            "SYSTEM~1CNF",
            0x20U,
            6U,
            20U);
        set_lfn_entry(
            root_entry(ROOT_LFN_SLOT),
            "system.conf",
            checksum_short_name(root_entry(ROOT_CONFIG_SLOT)));
        set_short_entry(root_entry(4U), "ETC        ", 0x10U, 5U, 0U);
        std::memset(root_entry(5U), 0, 32U);
        root_entry(5U)[0] = 0xE5U;
        root_entry(6U)[0] = 0U;

        set_short_entry(cluster(5U), ".          ", 0x10U, 5U, 0U);
        // FAT32 represents a parent that is the root directory with cluster
        // zero in the on-disk '..' entry (as mkfs.fat/mtools do).
        set_short_entry(cluster(5U) + 32U, "..         ", 0x10U, 0U, 0U);
        set_short_entry(
            cluster(5U) + 64U,
            "HELLO   TXT",
            0x20U,
            7U,
            5U);
        cluster(5U)[96U] = 0U;

        for (size_t index = 0U; index < 700U; ++index) {
            const uint8_t value = static_cast<uint8_t>(index % 251U);
            if (index < SECTOR_SIZE) {
                cluster(3U)[index] = value;
            } else {
                cluster(4U)[index - SECTOR_SIZE] = value;
            }
        }
        std::memcpy(cluster(6U), "console=true\nlang=en", 20U);
        std::memcpy(cluster(7U), "hello", 5U);
    }
};

void expect_mount_and_geometry(TestImage& image, FileSystem* filesystem) {
    CHECK(fs::fat32::mount(filesystem, &image.device) == Status::Ok);
    CHECK(fs::fat32::mounted(filesystem));
    Geometry geometry{};
    CHECK(fs::fat32::get_geometry(filesystem, &geometry) == Status::Ok);
    CHECK(geometry.bytes_per_sector == SECTOR_SIZE);
    CHECK(geometry.sectors_per_cluster == 1U);
    CHECK(geometry.first_data_sector == FIRST_DATA_SECTOR);
    CHECK(geometry.cluster_count == DATA_CLUSTERS);
    CHECK(geometry.root_cluster == ROOT_CLUSTER);
    CHECK(geometry.fat_mirroring);
    CHECK(std::string(fs::fat32::volume_label(filesystem)) == "KUROGANE");
}

void test_mount_lookup_read_and_stat(TestImage& image) {
    FileSystem filesystem{};
    expect_mount_and_geometry(image, &filesystem);

    Node node{};
    CHECK(fs::fat32::lookup(&filesystem, "/", &node) == Status::Ok);
    CHECK(node.type == EntryType::Directory);
    CHECK(node.first_cluster == ROOT_CLUSTER);
    CHECK(fs::fat32::lookup(
              &filesystem,
              "/./etc/../SYSTEM.CONF",
              &node) == Status::Ok);
    CHECK(node.type == EntryType::File);
    CHECK(node.size == 20U);
    CHECK(fs::fat32::lookup(&filesystem, "../../BIG.BIN", &node) ==
          Status::PathEscapesRoot);
    CHECK(fs::fat32::lookup(&filesystem, "/BIG.BIN/child", &node) ==
          Status::NotDirectory);
    CHECK(fs::fat32::lookup(
              &filesystem,
              "/etc/./hello.txt",
              &node) == Status::Ok);
    CHECK(node.size == 5U);

    Stat info{};
    CHECK(fs::fat32::stat(&filesystem, "/big.bin", &info) == Status::Ok);
    CHECK(info.type == EntryType::File);
    CHECK(info.size == 700U);
    CHECK(info.first_cluster == 3U);

    uint8_t output[64]{};
    size_t bytes_read = 999U;
    CHECK(fs::fat32::read(
              &filesystem,
              "/BIG.BIN",
              500U,
              output,
              sizeof(output),
              &bytes_read) == Status::Ok);
    CHECK(bytes_read == sizeof(output));
    for (size_t index = 0U; index < bytes_read; ++index) {
        CHECK(output[index] == static_cast<uint8_t>((500U + index) % 251U));
    }

    uint8_t tail[64]{};
    CHECK(fs::fat32::read(
              &filesystem,
              "/big.bin",
              680U,
              tail,
              sizeof(tail),
              &bytes_read) == Status::Ok);
    CHECK(bytes_read == 20U);
    CHECK(fs::fat32::read(
              &filesystem,
              "/big.bin",
              std::numeric_limits<uint64_t>::max(),
              nullptr,
              0U,
              &bytes_read) == Status::Ok);
    CHECK(bytes_read == 0U);

    char config[32]{};
    CHECK(fs::fat32::read(
              &filesystem,
              "system.conf",
              0U,
              config,
              sizeof(config),
              &bytes_read) == Status::Ok);
    CHECK(bytes_read == 20U);
    CHECK(std::string(config, bytes_read) == "console=true\nlang=en");

    char hello[8]{};
    CHECK(fs::fat32::read(
              &filesystem,
              "/ETC/HELLO.TXT",
              0U,
              hello,
              sizeof(hello),
              &bytes_read) == Status::Ok);
    CHECK(bytes_read == 5U);
    CHECK(std::string(hello, bytes_read) == "hello");

    CHECK(fs::fat32::sync(&filesystem) == Status::Ok);
    CHECK(fs::fat32::write(
              &filesystem,
              "/big.bin",
              0U,
              output,
              sizeof(output)) == Status::Ok);
}

void test_vfs_adapter(TestImage& image) {
    FileSystem filesystem{};
    expect_mount_and_geometry(image, &filesystem);

    fs::fat32_vfs::Adapter adapter{};
    fs::vfs::FileSystem backend{};
    CHECK(fs::fat32_vfs::initialize(
              &adapter, &filesystem, &backend) == fs::vfs::Status::Ok);
    CHECK(!backend.read_only);

    fs::vfs::State state{};
    CHECK(fs::vfs::initialize(&state, &backend) == fs::vfs::Status::Ok);
    fs::vfs::PathContext context{};
    CHECK(fs::vfs::initialize_path_context(&state, &context) ==
          fs::vfs::Status::Ok);

    fs::vfs::FileStat info{};
    CHECK(fs::vfs::stat(&state, &context, "/system.conf", &info) ==
          fs::vfs::Status::Ok);
    CHECK(info.type == fs::vfs::NodeType::Regular);
    CHECK(info.size == 20U);
    CHECK(fs::vfs::has_flag(info.flags, fs::vfs::NodeFlags::Seekable));

    fs::vfs::OpenFileHandle file{};
    CHECK(fs::vfs::open(
              &state,
              &context,
              "/system.conf",
              fs::vfs::OpenFlags::Read,
              &file) == fs::vfs::Status::Ok);
    char contents[32]{};
    size_t bytes_read = 0U;
    CHECK(fs::vfs::read(
              &state,
              file,
              contents,
              sizeof(contents),
              &bytes_read) == fs::vfs::Status::Ok);
    CHECK(bytes_read == 20U);
    CHECK(std::string(contents, bytes_read) == "console=true\nlang=en");
    uint64_t offset = 0U;
    CHECK(fs::vfs::seek(
              &state,
              file,
              -2,
              fs::vfs::SeekOrigin::End,
              &offset) == fs::vfs::Status::Ok);
    CHECK(offset == 18U);
    CHECK(fs::vfs::close(&state, file) == fs::vfs::Status::Ok);
    CHECK(fs::vfs::close(&state, file) == fs::vfs::Status::StaleHandle);

    fs::vfs::OpenFileHandle directory{};
    CHECK(fs::vfs::open(
              &state,
              &context,
              "/",
              fs::vfs::OpenFlags::Read | fs::vfs::OpenFlags::Directory,
              &directory) == fs::vfs::Status::Ok);
    fs::vfs::DirectoryEntry entry{};
    CHECK(fs::vfs::readdir(&state, directory, &entry) ==
          fs::vfs::Status::Ok);
    CHECK(std::string(entry.name) == "BIG.BIN");
    CHECK(fs::vfs::close(&state, directory) == fs::vfs::Status::Ok);

    fs::vfs::OpenFileHandle write_handle{};
    CHECK(fs::vfs::open(
              &state,
              &context,
              "/system.conf",
              fs::vfs::OpenFlags::Write,
              &write_handle) == fs::vfs::Status::Ok);
    CHECK(fs::vfs::close(&state, write_handle) == fs::vfs::Status::Ok);
    CHECK(fs::vfs::sync_all(&state) == fs::vfs::Status::Ok);
}

void test_mutation_and_persistence(TestImage& image) {
    FileSystem filesystem{};
    expect_mount_and_geometry(image, &filesystem);

    CHECK(fs::fat32::create(&filesystem, "/etc/persist.dat") == Status::Ok);
    CHECK(fs::fat32::create(&filesystem, "/etc/persist.dat") ==
          Status::AlreadyExists);
    uint8_t payload[700]{};
    for (size_t index = 0U; index < sizeof(payload); ++index) {
        payload[index] = static_cast<uint8_t>((index * 7U) & 0xFFU);
    }
    CHECK(fs::fat32::write(
              &filesystem,
              "/etc/persist.dat",
              0U,
              payload,
              sizeof(payload)) == Status::Ok);
    CHECK(fs::fat32::sync(&filesystem) == Status::Ok);

    FileSystem remounted{};
    CHECK(fs::fat32::mount(&remounted, &image.device) == Status::Ok);
    uint8_t restored[700]{};
    size_t bytes_read = 0U;
    CHECK(fs::fat32::read(
              &remounted,
              "/etc/PERSIST.DAT",
              0U,
              restored,
              sizeof(restored),
              &bytes_read) == Status::Ok);
    CHECK(bytes_read == sizeof(restored));
    CHECK(std::memcmp(payload, restored, sizeof(payload)) == 0);

    CHECK(fs::fat32::rename(
              &remounted,
              "/etc/persist.dat",
              "/etc/saved.dat") == Status::Ok);
    CHECK(fs::fat32::mkdir(&remounted, "/etc/testdir") == Status::Ok);
    CHECK(fs::fat32::create(&remounted, "/etc/testdir/child.txt") ==
          Status::Ok);
    CHECK(fs::fat32::rmdir(&remounted, "/etc/testdir") ==
          Status::DirectoryNotEmpty);
    CHECK(fs::fat32::unlink(&remounted, "/etc/testdir/child.txt") ==
          Status::Ok);
    CHECK(fs::fat32::rmdir(&remounted, "/etc/testdir") == Status::Ok);
    CHECK(fs::fat32::unlink(&remounted, "/etc/saved.dat") == Status::Ok);
    CHECK(fs::fat32::lookup(&remounted, "/etc/saved.dat", nullptr) ==
          Status::InvalidArgument);
    Node missing{};
    CHECK(fs::fat32::lookup(&remounted, "/etc/saved.dat", &missing) ==
          Status::NotFound);
}

void test_readdir_and_lfn_contract(TestImage& image) {
    FileSystem filesystem{};
    CHECK(fs::fat32::mount(&filesystem, &image.device) == Status::Ok);

    uint64_t cookie = 0U;
    DirectoryEntry entry{};
    CHECK(fs::fat32::readdir(&filesystem, "/", &cookie, &entry) == Status::Ok);
    CHECK(std::string(entry.name) == "BIG.BIN");
    CHECK(cookie == 1U);
    CHECK(fs::fat32::readdir(&filesystem, "/", &cookie, &entry) == Status::Ok);
    CHECK(std::string(entry.name) == "system.conf");
    CHECK(cookie == 2U);
    CHECK(fs::fat32::readdir(&filesystem, "/", &cookie, &entry) == Status::Ok);
    CHECK(std::string(entry.name) == "ETC");
    CHECK(cookie == 3U);
    CHECK(fs::fat32::readdir(&filesystem, "/", &cookie, &entry) ==
          Status::EndOfDirectory);
    CHECK(cookie == 3U);

    const uint8_t original_checksum = image.root_entry(ROOT_LFN_SLOT)[13];
    image.root_entry(ROOT_LFN_SLOT)[13] ^= 0x5AU;
    Node node{};
    CHECK(fs::fat32::lookup(&filesystem, "/system.conf", &node) ==
          Status::NotFound);
    CHECK(fs::fat32::lookup(&filesystem, "/SYSTEM~1.CNF", &node) == Status::Ok);
    cookie = 1U;
    CHECK(fs::fat32::readdir(&filesystem, "/", &cookie, &entry) == Status::Ok);
    CHECK(std::string(entry.name) == "SYSTEM~1.CNF");
    image.root_entry(ROOT_LFN_SLOT)[13] = original_checksum;

    // A valid non-ASCII UTF-16 name is not silently replaced in readdir: the
    // caller gets an explicit status and an advanced cookie.  Its short alias
    // remains usable.  This backend intentionally does not claim Unicode yet.
    uint8_t* lfn = image.root_entry(ROOT_LFN_SLOT);
    const uint16_t original_first_unit = static_cast<uint16_t>(lfn[1]) |
        static_cast<uint16_t>(static_cast<uint16_t>(lfn[2]) << 8U);
    put_u16(lfn + 1U, 0x0105U);
    cookie = 1U;
    CHECK(fs::fat32::readdir(&filesystem, "/", &cookie, &entry) ==
          Status::UnsupportedNameEncoding);
    CHECK(cookie == 2U);
    CHECK(fs::fat32::lookup(&filesystem, "/SYSTEM~1.CNF", &node) == Status::Ok);

    // A malformed surrogate is rejected as an LFN and falls back to 8.3.
    put_u16(lfn + 1U, 0xD800U);
    put_u16(lfn + 3U, static_cast<uint16_t>('y'));
    cookie = 1U;
    CHECK(fs::fat32::readdir(&filesystem, "/", &cookie, &entry) == Status::Ok);
    CHECK(std::string(entry.name) == "SYSTEM~1.CNF");
    put_u16(lfn + 1U, original_first_unit);
    put_u16(lfn + 3U, static_cast<uint16_t>('y'));
}

void test_metadata_validation_and_transactionality(TestImage& image) {
    FileSystem filesystem{};
    CHECK(fs::fat32::mount(&filesystem, &image.device) == Status::Ok);

    uint8_t* mirror = image.sector(RESERVED_SECTORS + FAT_SECTORS);
    mirror[100U] ^= 0x01U;
    FileSystem rejected{};
    CHECK(fs::fat32::mount(&rejected, &image.device) ==
          Status::FatMirrorMismatch);
    mirror[100U] ^= 0x01U;

    uint8_t* fs_info = image.sector(1U);
    const uint32_t old_signature = 0x41615252U;
    put_u32(fs_info, 0U);
    CHECK(fs::fat32::mount(&rejected, &image.device) == Status::CorruptFsInfo);
    put_u32(fs_info, old_signature);
    put_u32(fs_info + 488U, DATA_CLUSTERS + 1U);
    CHECK(fs::fat32::mount(&rejected, &image.device) == Status::CorruptFsInfo);
    put_u32(fs_info + 488U, 0xFFFFFFFFU);

    uint8_t* backup = image.sector(6U);
    backup[13] = 2U;
    CHECK(fs::fat32::mount(&rejected, &image.device) ==
          Status::CorruptBackupBoot);
    backup[13] = 1U;

    // With mirroring explicitly disabled, only the selected active FAT is
    // authoritative and divergence from an inactive copy is legal.
    uint8_t* primary = image.sector(0U);
    put_u16(primary + 40U, 0x0081U);
    put_u16(backup + 40U, 0x0081U);
    image.sector(RESERVED_SECTORS)[100U] ^= 0x02U;
    CHECK(fs::fat32::mount(&rejected, &image.device) == Status::Ok);
    CHECK(!rejected.geometry.fat_mirroring);
    CHECK(rejected.geometry.active_fat == 1U);
    image.sector(RESERVED_SECTORS)[100U] ^= 0x02U;
    put_u16(primary + 40U, 0U);
    put_u16(backup + 40U, 0U);

    Device wrong_sector_size = image.device;
    wrong_sector_size.sector_size = 4096U;
    CHECK(fs::fat32::mount(&rejected, &wrong_sector_size) ==
          Status::UnsupportedSectorSize);

    put_u32(primary + 32U, FIRST_DATA_SECTOR);
    put_u32(backup + 32U, FIRST_DATA_SECTOR);
    CHECK(fs::fat32::mount(&rejected, &image.device) ==
          Status::UnsupportedGeometry);
    put_u32(primary + 32U, TOTAL_SECTORS);
    put_u32(backup + 32U, TOTAL_SECTORS);

    FileSystem sentinel{};
    sentinel.device = &image.device;
    sentinel.is_mounted = true;
    std::memcpy(sentinel.volume_label, "KEEP", 5U);
    const FileSystem before = sentinel;
    image.backend.failed_sector = 0U;
    CHECK(fs::fat32::mount(&sentinel, &image.device) ==
          Status::BlockDeviceError);
    CHECK(std::memcmp(&sentinel, &before, sizeof(FileSystem)) == 0);
    image.backend.failed_sector = std::numeric_limits<uint64_t>::max();
}

void test_chain_corruption_and_backend_errors(TestImage& image) {
    FileSystem filesystem{};
    CHECK(fs::fat32::mount(&filesystem, &image.device) == Status::Ok);
    std::vector<uint8_t> output(1700U, 0U);
    size_t bytes_read = 0U;

    image.set_fat(3U, 4U);
    image.set_fat(4U, 3U);
    put_u32(image.root_entry(ROOT_BIG_SLOT) + 28U, 1600U);
    CHECK(fs::fat32::read(
              &filesystem,
              "/BIG.BIN",
              0U,
              output.data(),
              output.size(),
              &bytes_read) == Status::ChainCycle);

    image.set_fat(3U, 0x0FFFFFF7U);
    image.set_fat(4U, 0x0FFFFFFFU);
    put_u32(image.root_entry(ROOT_BIG_SLOT) + 28U, 700U);
    CHECK(fs::fat32::read(
              &filesystem,
              "/BIG.BIN",
              0U,
              output.data(),
              output.size(),
              &bytes_read) == Status::CorruptChain);

    image.set_fat(3U, 0U);
    CHECK(fs::fat32::read(
              &filesystem,
              "/BIG.BIN",
              0U,
              output.data(),
              output.size(),
              &bytes_read) == Status::CorruptChain);

    image.set_fat(3U, 0x0FFFFFF0U);
    CHECK(fs::fat32::read(
              &filesystem,
              "/BIG.BIN",
              0U,
              output.data(),
              output.size(),
              &bytes_read) == Status::CorruptChain);

    image.set_fat(3U, 0x0FFFFFFFU);
    CHECK(fs::fat32::read(
              &filesystem,
              "/BIG.BIN",
              0U,
              output.data(),
              output.size(),
              &bytes_read) == Status::TruncatedChain);

    image.set_fat(3U, 4U);
    image.set_fat(4U, 0x0FFFFFFFU);
    image.backend.failed_sector = FIRST_DATA_SECTOR + 2U;
    CHECK(fs::fat32::read(
              &filesystem,
              "/BIG.BIN",
              600U,
              output.data(),
              20U,
              &bytes_read) == Status::BlockDeviceError);
    image.backend.failed_sector = std::numeric_limits<uint64_t>::max();

    // A post-mount mirror split is detected on the touched FAT sector.
    image.sector(RESERVED_SECTORS + FAT_SECTORS)[12U] ^= 0x01U;
    CHECK(fs::fat32::read(
              &filesystem,
              "/BIG.BIN",
              500U,
              output.data(),
              30U,
              &bytes_read) == Status::FatMirrorMismatch);
    image.sector(RESERVED_SECTORS + FAT_SECTORS)[12U] ^= 0x01U;

    // Directory traversal has the same bounded cycle detection.  Remove the
    // end marker temporarily so lookup must follow the root chain.
    for (size_t slot = 6U; slot < 16U; ++slot) {
        image.root_entry(slot)[0] = 0xE5U;
    }
    image.set_fat(ROOT_CLUSTER, ROOT_CLUSTER);
    Node missing{};
    CHECK(fs::fat32::lookup(&filesystem, "/ABSENT.TXT", &missing) ==
          Status::ChainCycle);
    image.set_fat(ROOT_CLUSTER, 0x0FFFFFFFU);
    for (size_t slot = 6U; slot < 16U; ++slot) {
        image.root_entry(slot)[0] = 0U;
    }

    // Very large offsets are handled by subtraction/EOF checks, never by a
    // wrapping offset+length calculation.
    CHECK(fs::fat32::read(
              &filesystem,
              "/BIG.BIN",
              std::numeric_limits<uint64_t>::max(),
              nullptr,
              0U,
              &bytes_read) == Status::Ok);
    CHECK(bytes_read == 0U);
}

void test_public_formatter(TestImage& image) {
    std::fill(image.backend.bytes.begin(), image.backend.bytes.end(), 0U);
    CHECK(fs::fat32::format(&image.device, "KURO_TEST", 1U, 2048U) ==
          Status::Ok);
    FileSystem filesystem{};
    CHECK(fs::fat32::mount(&filesystem, &image.device) == Status::Ok);
    CHECK(std::string(fs::fat32::volume_label(&filesystem)) == "KURO_TEST");
    CHECK(fs::fat32::mkdir(&filesystem, "/ETC") == Status::Ok);
    CHECK(fs::fat32::create(&filesystem, "/ETC/BOOT.CFG") == Status::Ok);
    constexpr char payload[] = "DEFAULT=console\n";
    CHECK(fs::fat32::write(
              &filesystem, "/ETC/BOOT.CFG", 0U,
              payload, sizeof(payload) - 1U) == Status::Ok);
    CHECK(fs::fat32::sync(&filesystem) == Status::Ok);
    char restored[sizeof(payload)]{};
    size_t bytes_read = 0U;
    CHECK(fs::fat32::read(
              &filesystem, "/ETC/BOOT.CFG", 0U,
              restored, sizeof(restored), &bytes_read) == Status::Ok);
    CHECK(bytes_read == sizeof(payload) - 1U);
    CHECK(std::memcmp(restored, payload, bytes_read) == 0);
}

} // namespace

int main() {
    TestImage image;
    test_mount_lookup_read_and_stat(image);
    test_vfs_adapter(image);
    test_readdir_and_lfn_contract(image);
    test_metadata_validation_and_transactionality(image);
    test_chain_corruption_and_backend_errors(image);
    TestImage writable_image;
    test_mutation_and_persistence(writable_image);
    test_public_formatter(writable_image);

    if (failures != 0) {
        std::cerr << failures << " FAT32 test assertion(s) failed\n";
        return 1;
    }
    std::cout << "FAT32 read/write tests passed\n";
    return 0;
}
