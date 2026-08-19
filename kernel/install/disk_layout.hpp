#pragma once

#include <stdint.h>

#include "../storage/block_device.hpp"

namespace install::disk_layout {

constexpr uint64_t MINIMUM_DISK_SECTORS = 1024U * 1024U;
constexpr uint64_t ESP_FIRST_LBA = 2048U;
constexpr uint64_t ESP_SECTOR_COUNT = 131072U;

struct Layout {
    uint64_t esp_first_lba;
    uint64_t esp_sector_count;
    uint64_t root_first_lba;
    uint64_t root_sector_count;
};

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    UnsupportedGeometry,
    DiskTooSmall,
    DiskNotEmpty,
    BlockDeviceError
};

// Destructive installer path. The caller must obtain explicit erase
// authorization before calling this function. Existing GPT/MBR/filesystem
// metadata may be replaced so an interrupted KuroganeOS installation can be
// retried on the same target disk.
Status prepare_install_target(
    const storage::block::Device* device,
    Layout* output);

// Conservative helper retained for diagnostics/tests that specifically need a
// blank-media contract. It refuses non-zero LBA 0, then delegates to the same
// deterministic GPT writer used by the real installer.
Status prepare_empty_disk(
    const storage::block::Device* device,
    Layout* output);

const char* status_message(Status status);

} // namespace install::disk_layout