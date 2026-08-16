#include <stddef.h>
#include <stdint.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../kernel/storage/gpt.hpp"

namespace {

using storage::block::Device;
using BlockStatus = storage::block::Status;
using GptStatus = storage::gpt::Status;

struct MemoryDisk {
    uint32_t sector_size;
    uint64_t sector_count;
    std::vector<uint8_t> bytes;
    size_t read_calls;
    size_t fail_on_read_call;
    uint64_t fail_lba;
    BlockStatus failure_status;
    bool return_invalid_status;

    MemoryDisk(uint32_t logical_sector_size, uint64_t logical_sector_count)
        : sector_size(logical_sector_size),
          sector_count(logical_sector_count),
          bytes(
              static_cast<size_t>(logical_sector_size) *
                  static_cast<size_t>(logical_sector_count),
              0U),
          read_calls(0U),
          fail_on_read_call(std::numeric_limits<size_t>::max()),
          fail_lba(UINT64_MAX),
          failure_status(BlockStatus::IoError),
          return_invalid_status(false) {}
};

bool checked_byte_range(
    const MemoryDisk& disk,
    uint64_t first_block,
    uint64_t block_count,
    size_t* offset,
    size_t* length) {
    if (block_count == 0U || first_block >= disk.sector_count ||
        block_count > disk.sector_count - first_block) {
        return false;
    }
    if (first_block > static_cast<uint64_t>(SIZE_MAX) / disk.sector_size ||
        block_count > static_cast<uint64_t>(SIZE_MAX) / disk.sector_size) {
        return false;
    }

    *offset = static_cast<size_t>(first_block) * disk.sector_size;
    *length = static_cast<size_t>(block_count) * disk.sector_size;
    return *offset <= disk.bytes.size() &&
           *length <= disk.bytes.size() - *offset;
}

BlockStatus memory_read(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    void* destination) {
    auto* disk = static_cast<MemoryDisk*>(context);
    ++disk->read_calls;
    if (disk->return_invalid_status) {
        return static_cast<BlockStatus>(UINT8_C(0xFF));
    }
    if (disk->read_calls == disk->fail_on_read_call ||
        (disk->fail_lba >= first_block &&
         disk->fail_lba - first_block < block_count)) {
        return disk->failure_status;
    }

    size_t offset = 0U;
    size_t length = 0U;
    if (!checked_byte_range(
            *disk, first_block, block_count, &offset, &length)) {
        return BlockStatus::OutOfRange;
    }
    std::memcpy(destination, disk->bytes.data() + offset, length);
    return BlockStatus::Ok;
}

BlockStatus memory_write(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    const void* source) {
    auto* disk = static_cast<MemoryDisk*>(context);
    size_t offset = 0U;
    size_t length = 0U;
    if (!checked_byte_range(
            *disk, first_block, block_count, &offset, &length)) {
        return BlockStatus::OutOfRange;
    }
    std::memcpy(disk->bytes.data() + offset, source, length);
    return BlockStatus::Ok;
}

BlockStatus memory_flush(void*) {
    return BlockStatus::Ok;
}

Device make_device(MemoryDisk& disk) {
    return Device{
        &disk,
        disk.sector_size,
        disk.sector_count,
        memory_read,
        memory_write,
        memory_flush};
}

void write_u16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value & UINT16_C(0xFF));
    bytes[1] = static_cast<uint8_t>((value >> 8U) & UINT16_C(0xFF));
}

void write_u32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value & UINT32_C(0xFF));
    bytes[1] = static_cast<uint8_t>((value >> 8U) & UINT32_C(0xFF));
    bytes[2] = static_cast<uint8_t>((value >> 16U) & UINT32_C(0xFF));
    bytes[3] = static_cast<uint8_t>((value >> 24U) & UINT32_C(0xFF));
}

