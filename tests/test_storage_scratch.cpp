#include <stddef.h>
#include <stdint.h>

#include <array>
#include <cstdio>
#include <cstring>

#include "../kernel/storage/scratch_test.hpp"

namespace {

using BlockStatus = storage::block::Status;
using ScratchStatus = storage::scratch_test::Status;

constexpr uint32_t SECTOR_SIZE = 512U;
constexpr uint64_t SECTOR_COUNT = 64U;
constexpr uint64_t TEST_LBA = 8U;
constexpr uint32_t TEST_SECTORS = 8U;

struct MemoryDisk {
    std::array<uint8_t, SECTOR_SIZE * SECTOR_COUNT> bytes{};
    size_t reads = 0U;
    size_t writes = 0U;
    size_t flushes = 0U;
    size_t fail_read_call = 0U;
    size_t fail_write_call = 0U;
    size_t fail_flush_call = 0U;
    size_t corrupt_read_call = 0U;
};

bool checked_range(
    uint64_t first_block,
    uint64_t block_count,
    size_t* offset,
    size_t* length) {
    if (block_count == 0U || first_block >= SECTOR_COUNT ||
        block_count > SECTOR_COUNT - first_block) {
        return false;
    }
    *offset = static_cast<size_t>(first_block) * SECTOR_SIZE;
    *length = static_cast<size_t>(block_count) * SECTOR_SIZE;
    return *offset <= SECTOR_SIZE * SECTOR_COUNT &&
           *length <= SECTOR_SIZE * SECTOR_COUNT - *offset;
}

BlockStatus memory_read(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    void* destination) {
    auto& disk = *static_cast<MemoryDisk*>(context);
    ++disk.reads;
    if (disk.reads == disk.fail_read_call) {
        return BlockStatus::IoError;
    }
    size_t offset = 0U;
    size_t length = 0U;
    if (destination == nullptr ||
        !checked_range(first_block, block_count, &offset, &length)) {
        return BlockStatus::OutOfRange;
    }
    std::memcpy(destination, disk.bytes.data() + offset, length);
    if (disk.reads == disk.corrupt_read_call && length != 0U) {
        static_cast<uint8_t*>(destination)[0] ^= UINT8_C(0xFF);
    }
    return BlockStatus::Ok;
}

BlockStatus memory_write(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    const void* source) {
    auto& disk = *static_cast<MemoryDisk*>(context);
    ++disk.writes;
    if (disk.writes == disk.fail_write_call) {
        return BlockStatus::IoError;
    }
    size_t offset = 0U;
    size_t length = 0U;
    if (source == nullptr ||
        !checked_range(first_block, block_count, &offset, &length)) {
        return BlockStatus::OutOfRange;
    }
    std::memcpy(disk.bytes.data() + offset, source, length);
    return BlockStatus::Ok;
}

BlockStatus memory_flush(void* context) {
    auto& disk = *static_cast<MemoryDisk*>(context);
    ++disk.flushes;
    return disk.flushes == disk.fail_flush_call
        ? BlockStatus::IoError
        : BlockStatus::Ok;
}

storage::block::Device make_device(MemoryDisk& disk) {
    return storage::block::Device{
        &disk,
        SECTOR_SIZE,
        SECTOR_COUNT,
        memory_read,
        memory_write,
        memory_flush};
}

void write_u32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8U);
    output[2] = static_cast<uint8_t>(value >> 16U);
    output[3] = static_cast<uint8_t>(value >> 24U);
}

void write_u64(uint8_t* output, uint64_t value) {
    write_u32(output, static_cast<uint32_t>(value));
    write_u32(output + 4U, static_cast<uint32_t>(value >> 32U));
}

