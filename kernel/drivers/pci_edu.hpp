#pragma once

#include <stdint.h>

namespace drivers::pci_edu {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    NotFound,
    InvalidBar,
    MmioMapFailed,
    IdentityMismatch,
    MsiUnavailable,
    InterruptTimedOut,
    TeardownFailed,
};

// QEMU EDU is a real emulated PCI endpoint with both INTx and MSI delivery.
// KuroganeOS uses its documented interrupt-raise/acknowledge registers as a
// deterministic transport qualification target; no synthetic IDT invocation
// is used. Normal systems simply return NotFound.
Status initialize();
bool msi_configured();
bool qualify_msi_delivery(uint32_t spin_budget);
Status status();
const char* status_name(Status status);

} // namespace drivers::pci_edu
