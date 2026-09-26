#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::audio::hda::protocol {

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    UnsupportedCapabilities,
    UnsupportedRingSize,
    UnsupportedPcmFormat,
    InvalidBufferDescriptor,
    InvalidResponse,
};

struct ControllerCapabilities {
    uint8_t output_stream_count;
    uint8_t input_stream_count;
    uint8_t bidirectional_stream_count;
    uint8_t serial_data_out_count;
    bool supports_64_bit_addressing;
};

Status decode_global_capabilities(
    uint16_t raw_gcap,
    ControllerCapabilities* output);

struct RingSize {
    uint16_t entry_count;
    uint8_t selector;
};

// CORBSIZE/RIRBSIZE bits 6:4 advertise 256/16/2-entry support and bits 1:0
// select the active size. Choose the largest supported size not exceeding the
// caller's bounded maximum.
Status select_ring_size(
    uint8_t size_register,
    uint16_t maximum_entries,
    RingSize* output);

Status build_verb_12(
    uint8_t codec_address,
    uint8_t node_id,
    uint16_t verb,
    uint8_t payload,
    uint32_t* output);

struct RirbResponse {
    uint32_t response;
    uint8_t codec_address;
    bool unsolicited;
};

Status parse_rirb_entry(
    uint64_t raw_entry,
    RirbResponse* output);

Status build_pcm_format(
    uint32_t sample_rate_hz,
    uint8_t bits_per_sample,
    uint8_t channels,
    uint16_t* output_format);

struct BufferDescriptor {
    uint64_t address;
    uint32_t length;
    uint32_t flags;
};

// HDA BDL buffers start at a 128-byte boundary and have an even byte length.
// IOC is bit 0 of the final DWORD; all remaining flag bits stay zero.
Status build_buffer_descriptor(
    uint64_t physical_address,
    uint32_t length,
    bool interrupt_on_completion,
    BufferDescriptor* output);

const char* status_message(Status status);

} // namespace drivers::audio::hda::protocol
