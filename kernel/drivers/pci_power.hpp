#pragma once

#include <stdint.h>

#include "pci.hpp"

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
    CapabilityMissing,
    CapabilityMalformed,
    UnsupportedState,
    VerificationFailed,
};

struct Info {
    uint8_t capability_offset;
    uint8_t version;
    bool pme_clock;
    bool d1_supported;
    bool d2_supported;
    uint8_t pme_support;
    State state;
    bool pme_enabled;
    bool pme_status;
};

struct ConfigAccess {
    uint16_t (*read16)(Address address, uint8_t offset, void* context);
    void (*write16)(
        Address address,
        uint8_t offset,
        uint16_t value,
        void* context);
    void* context;
};

bool capability_layout_valid(uint8_t offset);
Status decode(
    uint8_t capability_offset,
    uint16_t capabilities,
    uint16_t control_status,
    Info* output);
Status transition(
    Address address,
    uint8_t capability_offset,
    State target,
    const ConfigAccess& access,
    Info* output = nullptr);

Status query(const Device& device, Info* output);
Status set_state(const Device& device, State target, Info* output = nullptr);
const char* state_name(State state);
const char* status_name(Status status);

} // namespace pci::power