void write_u64(uint8_t* bytes, uint64_t value) {
    write_u32(bytes, static_cast<uint32_t>(value & UINT64_C(0xFFFFFFFF)));
    write_u32(bytes + 4U, static_cast<uint32_t>(value >> 32U));
}

uint32_t test_crc32(const uint8_t* bytes, size_t count) {
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0U; index < count; ++index) {
        crc ^= static_cast<uint32_t>(bytes[index]);
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = UINT32_C(0) - (crc & UINT32_C(1));
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return crc ^ UINT32_MAX;
}

struct Fixture {
    MemoryDisk disk;
    uint32_t entry_count;
    uint32_t entry_size;
    uint64_t entry_blocks;
    uint64_t first_usable;
    uint64_t last_usable;

    explicit Fixture(
        uint64_t sector_count = UINT64_C(100),
        uint32_t partition_entry_count = 8U,
        uint32_t partition_entry_size = 128U)
        : disk(512U, sector_count),
          entry_count(partition_entry_count),
          entry_size(partition_entry_size),
          entry_blocks(
              ((static_cast<uint64_t>(partition_entry_count) *
                    partition_entry_size -
                UINT64_C(1)) /
                   512U) +
              UINT64_C(1)),
          first_usable(UINT64_C(2) + entry_blocks),
          last_usable(sector_count - entry_blocks - UINT64_C(2)) {
        uint8_t* const header = disk.bytes.data() + 512U;
        const uint8_t signature[8] = {
            'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};
        std::memcpy(header, signature, sizeof(signature));
        write_u32(header + 8U, storage::gpt::GPT_REVISION_1_0);
        write_u32(header + 12U, storage::gpt::GPT_MINIMUM_HEADER_SIZE);
        write_u32(header + 20U, 0U);
        write_u64(header + 24U, UINT64_C(1));
        write_u64(header + 32U, sector_count - UINT64_C(1));
        write_u64(header + 40U, first_usable);
        write_u64(header + 48U, last_usable);
        for (size_t index = 0U; index < 16U; ++index) {
            header[56U + index] = static_cast<uint8_t>(0xA0U + index);
        }
        write_u64(header + 72U, UINT64_C(2));
        write_u32(header + 80U, entry_count);
        write_u32(header + 84U, entry_size);
        finalize();
    }

    uint8_t* header() {
        return disk.bytes.data() + disk.sector_size;
    }

    uint8_t* entry(uint32_t index) {
        return disk.bytes.data() + (2U * disk.sector_size) +
               (static_cast<size_t>(index) * entry_size);
    }

    void set_partition(
        uint32_t index,
        uint64_t first_lba,
        uint64_t last_lba,
        const char* name) {
        uint8_t* const bytes = entry(index);
        std::memset(bytes, 0, entry_size);
        for (size_t guid_index = 0U; guid_index < 16U; ++guid_index) {
            bytes[guid_index] = static_cast<uint8_t>(guid_index + 1U);
            bytes[16U + guid_index] = static_cast<uint8_t>(
                0x80U + guid_index + static_cast<size_t>(index));
        }
        write_u64(bytes + 32U, first_lba);
        write_u64(bytes + 40U, last_lba);
        write_u64(bytes + 48U, UINT64_C(0x1122334455667788));
        for (size_t name_index = 0U;
             name_index < storage::gpt::GPT_PARTITION_NAME_CODE_UNITS &&
             name[name_index] != '\0';
             ++name_index) {
            write_u16(
                bytes + 56U + (name_index * 2U),
                static_cast<uint16_t>(
                    static_cast<unsigned char>(name[name_index])));
        }
    }

    void update_header_crc() {
        uint8_t* const bytes = header();
        write_u32(bytes + 16U, 0U);
        write_u32(
            bytes + 16U,
            test_crc32(bytes, storage::gpt::GPT_MINIMUM_HEADER_SIZE));
    }

    void finalize() {
        const size_t entries_size =
            static_cast<size_t>(entry_count) * entry_size;
        write_u32(
            header() + 88U,
            test_crc32(entry(0U), entries_size));
        update_header_crc();
    }
};

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::fprintf(                                                     \
                stderr,                                                       \
                "CHECK failed at %s:%d: %s\n",                              \
                __FILE__,                                                     \
                __LINE__,                                                     \
                #expression);                                                 \
            return false;                                                     \
        }                                                                     \
    } while (false)

