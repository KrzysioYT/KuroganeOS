#include "audio_backend.hpp"

#include "ac97.hpp"
#include "hda.hpp"

namespace drivers::audio {
namespace {

enum class Backend : uint8_t {
    None = 0,
    Hda,
    Ac97,
};

Backend active_backend() {
    if (hda::initialized()) return Backend::Hda;
    if (ac97::initialized()) return Backend::Ac97;
    return Backend::None;
}

Status map_hda(hda::Status status) {
    switch (status) {
        case hda::Status::Ok:
        case hda::Status::AlreadyInitialized:
            return Status::Ok;
        case hda::Status::NotInitialized:
        case hda::Status::NoController:
        case hda::Status::NoCodec:
            return Status::NotInitialized;
        case hda::Status::InvalidArgument:
            return Status::InvalidArgument;
        case hda::Status::BufferTooLarge:
            return Status::BufferTooLarge;
        case hda::Status::DeviceBusy:
            return Status::DeviceBusy;
        case hda::Status::UnsupportedController:
        case hda::Status::UnsupportedPcmPath:
            return Status::NotSupported;
        default:
            return Status::DeviceFault;
    }
}

Status map_ac97(ac97::Status status) {
    switch (status) {
        case ac97::Status::Ok:
        case ac97::Status::AlreadyInitialized:
            return Status::Ok;
        case ac97::Status::NotInitialized:
        case ac97::Status::NoDevice:
            return Status::NotInitialized;
        case ac97::Status::InvalidArgument:
            return Status::InvalidArgument;
        case ac97::Status::BufferTooLarge:
            return Status::BufferTooLarge;
        case ac97::Status::DeviceBusy:
            return Status::DeviceBusy;
        case ac97::Status::UnsupportedDevice:
            return Status::NotSupported;
        default:
            return Status::DeviceFault;
    }
}

} // namespace

bool initialized() { return active_backend() != Backend::None; }

Capabilities capabilities() {
    switch (active_backend()) {
        case Backend::Hda: {
            const hda::Capabilities caps = hda::capabilities();
            return {
                caps.sample_rate,
                caps.channels,
                caps.bits_per_sample,
                caps.maximum_frames_per_buffer,
            };
        }
        case Backend::Ac97: {
            const ac97::Capabilities caps = ac97::capabilities();
            return {
                caps.sample_rate,
                caps.channels,
                caps.bits_per_sample,
                caps.maximum_frames_per_buffer,
            };
        }
        case Backend::None:
            return {};
    }
    return {};
}

Status set_master_volume(uint32_t percent, bool muted_value) {
    switch (active_backend()) {
        case Backend::Hda:
            return map_hda(hda::set_master_volume(percent, muted_value));
        case Backend::Ac97:
            return map_ac97(ac97::set_master_volume(percent, muted_value));
        case Backend::None:
            return Status::NotInitialized;
    }
    return Status::NotInitialized;
}

uint32_t master_volume_percent() {
    switch (active_backend()) {
        case Backend::Hda: return hda::master_volume_percent();
        case Backend::Ac97: return ac97::master_volume_percent();
        case Backend::None: return 0U;
    }
    return 0U;
}

bool muted() {
    switch (active_backend()) {
        case Backend::Hda: return hda::muted();
        case Backend::Ac97: return ac97::muted();
        case Backend::None: return false;
    }
    return false;
}

Status play_pcm16_stereo(const int16_t* samples, size_t frame_count) {
    switch (active_backend()) {
        case Backend::Hda:
            return map_hda(hda::play_pcm16_stereo(samples, frame_count));
        case Backend::Ac97:
            return map_ac97(ac97::play_pcm16_stereo(samples, frame_count));
        case Backend::None:
            return Status::NotInitialized;
    }
    return Status::NotInitialized;
}

Status poll() {
    switch (active_backend()) {
        case Backend::Hda: return map_hda(hda::poll());
        case Backend::Ac97: return map_ac97(ac97::poll());
        case Backend::None: return Status::NotInitialized;
    }
    return Status::NotInitialized;
}

bool busy() {
    switch (active_backend()) {
        case Backend::Hda: return hda::busy();
        case Backend::Ac97: return ac97::busy();
        case Backend::None: return false;
    }
    return false;
}

Status stop() {
    switch (active_backend()) {
        case Backend::Hda: return map_hda(hda::stop());
        case Backend::Ac97: return map_ac97(ac97::stop());
        case Backend::None: return Status::NotInitialized;
    }
    return Status::NotInitialized;
}

const char* backend_name() {
    switch (active_backend()) {
        case Backend::Hda: return "hda";
        case Backend::Ac97: return "ac97";
        case Backend::None: return "none";
    }
    return "none";
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "audio backend not initialized";
        case Status::InvalidArgument: return "invalid audio argument";
        case Status::BufferTooLarge: return "audio buffer too large";
        case Status::DeviceBusy: return "audio device busy";
        case Status::NotSupported: return "audio operation not supported";
        case Status::DeviceFault: return "audio device fault";
    }
    return "unknown audio status";
}

} // namespace drivers::audio
