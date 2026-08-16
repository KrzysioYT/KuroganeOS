#include "gpt.hpp"

namespace storage::gpt {
namespace {

constexpr uint64_t PRIMARY_HEADER_LBA = UINT64_C(1);
constexpr size_t HEADER_SIGNATURE_OFFSET = 0U;
constexpr size_t HEADER_REVISION_OFFSET = 8U;
constexpr size_t HEADER_SIZE_OFFSET = 12U;
constexpr size_t HEADER_CRC_OFFSET = 16U;
constexpr size_t HEADER_RESERVED_OFFSET = 20U;
constexpr size_t HEADER_CURRENT_LBA_OFFSET = 24U;
constexpr size_t HEADER_BACKUP_LBA_OFFSET = 32U;
constexpr size_t HEADER_FIRST_USABLE_OFFSET = 40U;
constexpr size_t HEADER_LAST_USABLE_OFFSET = 48U;
constexpr size_t HEADER_DISK_GUID_OFFSET = 56U;
constexpr size_t HEADER_ENTRY_LBA_OFFSET = 72U;
constexpr size_t HEADER_ENTRY_COUNT_OFFSET = 80U;
constexpr size_t HEADER_ENTRY_SIZE_OFFSET = 84U;
constexpr size_t HEADER_ENTRY_CRC_OFFSET = 88U;

constexpr size_t ENTRY_TYPE_GUID_OFFSET = 0U;
constexpr size_t ENTRY_UNIQUE_GUID_OFFSET = 16U;
constexpr size_t ENTRY_FIRST_LBA_OFFSET = 32U;
constexpr size_t ENTRY_LAST_LBA_OFFSET = 40U;
constexpr size_t ENTRY_ATTRIBUTES_OFFSET = 48U;
constexpr size_t ENTRY_NAME_OFFSET = 56U;

ParseResult failure(
    Status status,
    block::Status block_status = block::Status::Ok,
    uint32_t entry_index = NO_ENTRY_INDEX) {
    return ParseResult{status, block_status, entry_index};
}

uint32_t read_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) |
           (static_cast<uint32_t>(bytes[3]) << 24U);
}

uint64_t read_u64(const uint8_t* bytes) {
    return static_cast<uint64_t>(read_u32(bytes)) |
           (static_cast<uint64_t>(read_u32(bytes + 4U)) << 32U);
}

uint16_t read_u16(const uint8_t* bytes) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8U));
}

void copy_bytes(uint8_t* destination, const uint8_t* source, size_t count) {
    for (size_t index = 0U; index < count; ++index) {
        destination[index] = source[index];
    }
}

bool equal_bytes(const uint8_t* left, const uint8_t* right, size_t count) {
    for (size_t index = 0U; index < count; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

bool is_zero_guid(const uint8_t* guid) {
    for (size_t index = 0U; index < 16U; ++index) {
        if (guid[index] != 0U) {
            return false;
        }
    }
    return true;
}

uint32_t crc32_update(uint32_t crc, const uint8_t* bytes, size_t count) {
    for (size_t index = 0U; index < count; ++index) {
        crc ^= static_cast<uint32_t>(bytes[index]);
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t low_bit_mask =
                UINT32_C(0) - (crc & UINT32_C(1));
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & low_bit_mask);
        }
    }
    return crc;
}

uint32_t crc32(const uint8_t* bytes, size_t count) {
    return crc32_update(UINT32_MAX, bytes, count) ^ UINT32_MAX;
}

block::Status read_one_sector(
    const block::Device* device,
    uint64_t lba,
    uint8_t* buffer) {
    return block::read_blocks(
        device,
        lba,
        UINT64_C(1),
        buffer,
        static_cast<size_t>(device->sector_size));
}

