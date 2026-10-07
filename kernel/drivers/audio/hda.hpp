#pragma once

#include <stdint.h>

namespace drivers::audio::hda {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NoController,
    UnsupportedController,
    PciCommandRejected,
    BarUnavailable,
    MmioMappingFailed,
    ControllerResetTimeout,
    NoCodec,
    ImmediateCommandTimeout,
    InvalidCodecResponse,
    ResourceReleaseFailed,
};

struct ControllerInfo {
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t output_stream_count;
    uint8_t input_stream_count;
    uint8_t bidirectional_stream_count;
    uint8_t codec_address;
    uint32_t codec_vendor_id;
    uint8_t root_start_node;
    uint8_t root_node_count;
};

Status initialize();
void shutdown();

bool initialized();
const ControllerInfo* controller_info();
const char* status_message(Status status);

} // namespace drivers::audio::hda
