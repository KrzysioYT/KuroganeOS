#pragma once

#include <stdint.h>

namespace system_metrics {

struct ActivitySnapshot {
    uint32_t cpu_percent;
    uint32_t disk_percent;
};

// Called once for every main kernel-loop iteration. `busy` means the loop
// performed scheduler, userspace, input, USB, timer or network work instead of
// going directly back to the CPU halt path.
void record_loop(bool busy);

// FAT32 records completed physical sector operations here. This deliberately
// measures live storage activity, not filesystem capacity usage.
void record_disk_blocks(uint64_t blocks);

// Returns activity since the previous sample. Percentages are bounded 0..100.
// The intended consumer is the desktop Performance widget at roughly 1 Hz.
ActivitySnapshot sample();

} // namespace system_metrics