bool expect_status(Fixture& fixture, GptStatus expected) {
    Device device = make_device(fixture.disk);
    storage::gpt::Table table{};
    table.partition_count = 77U;
    const storage::gpt::ParseResult result =
        storage::gpt::parse_primary(&device, &table);
    CHECK(result.status == expected);
    if (expected != GptStatus::Ok) {
        CHECK(table.partition_count == 77U);
    }
    return true;
}

bool test_block_device_contract() {
    MemoryDisk disk(512U, UINT64_C(8));
    Device device = make_device(disk);
    uint8_t sector[512]{};

    CHECK(storage::block::validate(nullptr) == BlockStatus::InvalidArgument);
    Device invalid = device;
    invalid.sector_size = 0U;
    CHECK(storage::block::validate(&invalid) == BlockStatus::InvalidGeometry);
    invalid = device;
    invalid.flush = nullptr;
    CHECK(storage::block::validate(&invalid) == BlockStatus::MissingCallback);
    CHECK(storage::block::read_blocks(
              &device, 0U, 0U, sector, sizeof(sector)) ==
          BlockStatus::InvalidArgument);
    CHECK(storage::block::read_blocks(
              &device, 8U, 1U, sector, sizeof(sector)) ==
          BlockStatus::OutOfRange);
    CHECK(storage::block::read_blocks(
              &device, 0U, 1U, sector, sizeof(sector) - 1U) ==
          BlockStatus::BufferTooSmall);

    Device enormous = device;
    enormous.sector_size = UINT32_MAX;
    enormous.sector_count = UINT64_MAX;
    CHECK(storage::block::read_blocks(
              &enormous,
              0U,
              UINT64_MAX,
              sector,
              sizeof(sector)) == BlockStatus::ArithmeticOverflow);

    disk.return_invalid_status = true;
    CHECK(storage::block::read_blocks(
              &device, 0U, 1U, sector, sizeof(sector)) ==
          BlockStatus::BackendFailure);
    disk.return_invalid_status = false;
    CHECK(storage::block::write_blocks(
              &device, 0U, 1U, sector, sizeof(sector)) == BlockStatus::Ok);
    CHECK(storage::block::flush(&device) == BlockStatus::Ok);
    CHECK(storage::block::to_kstatus(BlockStatus::Ok) == KStatus::Ok);
    CHECK(storage::block::to_kstatus(BlockStatus::TimedOut) == KStatus::Timeout);
    CHECK(storage::block::to_kstatus(BlockStatus::NoDevice) == KStatus::NoDevice);
    CHECK(storage::block::to_kstatus(BlockStatus::ControllerFault) ==
          KStatus::DeviceFault);
    return true;
}

bool test_happy_path() {
    Fixture fixture;
    fixture.set_partition(0U, 10U, 19U, "system");
    fixture.set_partition(3U, 30U, 39U, "data");
    fixture.finalize();
    Device device = make_device(fixture.disk);
    storage::gpt::Table table{};

    const storage::gpt::ParseResult result =
        storage::gpt::parse_primary(&device, &table);
    CHECK(result.status == GptStatus::Ok);
    CHECK(result.block_status == BlockStatus::Ok);
    CHECK(result.entry_index == storage::gpt::NO_ENTRY_INDEX);
    CHECK(table.current_lba == 1U);
    CHECK(table.backup_lba == 99U);
    CHECK(table.entry_array_lba == 2U);
    CHECK(table.declared_entry_count == 8U);
    CHECK(table.partition_count == 2U);
    CHECK(table.partitions[0].source_entry_index == 0U);
    CHECK(table.partitions[1].source_entry_index == 3U);
    CHECK(table.partitions[0].first_lba == 10U);
    CHECK(table.partitions[0].last_lba == 19U);
    CHECK(table.partitions[0].attributes == UINT64_C(0x1122334455667788));
    CHECK(table.partitions[0].type_guid.bytes[0] == 1U);
    CHECK(table.partitions[0].unique_guid.bytes[0] == 0x80U);
    CHECK(table.partitions[0].name_length == 6U);
    CHECK(table.partitions[0].name[0] == static_cast<uint16_t>('s'));
    CHECK(table.partitions[0].name[5] == static_cast<uint16_t>('m'));
    CHECK(table.disk_guid.bytes[0] == 0xA0U);
    CHECK(table.disk_guid.bytes[15] == 0xAFU);
    return true;
}

