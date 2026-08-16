#pragma once

#include <stdint.h>

#include "block_device.hpp"

namespace storage::scratch_test {

constexpr uint32_t SCRATCH_SECTOR_SIZE = 512U;
constexpr uint32_t MAXIMUM_TEST_SECTORS = 8U;
constexpr size_t MAXIMUM_TEST_BYTES =
    static_cast<size_t>(SCRATCH_SECTOR_SIZE) * MAXIMUM_TEST_SECTORS;

enum class Status : uint8_t {
    Ok = 0,
    NotTagged,
    InvalidArgument,
    InvalidHeader,
    GeometryMismatch,
    RangeInvalid,
    InitialReadFailed,
    PatternWriteFailed,
    PatternFlushFailed,
    VerifyReadFailed,
    VerifyMismatch,
    RestoreWriteFailed,
    RestoreFlushFailed,
    RestoreReadFailed,
    RestoreMismatch
};

struct Workspace {
    uint8_t header[SCRATCH_SECTOR_SIZE];
    uint8_t original[MAXIMUM_TEST_BYTES];
    uint8_t pattern[MAXIMUM_TEST_BYTES];
    uint8_t verification[MAXIMUM_TEST_BYTES];
};

struct Result {
    Status status;
    Status primary_status;
    block::Status block_status;
    uint64_t first_lba;
    uint32_t sector_count;
    bool tagged;
    bool write_attempted;
    bool restored;
};

// Performs a destructive-but-restoring write/flush/readback test only when
// LBA0 contains the exact KUROGANE_AHCI_SCRATCH_V1 header produced by the host
// helper. Untagged or malformed devices are never written. Once a write is
// attempted, every error path attempts write/flush/readback restoration of the
// original bytes; a recovery error takes precedence in Result::status while
// Result::primary_status retains the triggering failure.
Result run(const block::Device* device, Workspace* workspace);

const char* status_message(Status status);

} // namespace storage::scratch_test
