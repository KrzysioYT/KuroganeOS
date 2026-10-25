#include "ahci_protocol.hpp"

namespace storage::ahci::protocol {
namespace {

bool is_power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

void clear_bytes(uint8_t* bytes, size_t count) {
    for (size_t index = 0U; index < count; ++index) {
        bytes[index] = 0U;
    }
}

} // namespace

Status decode_bar5(uint32_t original, uint32_t size_probe, PciBar* output) {
    if (output == nullptr) {
        return Status::InvalidArgument;
    }
    *output = {};

    constexpr uint32_t io_space = UINT32_C(1);
    constexpr uint32_t memory_type_mask = UINT32_C(3) << 1U;
    constexpr uint32_t memory_type_32_bit = UINT32_C(0);
    constexpr uint32_t prefetchable = UINT32_C(1) << 3U;
    constexpr uint32_t address_mask = UINT32_C(0xFFFFFFF0);

    if (original == 0U || original == UINT32_MAX ||
        (original & io_space) != 0U ||
        (original & memory_type_mask) != memory_type_32_bit ||
        (original & prefetchable) != 0U) {
        return Status::UnsupportedPciBar;
    }

    const uint32_t physical_address = original & address_mask;
    const uint32_t probed_address_mask = size_probe & address_mask;
    if (physical_address == 0U || probed_address_mask == 0U) {
        return Status::InvalidPciBarSize;
    }
    const uint32_t size = (~probed_address_mask) + UINT32_C(1);
    if (!is_power_of_two(size) || size < AHCI_MINIMUM_REGISTER_SPAN ||
        (physical_address & (size - 1U)) != 0U) {
        return Status::InvalidPciBarSize;
    }

    output->physical_address = physical_address;
    output->size = size;
    output->prefetchable = false;
    return Status::Ok;
}

LinkState classify_sata_status(uint32_t sata_status) {
    constexpr uint32_t det_mask = UINT32_C(0x0F);
    constexpr uint32_t ipm_mask = UINT32_C(0x0F) << 8U;
    constexpr uint32_t det_no_device = UINT32_C(0x00);
    constexpr uint32_t det_present_no_link = UINT32_C(0x01);
    constexpr uint32_t det_present_link = UINT32_C(0x03);
    constexpr uint32_t det_offline = UINT32_C(0x04);
    constexpr uint32_t ipm_active = UINT32_C(0x01) << 8U;

    const uint32_t det = sata_status & det_mask;
    const uint32_t ipm = sata_status & ipm_mask;
    switch (det) {
        case det_no_device:
            return LinkState::NoDevice;
        case det_present_no_link:
            return LinkState::Transitional;
        case det_present_link:
            return ipm == ipm_active
                ? LinkState::Active
                : LinkState::Transitional;
        case det_offline:
            return LinkState::Offline;
        default:
            return LinkState::Unsupported;
    }
}

Status parse_identify(
    const uint16_t words[ATA_IDENTIFY_WORD_COUNT],
    IdentifyInfo* output) {
    if (words == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    *output = {};

    constexpr uint16_t validity_mask = UINT16_C(0xC000);
    constexpr uint16_t valid_value = UINT16_C(0x4000);
    constexpr uint16_t lba48_supported = UINT16_C(1) << 10U;
    if ((words[83] & validity_mask) != valid_value ||
        (words[83] & lba48_supported) == 0U) {
        return Status::Lba48Unsupported;
    }

    const uint64_t sector_count =
        static_cast<uint64_t>(words[100]) |
        (static_cast<uint64_t>(words[101]) << 16U) |
        (static_cast<uint64_t>(words[102]) << 32U) |
        (static_cast<uint64_t>(words[103]) << 48U);
    if (sector_count == 0U || sector_count > MAXIMUM_LBA48 + UINT64_C(1)) {
        return Status::InvalidCapacity;
    }

    uint32_t logical_sector_size = 512U;
    constexpr uint16_t sector_words_valid = UINT16_C(1) << 14U;
    constexpr uint16_t sector_words_longer = UINT16_C(1) << 12U;
    constexpr uint16_t sector_words_invalid = UINT16_C(1) << 15U;
    const uint16_t sector_description = words[106];
    if ((sector_description & sector_words_valid) != 0U &&
        (sector_description & sector_words_invalid) == 0U &&
        (sector_description & sector_words_longer) != 0U) {
        const uint32_t logical_words =
            static_cast<uint32_t>(words[117]) |
            (static_cast<uint32_t>(words[118]) << 16U);
        if (logical_words > UINT32_MAX / 2U) {
            return Status::UnsupportedSectorSize;
        }
        logical_sector_size = logical_words * 2U;
    }

    if (logical_sector_size < 512U || logical_sector_size > 4096U ||
        !is_power_of_two(logical_sector_size) ||
        (4096U % logical_sector_size) != 0U) {
        return Status::UnsupportedSectorSize;
    }

    size_t model_length = ATA_MODEL_CAPACITY;
    for (size_t index = 0U; index < ATA_MODEL_CAPACITY / 2U; ++index) {
        const uint16_t word = words[27U + index];
        output->model[index * 2U] = static_cast<char>(word >> 8U);
        output->model[index * 2U + 1U] =
            static_cast<char>(word & UINT16_C(0xFF));
    }
    while (model_length != 0U &&
           (output->model[model_length - 1U] == ' ' ||
            output->model[model_length - 1U] == '\0')) {
        --model_length;
    }
    output->model[model_length] = '\0';
    output->logical_sector_size = logical_sector_size;
    output->sector_count = sector_count;
    output->maximum_sectors_per_page = 4096U / logical_sector_size;
    return Status::Ok;
}

Status build_register_fis(
    AtaCommand command,
    uint64_t first_lba,
    uint16_t sector_count,
    uint8_t output[REGISTER_HOST_TO_DEVICE_FIS_SIZE]) {
    if (output == nullptr) {
        return Status::InvalidArgument;
    }
    clear_bytes(output, REGISTER_HOST_TO_DEVICE_FIS_SIZE);

    bool address_command = false;
    switch (command) {
        case AtaCommand::IdentifyDevice:
        case AtaCommand::FlushCacheExt:
            if (first_lba != 0U || sector_count != 0U) {
                return Status::InvalidArgument;
            }
            break;
        case AtaCommand::ReadDmaExt:
        case AtaCommand::WriteDmaExt:
            address_command = true;
            break;
        default:
            return Status::UnsupportedCommand;
    }

    if (address_command) {
        if (first_lba > MAXIMUM_LBA48) {
            return Status::InvalidLba;
        }
        if (sector_count == 0U) {
            return Status::InvalidSectorCount;
        }
        const uint64_t count = static_cast<uint64_t>(sector_count);
        if (count - UINT64_C(1) > MAXIMUM_LBA48 - first_lba) {
            return Status::InvalidLba;
        }
    }

    output[0] = UINT8_C(0x27); // Register FIS - host to device.
    output[1] = UINT8_C(0x80); // Command bit, port multiplier zero.
    output[2] = static_cast<uint8_t>(command);
    if (address_command) {
        output[4] = static_cast<uint8_t>(first_lba);
        output[5] = static_cast<uint8_t>(first_lba >> 8U);
        output[6] = static_cast<uint8_t>(first_lba >> 16U);
        output[7] = UINT8_C(0x40); // LBA mode.
        output[8] = static_cast<uint8_t>(first_lba >> 24U);
        output[9] = static_cast<uint8_t>(first_lba >> 32U);
        output[10] = static_cast<uint8_t>(first_lba >> 40U);
        output[12] = static_cast<uint8_t>(sector_count);
        output[13] = static_cast<uint8_t>(sector_count >> 8U);
    }
    return Status::Ok;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok:
            return "ok";
        case Status::InvalidArgument:
            return "invalid AHCI protocol argument";
        case Status::UnsupportedPciBar:
            return "BAR5 is not a supported 32-bit memory BAR";
        case Status::InvalidPciBarSize:
            return "BAR5 size or alignment is invalid";
        case Status::Lba48Unsupported:
            return "ATA device does not support LBA48";
        case Status::InvalidCapacity:
            return "ATA device reports an invalid capacity";
        case Status::UnsupportedSectorSize:
            return "ATA logical-sector size is unsupported";
        case Status::InvalidLba:
            return "ATA LBA is invalid";
        case Status::InvalidSectorCount:
            return "ATA sector count is invalid";
        case Status::UnsupportedCommand:
            return "unsupported ATA command";
    }
    return "unknown AHCI protocol status";
}

} // namespace storage::ahci::protocol
