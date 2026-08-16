#pragma once

#include <stdint.h>

namespace system_metrics {

struct ActivitySnapshot {
    uint32_t cpu_percent;
    uint32_t gpu_percent;
    uint32_t disk_percent;
};

// Optional idle/busy hook reserved for later SMP accounting.
void record_loop(bool busy);

// Completed physical block operations feed the live disk activity indicator.
void record_disk_blocks(uint64_t blocks);

// UI compositor submissions feed the current GOP/software graphics activity
// indicator. This is deliberately not presented as physical GPU-core load.
void record_graphics_work(uint64_t units);

// Returns activity since the previous sample. Percentages are bounded 0..100.
ActivitySnapshot sample();

} // namespace system_metrics
