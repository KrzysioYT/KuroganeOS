#pragma once

#include "pci.hpp"

#include <stdint.h>

namespace pci::power {

enum class State : uint8_t {
    D0 = 0,
    D1 = 1,
    D2 = 2,
    D3Hot = 3,
};

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    CapabilityMalformed,
    UnsupportedState,
    BusMasterActive,
    VerificationFailed,
    NotActive,
    AlreadyActive,
    InvalidTransition,
    DelayUnavailable,
    DelayFailed,
};

struct ConfigAccess {
    uint16_t (*read16)(Address address, uint8_t offset, void* context);
    void (*write16)(Address address, uint8_t offset, uint16_t value, void* context);
    void* context;
    // Must wait at least the requested duration; return false on a bounded
    // timer failure. No config/MMIO accesses are allowed during this wait.
    bool (*wait_us)(uint32_t microseconds, void* context) = nullptr;
};

struct CapabilityInfo {
    uint8_t offset;
    uint8_t version;
    bool d1_supported;
    bool d2_supported;
    uint8_t pme_support;
    State current_state;
};

struct Transaction {
    Address address;
    uint8_t capability_offset;
    uint16_t original_pmcsr;
    State target_state;
    bool active;
    uint32_t pending_delay_us;
};

constexpr uint16_t PCI_COMMAND_BUS_MASTER = UINT16_C(1) << 2U;
constexpr uint16_t PMCSR_POWER_STATE_MASK = UINT16_C(0x0003);
constexpr uint16_t PMCSR_PME_ENABLE = UINT16_C(1) << 8U;
constexpr uint16_t PMCSR_PME_STATUS = UINT16_C(1) << 15U;

// Callers serialize the entire transaction and quiesce device DMA before
// entering any low-power state. This API restores PMCSR only: D3hot may reset
// BARs/device state, which the driver must save/restore before resuming I/O.
// Zero-initialize Transaction. An active transaction must be restored before
// reuse, including after a failed write verification or settling delay.

Status inspect_capability(
    Address address,
    uint8_t capability_offset,
    const ConfigAccess& access,
    CapabilityInfo* output);

Status transition(
    Address address,
    uint8_t capability_offset,
    State target,
    const ConfigAccess& access,
    Transaction* output);

Status restore(
    const ConfigAccess& access,
    Transaction* transaction);

const char* status_name(Status status);

} // namespace pci::power
