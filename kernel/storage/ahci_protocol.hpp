#pragma once

#include <stddef.h>
#include <stdint.h>

namespace storage::ahci::protocol {

constexpr size_t ATA_IDENTIFY_WORD_COUNT = 256U;
constexpr size_t ATA_MODEL_CAPACITY = 40U;
constexpr size_t REGISTER_HOST_TO_DEVICE_FIS_SIZE = 64U;
constexpr uint64_t MAXIMUM_LBA48 = UINT64_C(0x0000FFFFFFFFFFFF);
constexpr uint32_t AHCI_MINIMUM_REGISTER_SPAN = UINT32_C(0x180);
constexpr uint32_t AHCI_REGISTER_SPAN = UINT32_C(0x1100);

enum class AtaCommand : uint8_t {
    IdentifyDevice = 0xEC,
    ReadDmaExt = 0x25,
    WriteDmaExt = 0x35,
    FlushCacheExt = 0xEA
};

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    UnsupportedPciBar,
    InvalidPciBarSize,
    Lba48Unsupported,
    InvalidCapacity,
    UnsupportedSectorSize,
    InvalidLba,
    InvalidSectorCount,
    UnsupportedCommand
};

struct PciBar {
    uint64_t physical_address;
    uint32_t size;
    bool prefetchable;
};

struct IdentifyInfo {
    uint32_t logical_sector_size;
    uint64_t sector_count;
    uint32_t maximum_sectors_per_page;
    char model[ATA_MODEL_CAPACITY + 1U];
};

// Decodes a 32-bit BAR from its original value and the value read while all
// address bits were written as one. BAR5 cannot hold the high half of a 64-bit
// BAR and is therefore rejected if it advertises that format.
Status decode_bar5(uint32_t original, uint32_t size_probe, PciBar* output);

Status parse_identify(
    const uint16_t words[ATA_IDENTIFY_WORD_COUNT],
    IdentifyInfo* output);

Status build_register_fis(
    AtaCommand command,
    uint64_t first_lba,
    uint16_t sector_count,
    uint8_t output[REGISTER_HOST_TO_DEVICE_FIS_SIZE]);

const char* status_message(Status status);

} // namespace storage::ahci::protocol