block::Status read_entry_bytes(
    const block::Device* device,
    uint64_t entry_array_lba,
    uint64_t byte_offset,
    uint8_t* destination,
    size_t byte_count) {
    uint8_t sector[GPT_MAXIMUM_SECTOR_SIZE];
    const uint64_t sector_size = static_cast<uint64_t>(device->sector_size);
    uint64_t current_offset = byte_offset;
    size_t copied = 0U;

    while (copied < byte_count) {
        const uint64_t relative_lba = current_offset / sector_size;
        if (entry_array_lba > UINT64_MAX - relative_lba) {
            return block::Status::ArithmeticOverflow;
        }
        const uint64_t lba = entry_array_lba + relative_lba;
        const block::Status status = read_one_sector(device, lba, sector);
        if (status != block::Status::Ok) {
            return status;
        }

        const size_t within_sector = static_cast<size_t>(
            current_offset % sector_size);
        const size_t available =
            static_cast<size_t>(device->sector_size) - within_sector;
        const size_t remaining = byte_count - copied;
        const size_t amount = remaining < available ? remaining : available;
        copy_bytes(destination + copied, sector + within_sector, amount);

        copied += amount;
        current_offset += static_cast<uint64_t>(amount);
    }

    return block::Status::Ok;
}

bool ranges_overlap(
    uint64_t first_left,
    uint64_t last_left,
    uint64_t first_right,
    uint64_t last_right) {
    return first_left <= last_right && first_right <= last_left;
}

} // namespace

