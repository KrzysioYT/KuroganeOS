#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::audio {

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    InvalidArgument,
    BufferTooLarge,
    DeviceBusy,
    NotSupported,
    DeviceFault,
};

struct Capabilities {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    size_t maximum_frames_per_buffer;
};

bool initialized();
Capabilities capabilities();

Status set_master_volume(uint32_t percent, bool muted);
uint32_t master_volume_percent();
bool muted();

Status play_pcm16_stereo(const int16_t* samples, size_t frame_count);
Status poll();
bool busy();
Status stop();

const char* backend_name();
const char* status_message(Status status);

} // namespace drivers::audio