bool test_header_validation() {
    {
        Fixture fixture;
        fixture.header()[0] = 'X';
        CHECK(expect_status(fixture, GptStatus::InvalidSignature));
    }
    {
        Fixture fixture;
        write_u32(fixture.header() + 8U, UINT32_C(0x00020000));
        fixture.update_header_crc();
        CHECK(expect_status(fixture, GptStatus::UnsupportedRevision));
    }
    {
        Fixture fixture;
        write_u32(fixture.header() + 12U, 91U);
        fixture.update_header_crc();
        CHECK(expect_status(fixture, GptStatus::InvalidHeaderSize));
    }
    {
        Fixture fixture;
        fixture.header()[40U] ^= UINT8_C(1);
        CHECK(expect_status(fixture, GptStatus::InvalidHeaderCrc));
    }
    {
        Fixture fixture;
        write_u64(fixture.header() + 24U, UINT64_MAX);
        fixture.update_header_crc();
        CHECK(expect_status(fixture, GptStatus::InvalidCurrentLba));
    }
    {
        Fixture fixture;
        write_u64(fixture.header() + 32U, UINT64_MAX);
        fixture.update_header_crc();
        CHECK(expect_status(fixture, GptStatus::InvalidBackupLba));
    }
    return true;
}

bool test_layout_limits_and_overflow_guards() {
    {
        Fixture fixture;
        write_u32(
            fixture.header() + 80U,
            storage::gpt::GPT_MAXIMUM_ENTRY_COUNT + 1U);
        fixture.update_header_crc();
        CHECK(expect_status(fixture, GptStatus::EntryCountLimitExceeded));
    }
    {
        Fixture fixture;
        write_u32(fixture.header() + 84U, UINT32_MAX);
        fixture.update_header_crc();
        CHECK(expect_status(fixture, GptStatus::EntrySizeLimitExceeded));
    }
    {
        Fixture fixture;
        write_u64(fixture.header() + 72U, UINT64_MAX);
        fixture.update_header_crc();
        CHECK(expect_status(fixture, GptStatus::OutOfBounds));
    }
    {
        Fixture fixture;
        write_u64(fixture.header() + 40U, UINT64_MAX);
        fixture.update_header_crc();
        CHECK(expect_status(fixture, GptStatus::InvalidUsableRange));
    }
    return true;
}

bool test_entry_crc_and_ranges() {
    {
        Fixture fixture;
        fixture.set_partition(0U, 10U, 19U, "system");
        fixture.finalize();
        fixture.entry(0U)[60U] ^= UINT8_C(1);
        CHECK(expect_status(fixture, GptStatus::InvalidEntryArrayCrc));
    }
    {
        Fixture fixture;
        fixture.set_partition(0U, 10U, 19U, "system");
        fixture.set_partition(1U, 19U, 25U, "overlap");
        fixture.finalize();
        Device device = make_device(fixture.disk);
        storage::gpt::Table table{};
        const storage::gpt::ParseResult result =
            storage::gpt::parse_primary(&device, &table);
        CHECK(result.status == GptStatus::OverlappingPartitions);
        CHECK(result.entry_index == 1U);
    }
    {
        Fixture fixture;
        fixture.set_partition(
            0U, fixture.first_usable - UINT64_C(1), 19U, "outside");
        fixture.finalize();
        Device device = make_device(fixture.disk);
        storage::gpt::Table table{};
        const storage::gpt::ParseResult result =
            storage::gpt::parse_primary(&device, &table);
        CHECK(result.status == GptStatus::InvalidPartitionRange);
        CHECK(result.entry_index == 0U);
    }
    return true;
}

