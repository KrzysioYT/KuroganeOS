#include <stddef.h>
#include <stdint.h>

#include <array>
#include <cstdio>
#include <cstring>

#include "../kernel/storage/partition_device.hpp"

namespace {

using BlockStatus = storage::block::Status;

struct MemoryDisk {
    std::array<uint8_t, 32U * 512U> bytes{};
    size_t reads = 0U;
    size_t writes = 0U;
    size_t flushes = 0U;
    BlockStatus next_status = BlockStatus::Ok;
};

BlockStatus consume_status(MemoryDisk& disk) {
    const BlockStatus result = disk.next_status;
    disk.next_status = BlockStatus::Ok;
    return result;
}

BlockStatus memory_read(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    void* destination) {
    auto& disk = *static_cast<MemoryDisk*>(context);
    ++disk.reads;
    const BlockStatus status = consume_status(disk);
    if (status != BlockStatus::Ok) {
        return status;
    }
    const size_t offset = static_cast<size_t>(first_block) * 512U;
    const size_t length = static_cast<size_t>(block_count) * 512U;
    std::memcpy(destination, disk.bytes.data() + offset, length);
    return BlockStatus::Ok;
}

BlockStatus memory_write(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    const void* source) {
    auto& disk = *static_cast<MemoryDisk*>(context);
    ++disk.writes;
    const BlockStatus status = consume_status(disk);
    if (status != BlockStatus::Ok) {
        return status;
    }
    const size_t offset = static_cast<size_t>(first_block) * 512U;
    const size_t length = static_cast<size_t>(block_count) * 512U;
    std::memcpy(disk.bytes.data() + offset, source, length);
    return BlockStatus::Ok;
}

BlockStatus memory_flush(void* context) {
    auto& disk = *static_cast<MemoryDisk*>(context);
    ++disk.flushes;
    return consume_status(disk);
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

bool test_translation_and_boundaries() {
    MemoryDisk disk{};
    for (size_t sector = 0U; sector < 32U; ++sector) {
        std::memset(
            disk.bytes.data() + sector * 512U,
            static_cast<int>(sector),
            512U);
    }
    storage::block::Device parent{
        &disk, 512U, 32U, memory_read, memory_write, memory_flush};
    storage::partition::Device partition{};

    bool ok = expect(
        storage::partition::initialize(&partition, &parent, 8U, 10U) ==
            BlockStatus::Ok,
        "valid partition initializes");
    const storage::block::Device* view =
        storage::partition::as_block_device(&partition);
    ok &= expect(view != nullptr, "initialized view is available");
    ok &= expect(
        view != nullptr && view->sector_size == 512U &&
            view->sector_count == 10U,
        "view exposes partition geometry");

    std::array<uint8_t, 1024U> buffer{};
    ok &= expect(
        storage::block::read_blocks(
            view, 1U, 2U, buffer.data(), buffer.size()) == BlockStatus::Ok,
        "partition read succeeds");
    ok &= expect(
        buffer[0] == 9U && buffer[511] == 9U && buffer[512] == 10U,
        "relative read maps to parent LBAs");
    const size_t reads_before_reject = disk.reads;
    ok &= expect(
        storage::block::read_blocks(
            view, 9U, 2U, buffer.data(), buffer.size()) ==
            BlockStatus::OutOfRange,
        "read cannot cross partition end");
    ok &= expect(
        disk.reads == reads_before_reject,
        "rejected read does not reach parent");

    buffer.fill(0xA5U);
    ok &= expect(
        storage::block::write_blocks(
            view, 0U, 2U, buffer.data(), buffer.size()) == BlockStatus::Ok,
        "partition write succeeds");
    ok &= expect(
        disk.bytes[8U * 512U] == 0xA5U &&
            disk.bytes[7U * 512U] == 7U &&
            disk.bytes[10U * 512U] == 10U,
        "write is confined to translated range");
    ok &= expect(
        storage::block::flush(view) == BlockStatus::Ok &&
            disk.flushes == 1U,
        "flush reaches parent");
    return ok;
}

bool test_errors_and_transactionality() {
    MemoryDisk disk{};
    storage::block::Device parent{
        &disk, 512U, 32U, memory_read, memory_write, memory_flush};
    storage::partition::Device partition{};
    bool ok = expect(
        storage::partition::initialize(&partition, &parent, 4U, 8U) ==
            BlockStatus::Ok,
        "baseline partition initializes");
    const uint64_t old_first_lba = partition.first_lba;
    const uint64_t old_count = partition.block_device.sector_count;

    ok &= expect(
        storage::partition::initialize(&partition, &parent, 31U, 2U) ==
            BlockStatus::OutOfRange,
        "invalid parent range is rejected");
    ok &= expect(
        partition.first_lba == old_first_lba &&
            partition.block_device.sector_count == old_count,
        "failed initialize leaves prior view unchanged");
    ok &= expect(
        storage::partition::initialize(&partition, &parent, 0U, 0U) ==
            BlockStatus::InvalidArgument,
        "empty partition is rejected");

    std::array<uint8_t, 512U> buffer{};
    disk.next_status = BlockStatus::IoError;
    ok &= expect(
        storage::block::read_blocks(
            storage::partition::as_block_device(&partition),
            0U,
            1U,
            buffer.data(),
            buffer.size()) == BlockStatus::IoError,
        "known parent error propagates");
    disk.next_status = static_cast<BlockStatus>(UINT8_C(0xFF));
    ok &= expect(
        storage::block::write_blocks(
            storage::partition::as_block_device(&partition),
            0U,
            1U,
            buffer.data(),
            buffer.size()) == BlockStatus::BackendFailure,
        "unknown parent error is normalized");
    return ok;
}

} // namespace

int main() {
    const bool ok = test_translation_and_boundaries() &&
                    test_errors_and_transactionality();
    if (!ok) {
        return 1;
    }
    std::puts("partition-device tests: PASS");
    return 0;
}
