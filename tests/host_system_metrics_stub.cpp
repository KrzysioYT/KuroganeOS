#include "../kernel/core/system_metrics.hpp"

namespace system_metrics {

// Host-side filesystem/image validators exercise production storage code as a
// normal macOS/Linux process. Runtime activity accounting belongs to the
// KuroganeOS kernel and must not pull scheduler/runtime dependencies into
// those validators.
void record_loop(bool busy) {
    static_cast<void>(busy);
}

void record_disk_blocks(uint64_t blocks) {
    static_cast<void>(blocks);
}

void record_graphics_work(uint64_t units) {
    static_cast<void>(units);
}

ActivitySnapshot sample() {
    return ActivitySnapshot{0U, 0U, 0U};
}

} // namespace system_metrics
