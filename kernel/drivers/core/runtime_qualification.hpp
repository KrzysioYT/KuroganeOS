#pragma once

#include "driver_manager.hpp"

namespace drivers::runtime_qualification {

struct Result {
    bool device_claim;
    bool device_generation;
    bool device_unbind;
    bool device_remove_cleanup;
    bool device_stale_handle;
    bool device_failure_isolation;
    bool device_rebind;
    bool device_resource_boundary;
    bool driver_match;
    bool driver_attach;
    bool driver_fallback;
    bool driver_failure_cleanup;

    bool complete() const {
        return device_claim && device_generation && device_unbind &&
            device_remove_cleanup && device_stale_handle &&
            device_failure_isolation && device_rebind &&
            device_resource_boundary && driver_match && driver_attach &&
            driver_fallback && driver_failure_cleanup;
    }
};

// Exercises the production Device Model and Driver Manager with bounded
// virtual devices. The test devices are always removed before success returns.
KStatus run(Result* output);

} // namespace drivers::runtime_qualification