bool test_partition_result_limit() {
    Fixture fixture(UINT64_C(400), 129U);
    for (uint32_t index = 0U; index < 129U; ++index) {
        const uint64_t lba = UINT64_C(40) + static_cast<uint64_t>(index);
        fixture.set_partition(index, lba, lba, "p");
    }
    fixture.finalize();
    Device device = make_device(fixture.disk);
    storage::gpt::Table table{};
    const storage::gpt::ParseResult result =
        storage::gpt::parse_primary(&device, &table);
    CHECK(result.status == GptStatus::PartitionLimitExceeded);
    CHECK(result.entry_index == 128U);
    return true;
}

bool test_backend_failures() {
    {
        Fixture fixture;
        fixture.disk.fail_lba = 1U;
        fixture.disk.failure_status = BlockStatus::DeviceFault;
        Device device = make_device(fixture.disk);
        storage::gpt::Table table{};
        const storage::gpt::ParseResult result =
            storage::gpt::parse_primary(&device, &table);
        CHECK(result.status == GptStatus::BlockDeviceError);
        CHECK(result.block_status == BlockStatus::DeviceFault);
        CHECK(result.entry_index == storage::gpt::NO_ENTRY_INDEX);
    }
    {
        Fixture fixture;
        fixture.disk.fail_lba = 2U;
        Device device = make_device(fixture.disk);
        storage::gpt::Table table{};
        const storage::gpt::ParseResult result =
            storage::gpt::parse_primary(&device, &table);
        CHECK(result.status == GptStatus::BlockDeviceError);
        CHECK(result.block_status == BlockStatus::IoError);
    }
    {
        Fixture fixture;
        fixture.set_partition(0U, 10U, 19U, "system");
        fixture.finalize();
        // Header and two CRC sectors consume reads 1..3. Read 4 starts
        // decoding entry zero, after the array CRC has already succeeded.
        fixture.disk.fail_on_read_call = 4U;
        Device device = make_device(fixture.disk);
        storage::gpt::Table table{};
        const storage::gpt::ParseResult result =
            storage::gpt::parse_primary(&device, &table);
        CHECK(result.status == GptStatus::BlockDeviceError);
        CHECK(result.entry_index == 0U);
    }
    {
        Fixture fixture;
        Device device = make_device(fixture.disk);
        device.write = nullptr;
        storage::gpt::Table table{};
        const storage::gpt::ParseResult result =
            storage::gpt::parse_primary(&device, &table);
        CHECK(result.status == GptStatus::InvalidBlockDevice);
        CHECK(result.block_status == BlockStatus::MissingCallback);
    }
    return true;
}

} // namespace

int main() {
    struct TestCase {
        const char* name;
        bool (*run)();
    };

    const TestCase tests[] = {
        {"block-device contract", test_block_device_contract},
        {"GPT happy path", test_happy_path},
        {"GPT header validation", test_header_validation},
        {"GPT layout/overflow guards", test_layout_limits_and_overflow_guards},
        {"GPT CRC/range validation", test_entry_crc_and_ranges},
        {"GPT fixed result limit", test_partition_result_limit},
        {"GPT backend failures", test_backend_failures},
    };

    for (const TestCase& test : tests) {
        if (!test.run()) {
            std::fprintf(stderr, "[FAIL] %s\n", test.name);
            return 1;
        }
        std::printf("[PASS] %s\n", test.name);
    }

    return 0;
}
