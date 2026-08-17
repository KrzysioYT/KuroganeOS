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

Status initialize();
bool initialized();
Status initialization_status();
Capabilities capabilities();

// Master output control for the VirtualBox Intel ICH AC'97 codec. Percentage
// is 0..100 and is translated to the AC'97 stereo attenuation register.
Status set_master_volume(uint32_t percent, bool muted);
uint32_t master_volume_percent();
bool muted();

Status play_pcm16_stereo(const int16_t* samples, size_t frame_count);
Status poll();
bool busy();
Status stop();

const char* status_message(Status status);

} // namespace drivers::audio::ac97
