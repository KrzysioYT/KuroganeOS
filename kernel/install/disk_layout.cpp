#include "disk_layout.hpp"

#include "../libk/crc.hpp"

namespace install::disk_layout {
namespace {

constexpr uint32_t kSectorSize = 512U;
constexpr uint32_t kEntryCount = 128U;
constexpr uint32_t kEntrySize = 128U;
constexpr uint64_t kEntrySectors = 32U;
constexpr uint64_t kFirstUsableLba = 34U;
constexpr uint8_t kEspType[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};
constexpr uint8_t kRootType[16] = {
    0x4F, 0x52, 0x55, 0x4B, 0x41, 0x47, 0x45, 0x4E,
    0x8F, 0x53, 0x52, 0x4F, 0x4F, 0x54, 0x30, 0x01
};
constexpr uint8_t kDiskGuid[16] = {
    0x53, 0x4F, 0x52, 0x4B, 0x47, 0x41, 0x4E, 0x45,
    0x80, 0x00, 0x44, 0x49, 0x53, 0x4B, 0x30, 0x01
};
constexpr uint8_t kEspGuid[16] = {
    0x53, 0x45, 0x55, 0x4B, 0x47, 0x41, 0x4E, 0x45,
    0x80, 0x00, 0x45, 0x53, 0x50, 0x30, 0x30, 0x01
};
constexpr uint8_t kRootGuid[16] = {
    0x54, 0x4F, 0x4F, 0x52, 0x47, 0x41, 0x4E, 0x45,
    0x80, 0x00, 0x52, 0x4F, 0x4F, 0x54, 0x30, 0x01
};

alignas(16) uint8_t g_entries[kEntryCount * kEntrySize];

void clear(void* memory, size_t size) {
    auto* bytes = static_cast<uint8_t*>(memory);
    for (size_t index = 0U; index < size; ++index) {
        bytes[index] = 0U;
    }
}

void copy(uint8_t* destination, const uint8_t* source, size_t size) {
    for (size_t index = 0U; index < size; ++index) {
        destination[index] = source[index];
    }
}

void put16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8U);
}

void put32(uint8_t* output, uint32_t value) {
    for (size_t index = 0U; index < 4U; ++index) {
        output[index] = static_cast<uint8_t>(value >> (index * 8U));
    }
}

void put64(uint8_t* output, uint64_t value) {
    put32(output, static_cast<uint32_t>(value));
    put32(output + 4U, static_cast<uint32_t>(value >> 32U));
}

void put_name(uint8_t* entry, const char* name) {
    for (size_t index = 0U; name[index] != '\0' && index < 36U; ++index) {
        put16(entry + 56U + index * 2U,
              static_cast<uint16_t>(static_cast<uint8_t>(name[index])));
    }
}

void build_entry(
    uint8_t* entry,
    const uint8_t type[16],
    const uint8_t unique[16],
    uint64_t first_lba,
    uint64_t last_lba,
    const char* name) {
    copy(entry, type, 16U);
    copy(entry + 16U, unique, 16U);
    put64(entry + 32U, first_lba);
    put64(entry + 40U, last_lba);
    put_name(entry, name);
}

void build_header(
    uint8_t sector[kSectorSize],
    uint64_t current_lba,
    uint64_t backup_lba,
    uint64_t last_usable_lba,
    uint64_t entries_lba,
    uint32_t entries_crc) {
    clear(sector, kSectorSize);
    const uint8_t signature[8] = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};
    copy(sector, signature, sizeof(signature));
    put32(sector + 8U, UINT32_C(0x00010000));
    put32(sector + 12U, 92U);
    put64(sector + 24U, current_lba);
    put64(sector + 32U, backup_lba);
    put64(sector + 40U, kFirstUsableLba);
    put64(sector + 48U, last_usable_lba);
    copy(sector + 56U, kDiskGuid, 16U);
    put64(sector + 72U, entries_lba);
    put32(sector + 80U, kEntryCount);
    put32(sector + 84U, kEntrySize);
    put32(sector + 88U, entries_crc);
    put32(sector + 16U, k_crc32(sector, 92U));
}

storage::block::Status write_sector(
    const storage::block::Device* device,
    uint64_t lba,
    const uint8_t sector[kSectorSize]) {
    return storage::block::write_blocks(device, lba, 1U, sector, kSectorSize);
}

