#pragma once

#include <stddef.h>
#include <stdint.h>

#include "block_device.hpp"

namespace storage::gpt {

constexpr uint32_t GPT_REVISION_1_0 = UINT32_C(0x00010000);
constexpr uint32_t GPT_MINIMUM_HEADER_SIZE = 92U;
constexpr uint32_t GPT_MINIMUM_ENTRY_SIZE = 128U;
constexpr uint32_t GPT_MAXIMUM_ENTRY_SIZE = 1024U;
constexpr uint32_t GPT_MAXIMUM_ENTRY_COUNT = 4096U;
constexpr uint32_t GPT_MAXIMUM_SECTOR_SIZE = 4096U;
constexpr size_t GPT_PARTITION_NAME_CODE_UNITS = 36U;
constexpr size_t GPT_MAXIMUM_PARTITIONS = 128U;
constexpr uint32_t NO_ENTRY_INDEX = UINT32_MAX;

struct Guid {
    // Bytes are retained exactly as encoded on disk. Formatting a GPT GUID
    // requires applying the mixed-endian GPT representation at presentation
    // time; the parser intentionally does not reinterpret it.
    uint8_t bytes[16];
};

struct Partition {
    Guid type_guid;
    Guid unique_guid;
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[GPT_PARTITION_NAME_CODE_UNITS];
    size_t name_length;
    uint32_t source_entry_index;
};

struct Table {
    uint32_t revision;
    uint32_t header_size;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    Guid disk_guid;
    uint64_t entry_array_lba;
    uint32_t declared_entry_count;
    uint32_t entry_size;
    Partition partitions[GPT_MAXIMUM_PARTITIONS];
    size_t partition_count;
};

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidBlockDevice,
    UnsupportedSectorSize,
    BlockDeviceError,
    InvalidSignature,
    UnsupportedRevision,
    InvalidHeaderSize,
    InvalidReservedField,
    InvalidHeaderCrc,
    InvalidCurrentLba,
    InvalidBackupLba,
    InvalidUsableRange,
    InvalidEntryLayout,
    EntryCountLimitExceeded,
    EntrySizeLimitExceeded,
    AddressOverflow,
    OutOfBounds,
    InvalidEntryArrayCrc,
    InvalidPartitionRange,
    OverlappingPartitions,
    PartitionLimitExceeded
};

struct ParseResult {
    Status status;
    // Exact status returned by the block layer for InvalidBlockDevice and
    // BlockDeviceError. It is Ok for format-validation failures.
    block::Status block_status;
    // Identifies a bad partition entry when applicable, otherwise UINT32_MAX.
    uint32_t entry_index;
};

// Parses and validates the primary GPT header at LBA 1. The output table is
// unchanged unless the complete header, entry-array CRC and every non-empty
// partition entry pass validation.
ParseResult parse_primary(const block::Device* device, Table* output);

const char* status_message(Status status);

} // namespace storage::gpt
