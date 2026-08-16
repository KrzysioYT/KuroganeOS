#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::audio::ac97 {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    NoDevice,
    UnsupportedDevice,
    InvalidBar,
    DmaAllocationFailed,
    CodecResetFailed,
    InvalidArgument,
    BufferTooLarge,
    DeviceBusy,
    DeviceFault,
};

struct Capabilities {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    size_t maximum_frames_per_buffer;
};

// Initializes Intel 82801AA/ICH AC'97 (PCI 8086:2415), the controller exposed
// by the KuroganeOS VirtualBox reference profile. Initialization is polling
// based; no IRQ routing is required for the first PCM output backend.
Status initialize();
bool initialized();
Status initialization_status();
Capabilities capabilities();

// Submits one bounded interleaved signed PCM16 stereo buffer at 48 kHz. The
// driver copies the user/kernel source into DMA32-owned memory before starting
// the bus-master engine. The source may be released after this call returns.
Status play_pcm16_stereo(const int16_t* samples, size_t frame_count);

// Polls completion/error state. It is safe to call from the kernel main loop.
Status poll();
bool busy();
Status stop();

const char* status_message(Status status);

} // namespace drivers::audio::ac97