Status validate_target(const storage::block::Device* device, Layout* output) {
    if (device == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    if (storage::block::validate(device) != storage::block::Status::Ok ||
        device->sector_size != kSectorSize) {
        return Status::UnsupportedGeometry;
    }
    if (device->sector_count < MINIMUM_DISK_SECTORS) {
        return Status::DiskTooSmall;
    }
    return Status::Ok;
}

} // namespace

Status prepare_install_target(
    const storage::block::Device* device,
    Layout* output) {
    const Status validation = validate_target(device, output);
    if (validation != Status::Ok) {
        return validation;
    }

    const uint64_t backup_header_lba = device->sector_count - 1U;
    const uint64_t backup_entries_lba = backup_header_lba - kEntrySectors;
    const uint64_t last_usable_lba = backup_entries_lba - 1U;
    const uint64_t esp_last_lba = ESP_FIRST_LBA + ESP_SECTOR_COUNT - 1U;
    const uint64_t root_first_lba = esp_last_lba + 1U;
    if (root_first_lba >= last_usable_lba) {
        return Status::DiskTooSmall;
    }

    clear(g_entries, sizeof(g_entries));
    build_entry(g_entries, kEspType, kEspGuid, ESP_FIRST_LBA,
                esp_last_lba, "Kurogane ESP");
    build_entry(g_entries + kEntrySize, kRootType, kRootGuid,
                root_first_lba, last_usable_lba, "Kurogane Root");
    const uint32_t entries_crc = k_crc32(g_entries, sizeof(g_entries));

    // Commit the backup GPT first, then the primary GPT, and publish the
    // protective MBR last. A failed install can safely retry this same
    // deterministic sequence after the user explicitly confirms erasure.
    for (uint64_t index = 0U; index < kEntrySectors; ++index) {
        const uint8_t* entry_sector = g_entries + index * kSectorSize;
        if (write_sector(device, backup_entries_lba + index, entry_sector) !=
            storage::block::Status::Ok ||
            write_sector(device, 2U + index, entry_sector) !=
            storage::block::Status::Ok) {
            return Status::BlockDeviceError;
        }
    }

    uint8_t sector[kSectorSize]{};
    build_header(sector, backup_header_lba, 1U, last_usable_lba,
                 backup_entries_lba, entries_crc);
    if (write_sector(device, backup_header_lba, sector) !=
        storage::block::Status::Ok) {
        return Status::BlockDeviceError;
    }
    build_header(sector, 1U, backup_header_lba, last_usable_lba, 2U,
                 entries_crc);
    if (write_sector(device, 1U, sector) != storage::block::Status::Ok) {
        return Status::BlockDeviceError;
    }

    clear(sector, sizeof(sector));
    sector[446U + 4U] = 0xEEU;
    put32(sector + 446U + 8U, 1U);
    const uint64_t protective_size = device->sector_count - 1U;
    put32(sector + 446U + 12U,
          protective_size > UINT32_MAX
              ? UINT32_MAX
              : static_cast<uint32_t>(protective_size));
    sector[510U] = 0x55U;
    sector[511U] = 0xAAU;
    if (write_sector(device, 0U, sector) != storage::block::Status::Ok ||
        storage::block::flush(device) != storage::block::Status::Ok) {
        return Status::BlockDeviceError;
    }

    *output = {
        ESP_FIRST_LBA,
        ESP_SECTOR_COUNT,
        root_first_lba,
        last_usable_lba - root_first_lba + 1U
    };
    return Status::Ok;
}

Status prepare_empty_disk(
    const storage::block::Device* device,
    Layout* output) {
    const Status validation = validate_target(device, output);
    if (validation != Status::Ok) {
        return validation;
    }

    uint8_t sector[kSectorSize]{};
    if (storage::block::read_blocks(
            device, 0U, 1U, sector, sizeof(sector)) !=
        storage::block::Status::Ok) {
        return Status::BlockDeviceError;
    }
    for (uint8_t byte : sector) {
        if (byte != 0U) {
            return Status::DiskNotEmpty;
        }
    }
    return prepare_install_target(device, output);
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidArgument: return "invalid argument";
        case Status::UnsupportedGeometry: return "requires writable 512-byte sectors";
        case Status::DiskTooSmall: return "disk must be at least 512 MiB";
        case Status::DiskNotEmpty: return "target is not blank";
        case Status::BlockDeviceError: return "block-device I/O failed";
    }
    return "unknown disk-layout status";
}

} // namespace install::disk_layout