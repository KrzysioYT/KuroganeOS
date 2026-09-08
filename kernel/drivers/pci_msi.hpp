#pragma once

#include <stdint.h>

#include "pci.hpp"
#include "../arch/x86_64/hardware_vectors.hpp"
#include "../arch/x86_64/interrupts.hpp"

namespace pci::msi {

constexpr uint32_t MESSAGE_ADDRESS_BASE = UINT32_C(0xFEE00000);
constexpr uint16_t PCI_COMMAND_INTX_DISABLE = UINT16_C(1) << 10U;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    LocalApicUnavailable,
    CapabilityMissing,
    CapabilityMalformed,
    AlreadyEnabled,
    VectorUnavailable,
    StaleRoute,
    NotActive,
};

struct ConfigAccess {
    uint16_t (*read16)(Address address, uint8_t offset, void* context);
    uint32_t (*read32)(Address address, uint8_t offset, void* context);
    void (*write16)(
        Address address,
        uint8_t offset,
        uint16_t value,
        void* context);
    void (*write32)(
        Address address,
        uint8_t offset,
        uint32_t value,
        void* context);
    void* context;
};

struct ProgrammedState {
    Address address;
    uint8_t capability_offset;
    bool address_64_bit;
    bool per_vector_masking;
    bool active;
    bool command_held;
    uint16_t original_command;
    uint16_t original_control;
    uint16_t original_data;
    uint32_t original_address_low;
    uint32_t original_address_high;
    uint32_t original_mask;
};

struct Route {
    arch::x86_64::hardware_vectors::Lease vector;
    ProgrammedState programmed;
    bool active;
};

// These transaction primitives are public for deterministic host regression.
// Normal drivers use enable()/disable() so vector ownership and device state
// cannot be torn down in the wrong order.
Status program_single(
    Address address,
    uint8_t capability_offset,
    uint8_t vector,
    uint32_t destination_apic_id,
    const ConfigAccess& access,
    ProgrammedState* output);
Status quiesce(const ConfigAccess& access, ProgrammedState* state);
Status finish_restore(const ConfigAccess& access, ProgrammedState* state);

Status enable(
    const Device& device,
    arch::x86_64::interrupts::InterruptHandler handler,
    Route* output);
Status disable(Route* route);

uint32_t message_address(uint8_t destination_apic_id);
uint16_t message_data(uint8_t vector);
const char* status_name(Status status);

} // namespace pci::msi
