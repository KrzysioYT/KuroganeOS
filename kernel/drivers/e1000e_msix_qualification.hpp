#pragma once

#include <stdint.h>

namespace drivers::e1000e_msix_qualification {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    NotFound,
    CapabilityMalformed,
    BarProbeFailed,
    UnsupportedRegion,
    MmioMapFailed,
    MsiXUnavailable,
    InterruptTimedOut,
    TeardownFailed,
};

// This driver is linked into release kernels so CI tests the production PCI,
// MMIO, vector and interrupt code. It is never invoked by the normal boot
// path: the Steel MSI-X workflow injects the two calls into kernel/main.cpp.
// The target is a dedicated QEMU Intel 82574L (e1000e) endpoint. The bounded
// transaction restores its PCI command, MSI-X table, IVAR, interrupt mask and
// MMIO mappings before returning.
Status initialize();
bool msix_configured();
bool qualify_delivery(uint32_t spin_budget);
Status status();
const char* status_name(Status status);

} // namespace drivers::e1000e_msix_qualification