void tag_disk(MemoryDisk& disk) {
    constexpr char magic[] = "KUROGANE_AHCI_SCRATCH_V1";
    std::memcpy(disk.bytes.data(), magic, sizeof(magic) - 1U);
    write_u32(disk.bytes.data() + 32U, 1U);
    write_u32(disk.bytes.data() + 36U, 64U);
    write_u64(disk.bytes.data() + 40U, SECTOR_COUNT);
    write_u64(disk.bytes.data() + 48U, TEST_LBA);
    write_u32(disk.bytes.data() + 56U, TEST_SECTORS);
    write_u32(disk.bytes.data() + 60U, UINT32_C(0x4B535431));
    for (size_t index = 0U; index < TEST_SECTORS * SECTOR_SIZE; ++index) {
        disk.bytes[static_cast<size_t>(TEST_LBA) * SECTOR_SIZE + index] =
            static_cast<uint8_t>((index * 37U + 11U) & 0xFFU);
    }
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

bool test_untagged_never_writes() {
    MemoryDisk disk{};
    auto device = make_device(disk);
    storage::scratch_test::Workspace workspace{};
    const auto result = storage::scratch_test::run(&device, &workspace);
    return expect(result.status == ScratchStatus::NotTagged, "untagged status") &&
           expect(!result.tagged, "untagged flag") &&
           expect(disk.writes == 0U && disk.flushes == 0U, "untagged no write");
}

bool test_invalid_header_never_writes() {
    MemoryDisk disk{};
    tag_disk(disk);
    write_u64(disk.bytes.data() + 40U, SECTOR_COUNT + 1U);
    auto device = make_device(disk);
    storage::scratch_test::Workspace workspace{};
    const auto result = storage::scratch_test::run(&device, &workspace);
    return expect(result.tagged, "malformed tag recognized") &&
           expect(result.status == ScratchStatus::GeometryMismatch,
                  "malformed geometry rejected") &&
           expect(disk.writes == 0U && disk.flushes == 0U,
                  "malformed header no write");
}

bool test_success_restores_original() {
    MemoryDisk disk{};
    tag_disk(disk);
    const auto original = disk.bytes;
    auto device = make_device(disk);
    storage::scratch_test::Workspace workspace{};
    const auto result = storage::scratch_test::run(&device, &workspace);
    return expect(result.status == ScratchStatus::Ok, "success status") &&
           expect(result.primary_status == ScratchStatus::Ok, "success primary") &&
           expect(result.write_attempted && result.restored, "success lifecycle") &&
           expect(disk.writes == 2U && disk.flushes == 2U && disk.reads == 4U,
                  "success I/O sequence") &&
           expect(disk.bytes == original, "success exact restoration");
}

bool test_verification_mismatch_is_recovered() {
    MemoryDisk disk{};
    tag_disk(disk);
    const auto original = disk.bytes;
    disk.corrupt_read_call = 3U;
    auto device = make_device(disk);
    storage::scratch_test::Workspace workspace{};
    const auto result = storage::scratch_test::run(&device, &workspace);
    return expect(result.status == ScratchStatus::VerifyMismatch,
                  "mismatch primary status") &&
           expect(result.primary_status == ScratchStatus::VerifyMismatch,
                  "mismatch retained") &&
           expect(result.restored, "mismatch restored") &&
           expect(disk.bytes == original, "mismatch exact restoration");
}

bool test_write_failure_is_recovered() {
    MemoryDisk disk{};
    tag_disk(disk);
    const auto original = disk.bytes;
    disk.fail_write_call = 1U;
    auto device = make_device(disk);
    storage::scratch_test::Workspace workspace{};
    const auto result = storage::scratch_test::run(&device, &workspace);
    return expect(result.status == ScratchStatus::PatternWriteFailed,
                  "write failure status") &&
           expect(result.restored, "write failure restored") &&
           expect(disk.writes == 2U, "recovery write attempted") &&
           expect(disk.bytes == original, "write failure exact restoration");
}

bool test_restore_failure_takes_precedence() {
    MemoryDisk disk{};
    tag_disk(disk);
    disk.corrupt_read_call = 3U;
    disk.fail_write_call = 2U;
    auto device = make_device(disk);
    storage::scratch_test::Workspace workspace{};
    const auto result = storage::scratch_test::run(&device, &workspace);
    return expect(result.status == ScratchStatus::RestoreWriteFailed,
                  "restore failure precedence") &&
           expect(result.primary_status == ScratchStatus::VerifyMismatch,
                  "restore failure retains primary") &&
           expect(!result.restored, "restore failure is explicit") &&
           expect(result.block_status == BlockStatus::IoError,
                  "restore backend status retained");
}

} // namespace

int main() {
    const bool ok = test_untagged_never_writes() &&
                    test_invalid_header_never_writes() &&
                    test_success_restores_original() &&
                    test_verification_mismatch_is_recovered() &&
                    test_write_failure_is_recovered() &&
                    test_restore_failure_takes_precedence();
    if (!ok) {
        return 1;
    }
    std::puts("storage scratch tests: PASS");
    return 0;
}