ParseResult parse_primary(const block::Device* device, Table* output) {
    if (output == nullptr) {
        return failure(Status::InvalidArgument);
    }

    const block::Status device_status = block::validate(device);
    if (device_status != block::Status::Ok) {
        return failure(Status::InvalidBlockDevice, device_status);
    }
    if (device->sector_size < 512U ||
        device->sector_size > GPT_MAXIMUM_SECTOR_SIZE) {
        return failure(Status::UnsupportedSectorSize);
    }
    if (device->sector_count <= PRIMARY_HEADER_LBA) {
        return failure(Status::OutOfBounds);
    }

    uint8_t header[GPT_MAXIMUM_SECTOR_SIZE]{};
    block::Status read_status =
        read_one_sector(device, PRIMARY_HEADER_LBA, header);
    if (read_status != block::Status::Ok) {
        return failure(Status::BlockDeviceError, read_status);
    }

    constexpr uint8_t signature[8] = {
        'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};
    if (!equal_bytes(
            header + HEADER_SIGNATURE_OFFSET,
            signature,
            sizeof(signature))) {
        return failure(Status::InvalidSignature);
    }

    const uint32_t revision = read_u32(header + HEADER_REVISION_OFFSET);
    if (revision != GPT_REVISION_1_0) {
        return failure(Status::UnsupportedRevision);
    }

    const uint32_t header_size = read_u32(header + HEADER_SIZE_OFFSET);
    if (header_size < GPT_MINIMUM_HEADER_SIZE ||
        header_size > device->sector_size) {
        return failure(Status::InvalidHeaderSize);
    }
    if (read_u32(header + HEADER_RESERVED_OFFSET) != 0U) {
        return failure(Status::InvalidReservedField);
    }

    const uint32_t recorded_header_crc = read_u32(header + HEADER_CRC_OFFSET);
    header[HEADER_CRC_OFFSET] = 0U;
    header[HEADER_CRC_OFFSET + 1U] = 0U;
    header[HEADER_CRC_OFFSET + 2U] = 0U;
    header[HEADER_CRC_OFFSET + 3U] = 0U;
    if (crc32(header, static_cast<size_t>(header_size)) !=
        recorded_header_crc) {
        return failure(Status::InvalidHeaderCrc);
    }

    const uint64_t current_lba = read_u64(header + HEADER_CURRENT_LBA_OFFSET);
    const uint64_t backup_lba = read_u64(header + HEADER_BACKUP_LBA_OFFSET);
    const uint64_t first_usable =
        read_u64(header + HEADER_FIRST_USABLE_OFFSET);
    const uint64_t last_usable =
        read_u64(header + HEADER_LAST_USABLE_OFFSET);
    const uint64_t entry_array_lba =
        read_u64(header + HEADER_ENTRY_LBA_OFFSET);
    const uint32_t entry_count = read_u32(header + HEADER_ENTRY_COUNT_OFFSET);
    const uint32_t entry_size = read_u32(header + HEADER_ENTRY_SIZE_OFFSET);
    const uint32_t recorded_entry_crc =
        read_u32(header + HEADER_ENTRY_CRC_OFFSET);

    if (current_lba != PRIMARY_HEADER_LBA) {
        return failure(Status::InvalidCurrentLba);
    }
    if (backup_lba != device->sector_count - UINT64_C(1) ||
        backup_lba == current_lba) {
        return failure(Status::InvalidBackupLba);
    }
    if (first_usable > last_usable || last_usable >= device->sector_count ||
        first_usable <= current_lba || last_usable >= backup_lba) {
        return failure(Status::InvalidUsableRange);
    }

    if (entry_count == 0U) {
        return failure(Status::InvalidEntryLayout);
    }
    if (entry_count > GPT_MAXIMUM_ENTRY_COUNT) {
        return failure(Status::EntryCountLimitExceeded);
    }
    if (entry_size < GPT_MINIMUM_ENTRY_SIZE ||
        entry_size > GPT_MAXIMUM_ENTRY_SIZE || (entry_size % 8U) != 0U) {
        return failure(Status::EntrySizeLimitExceeded);
    }

    const uint64_t entry_count_u64 = static_cast<uint64_t>(entry_count);
    const uint64_t entry_size_u64 = static_cast<uint64_t>(entry_size);
    if (entry_count_u64 > UINT64_MAX / entry_size_u64) {
        return failure(Status::AddressOverflow);
    }
    const uint64_t entry_bytes = entry_count_u64 * entry_size_u64;
    const uint64_t sector_size_u64 =
        static_cast<uint64_t>(device->sector_size);
    const uint64_t entry_blocks =
        ((entry_bytes - UINT64_C(1)) / sector_size_u64) + UINT64_C(1);

    if (entry_array_lba <= current_lba) {
        return failure(Status::InvalidEntryLayout);
    }
    if (entry_array_lba >= device->sector_count ||
        entry_blocks > device->sector_count - entry_array_lba) {
        return failure(Status::OutOfBounds);
    }
    const uint64_t entry_array_last =
        entry_array_lba + entry_blocks - UINT64_C(1);
    if (entry_array_last >= first_usable) {
        return failure(Status::InvalidEntryLayout);
    }
    if (entry_blocks >= backup_lba) {
        return failure(Status::InvalidUsableRange);
    }
    const uint64_t backup_entry_array_first = backup_lba - entry_blocks;
    if (last_usable >= backup_entry_array_first) {
        return failure(Status::InvalidUsableRange);
    }

    uint8_t sector[GPT_MAXIMUM_SECTOR_SIZE]{};
    uint64_t remaining = entry_bytes;
    uint32_t computed_entry_crc = UINT32_MAX;
    for (uint64_t block_index = 0U;
         block_index < entry_blocks;
         ++block_index) {
        read_status = read_one_sector(
            device, entry_array_lba + block_index, sector);
        if (read_status != block::Status::Ok) {
            return failure(Status::BlockDeviceError, read_status);
        }
        const uint64_t amount_u64 =
            remaining < sector_size_u64 ? remaining : sector_size_u64;
        const size_t amount = static_cast<size_t>(amount_u64);
        computed_entry_crc = crc32_update(computed_entry_crc, sector, amount);
        remaining -= amount_u64;
    }
    computed_entry_crc ^= UINT32_MAX;
    if (computed_entry_crc != recorded_entry_crc) {
        return failure(Status::InvalidEntryArrayCrc);
    }

    Table parsed{};
    parsed.revision = revision;
    parsed.header_size = header_size;
    parsed.current_lba = current_lba;
    parsed.backup_lba = backup_lba;
    parsed.first_usable_lba = first_usable;
    parsed.last_usable_lba = last_usable;
    copy_bytes(
        parsed.disk_guid.bytes,
        header + HEADER_DISK_GUID_OFFSET,
        sizeof(parsed.disk_guid.bytes));
    parsed.entry_array_lba = entry_array_lba;
    parsed.declared_entry_count = entry_count;
    parsed.entry_size = entry_size;

    uint8_t entry[GPT_MAXIMUM_ENTRY_SIZE]{};
    for (uint32_t entry_index = 0U;
         entry_index < entry_count;
         ++entry_index) {
        const uint64_t byte_offset =
            static_cast<uint64_t>(entry_index) * entry_size_u64;
        read_status = read_entry_bytes(
            device,
            entry_array_lba,
            byte_offset,
            entry,
            static_cast<size_t>(entry_size));
        if (read_status != block::Status::Ok) {
            return failure(
                Status::BlockDeviceError, read_status, entry_index);
        }

        if (is_zero_guid(entry + ENTRY_TYPE_GUID_OFFSET)) {
            continue;
        }

        const uint64_t partition_first =
            read_u64(entry + ENTRY_FIRST_LBA_OFFSET);
        const uint64_t partition_last =
            read_u64(entry + ENTRY_LAST_LBA_OFFSET);
        if (partition_first > partition_last ||
            partition_first < first_usable || partition_last > last_usable) {
            return failure(
                Status::InvalidPartitionRange,
                block::Status::Ok,
                entry_index);
        }

        for (size_t existing_index = 0U;
             existing_index < parsed.partition_count;
             ++existing_index) {
            const Partition& existing = parsed.partitions[existing_index];
            if (ranges_overlap(
                    partition_first,
                    partition_last,
                    existing.first_lba,
                    existing.last_lba)) {
                return failure(
                    Status::OverlappingPartitions,
                    block::Status::Ok,
                    entry_index);
            }
        }

        if (parsed.partition_count >= GPT_MAXIMUM_PARTITIONS) {
            return failure(
                Status::PartitionLimitExceeded,
                block::Status::Ok,
                entry_index);
        }

        Partition& partition = parsed.partitions[parsed.partition_count];
        copy_bytes(
            partition.type_guid.bytes,
            entry + ENTRY_TYPE_GUID_OFFSET,
            sizeof(partition.type_guid.bytes));
        copy_bytes(
            partition.unique_guid.bytes,
            entry + ENTRY_UNIQUE_GUID_OFFSET,
            sizeof(partition.unique_guid.bytes));
        partition.first_lba = partition_first;
        partition.last_lba = partition_last;
        partition.attributes = read_u64(entry + ENTRY_ATTRIBUTES_OFFSET);
        partition.name_length = 0U;
        for (size_t name_index = 0U;
             name_index < GPT_PARTITION_NAME_CODE_UNITS;
             ++name_index) {
            const uint16_t code_unit = read_u16(
                entry + ENTRY_NAME_OFFSET + (name_index * 2U));
            partition.name[name_index] = code_unit;
            if (code_unit != 0U && partition.name_length == name_index) {
                partition.name_length = name_index + 1U;
            }
        }
        partition.source_entry_index = entry_index;
        ++parsed.partition_count;
    }

    *output = parsed;
    return ParseResult{Status::Ok, block::Status::Ok, NO_ENTRY_INDEX};
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok:
            return "ok";
        case Status::InvalidArgument:
            return "invalid argument";
        case Status::InvalidBlockDevice:
            return "invalid block device";
        case Status::UnsupportedSectorSize:
            return "unsupported logical-sector size";
        case Status::BlockDeviceError:
            return "block-device read failed";
        case Status::InvalidSignature:
            return "invalid GPT signature";
        case Status::UnsupportedRevision:
            return "unsupported GPT revision";
        case Status::InvalidHeaderSize:
            return "invalid GPT header size";
        case Status::InvalidReservedField:
            return "non-zero GPT reserved field";
        case Status::InvalidHeaderCrc:
            return "GPT header CRC mismatch";
        case Status::InvalidCurrentLba:
            return "invalid primary-header LBA";
        case Status::InvalidBackupLba:
            return "invalid backup-header LBA";
        case Status::InvalidUsableRange:
            return "invalid GPT usable range";
        case Status::InvalidEntryLayout:
            return "invalid GPT entry-array layout";
        case Status::EntryCountLimitExceeded:
            return "GPT entry-count limit exceeded";
        case Status::EntrySizeLimitExceeded:
            return "invalid or excessive GPT entry size";
        case Status::AddressOverflow:
            return "GPT address arithmetic overflow";
        case Status::OutOfBounds:
            return "GPT structure is outside the device";
        case Status::InvalidEntryArrayCrc:
            return "GPT entry-array CRC mismatch";
        case Status::InvalidPartitionRange:
            return "partition is outside the usable range";
        case Status::OverlappingPartitions:
            return "GPT partitions overlap";
        case Status::PartitionLimitExceeded:
            return "partition result limit exceeded";
    }

    return "unknown GPT status";
}

} // namespace storage::gpt
