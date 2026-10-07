#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::audio::hda {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    NoController,
    UnsupportedController,
    PciCommandRejected,
    BarUnavailable,
    MmioMappingFailed,
    ControllerResetTimeout,
    NoCodec,
    ImmediateCommandTimeout,
    InvalidCodecResponse,
    UnsupportedPcmPath,
    DmaAllocationFailed,
    InvalidArgument,
    BufferTooLarge,
    DeviceBusy,
    DeviceFault,
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
    uint8_t audio_function_group_node;
    uint8_t output_converter_node;
    uint8_t output_pin_node;
    uint8_t stream_tag;
};

struct Capabilities {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    size_t maximum_frames_per_buffer;
};

Status initialize();
void shutdown();

bool initialized();
const ControllerInfo* controller_info();
Capabilities capabilities();

Status set_master_volume(uint32_t percent, bool muted);
uint32_t master_volume_percent();
bool muted();

Status play_pcm16_stereo(const int16_t* samples, size_t frame_count);
Status poll();
bool busy();
Status stop();

const char* status_message(Status status);

} // namespace drivers::audio::hda
