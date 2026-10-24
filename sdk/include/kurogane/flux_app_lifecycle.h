#ifndef KUROGANE_FLUX_APP_LIFECYCLE_H
#define KUROGANE_FLUX_APP_LIFECYCLE_H
#include <stdint.h>
#include <kurogane/system.h>

/* Only the parent of a process may choose to restart it. Merely spawning a
 * process is not an app readiness signal. Require elapsed >= timeout, an
 * absent native surface, and a fresh explicit launch request (the caller
 * decides when to invoke this predicate).
 */
#define KU_FLUX_APP_LAUNCH_TIMEOUT_TICKS (UINT64_C(15) * KU_SYSTEM_TICKS_PER_SECOND)
static inline int ku_flux_app_retry_eligible(uint64_t started_ticks,
                                             uint64_t current_ticks,
                                             int surface_ready) {
    // UINT64_MAX means no monotonic clock sample, not a timed-out child.
    return !surface_ready &&
        started_ticks != UINT64_MAX && current_ticks != UINT64_MAX &&
        current_ticks >= started_ticks &&
        current_ticks - started_ticks >= KU_FLUX_APP_LAUNCH_TIMEOUT_TICKS;
}
#endif
