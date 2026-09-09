#pragma once

#include <stdint.h>

#include "pci.hpp"

namespace pci::bar {

constexpr uint16_t COMMAND_DECODE_MASK = UINT16_C(0x0007);

enum class Kind : uint8_t {
    Io = 0,
    Memory32,
    Memory64,
};

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    UnsupportedHeader,
    DecodingEnabled,
    UpperHalf,
    UnsupportedType,
    Unimplemented,
    Unassigned,
    InvalidSize,
    MisalignedBase,
};

struct ConfigAccess {
    uint16_t (*read16)(Address address, uint8_t offset, void* context);
    uint32_t (*read32)(Address address, uint8_t offset, void* context);
    void (*write32)(
        Address address,
        uint8_t offset,
        uint32_t value,
        void* context);
    void* context;
};

struct Info {
    Kind kind;
    uint8_t index;
    uint8_t consumed_bars;
    bool prefetchable;
    uint64_t physical_address;
    uint64_t size;
};

// Decode values captured before and during the standard write-all-ones BAR
// sizing transaction. This routine never touches hardware.
Status decode(
    uint8_t index,
    uint32_t original_low,
    uint32_t original_high,
    uint32_t probe_low,
    uint32_t probe_high,
    Info* output);

// Probe one BAR only while I/O, memory and bus-master command bits are already
// disabled by the owning driver. The complete original BAR value is restored
// before this function returns, including both halves of a 64-bit BAR.
Status probe_disabled(
    Address address,
    uint8_t header_type,
    uint8_t index,
    const ConfigAccess& access,
    Info* output);
Status probe_disabled(const Device& device, uint8_t index, Info* output);

const char* status_name(Status status);

} // namespace pci::bar
