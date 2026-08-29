#include "ac97.hpp"
#include "ac97_buffer_layout.hpp"

#include "../../arch/x86_64/io.hpp"
#include "../pci.hpp"
#include "../../storage/dma.hpp"

namespace drivers::audio::ac97 {
namespace {

constexpr uint16_t kIntelVendor = 0x8086U;
constexpr uint16_t kIchAc97Device = 0x2415U;
constexpr uint32_t kSampleRate = 48000U;
constexpr uint8_t kChannels = 2U;
constexpr uint8_t kBitsPerSample = 16U;

constexpr uint16_t kMixerReset = 0x00U;
constexpr uint16_t kMixerMasterVolume = 0x02U;
constexpr uint16_t kMixerPcmOutVolume = 0x18U;
constexpr uint16_t kMixerExtendedAudioId = 0x28U;
constexpr uint16_t kMixerExtendedAudioStatus = 0x2AU;
constexpr uint16_t kMixerPcmFrontDacRate = 0x2CU;
constexpr uint16_t kExtendedVariableRate = UINT16_C(1) << 0U;
constexpr uint16_t kMixerMute = UINT16_C(1) << 15U;
constexpr uint16_t kMixerAttenuationMask = UINT16_C(0x1F);

constexpr uint16_t kPcmOutBdbase = 0x10U;
constexpr uint16_t kPcmOutCiv = 0x14U;
constexpr uint16_t kPcmOutLvi = 0x15U;
constexpr uint16_t kPcmOutStatus = 0x16U;
constexpr uint16_t kPcmOutPicb = 0x18U;
constexpr uint16_t kPcmOutPiv = 0x1AU;
constexpr uint16_t kPcmOutControl = 0x1BU;

constexpr uint8_t kControlRun = UINT8_C(1) << 0U;
constexpr uint8_t kControlReset = UINT8_C(1) << 1U;
constexpr uint16_t kStatusDmaHalted = UINT16_C(1) << 0U;
constexpr uint16_t kStatusCurrentEqualsLast = UINT16_C(1) << 1U;
constexpr uint16_t kStatusLastValidInterrupt = UINT16_C(1) << 2U;
constexpr uint16_t kStatusCompletionInterrupt = UINT16_C(1) << 3U;
constexpr uint16_t kStatusFifoError = UINT16_C(1) << 4U;
constexpr uint16_t kStatusWriteOneToClear =
    kStatusLastValidInterrupt |
    kStatusCompletionInterrupt |
    kStatusFifoError;

constexpr uint32_t kDescriptorInterruptOnCompletion = UINT32_C(1) << 31U;
constexpr uint32_t kDescriptorBufferUnderrunPolicy = UINT32_C(1) << 30U;
constexpr uint32_t kResetPollBudget = 100000U;
constexpr uint32_t kCodecPollBudget = 100000U;

struct __attribute__((packed)) BufferDescriptor {
    uint32_t address;
    uint32_t length_control;
};

static_assert(sizeof(BufferDescriptor) == 8U, "AC97 BDL entry ABI");
static_assert(detail::kDescriptorCount * sizeof(BufferDescriptor) <= detail::kDmaPageBytes,
              "AC97 BDL must fit one DMA page");

const pci::Device* g_pci_device = nullptr;
uint16_t g_mixer_base = 0U;
uint16_t g_bus_master_base = 0U;
storage::dma::Page g_bdl_page{};
storage::dma::Page g_pcm_pages[detail::kDescriptorCount]{};
bool g_initialized = false;
bool g_busy = false;
Status g_status = Status::NotInitialized;
uint32_t g_master_volume_percent = 100U;
bool g_muted = false;

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

void copy_bytes(void* destination, const void* source, size_t size) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t index = 0U; index < size; ++index) output[index] = input[index];
}

uint16_t mixer_port(uint16_t offset) {
    return static_cast<uint16_t>(g_mixer_base + offset);
}

uint16_t bus_port(uint16_t offset) {
    return static_cast<uint16_t>(g_bus_master_base + offset);
}

void mixer_write(uint16_t offset, uint16_t value) {
    arch::out16(mixer_port(offset), value);
}

uint16_t mixer_read(uint16_t offset) {
    return arch::in16(mixer_port(offset));
}

uint8_t bus_read8(uint16_t offset) {
    return arch::in8(bus_port(offset));
}

uint16_t bus_read16(uint16_t offset) {
    return arch::in16(bus_port(offset));
}

void bus_write8(uint16_t offset, uint8_t value) {
    arch::out8(bus_port(offset), value);
}

void bus_write16(uint16_t offset, uint16_t value) {
    arch::out16(bus_port(offset), value);
}

void bus_write32(uint16_t offset, uint32_t value) {
    arch::out32(bus_port(offset), value);
}

bool valid_io_bar(uint64_t address, bool is_io, uint16_t span) {
    return is_io && address != 0U && address <= UINT16_MAX &&
        static_cast<uint64_t>(span) <= UINT16_MAX - address;
}

void apply_master_volume() {
    const uint32_t bounded = g_master_volume_percent > 100U
        ? 100U : g_master_volume_percent;
    const uint16_t attenuation = static_cast<uint16_t>(
        ((100U - bounded) * static_cast<uint32_t>(kMixerAttenuationMask) + 50U) /
        100U);
    uint16_t value = static_cast<uint16_t>(
        attenuation | static_cast<uint16_t>(attenuation << 8U));
    if (g_muted) value = static_cast<uint16_t>(value | kMixerMute);
    mixer_write(kMixerMasterVolume, value);
}

bool reset_pcm_engine() {
    bus_write8(kPcmOutControl, 0U);
    bus_write8(kPcmOutControl, kControlReset);
    for (uint32_t attempt = 0U; attempt < kResetPollBudget; ++attempt) {
        if ((bus_read8(kPcmOutControl) & kControlReset) == 0U) {
            bus_write16(kPcmOutStatus, kStatusWriteOneToClear);
            return true;
        }
        arch::pause();
    }
    return false;
}

bool reset_codec() {
    mixer_write(kMixerReset, 0U);
    for (uint32_t attempt = 0U; attempt < kCodecPollBudget; ++attempt) {
        const uint16_t master = mixer_read(kMixerMasterVolume);
        if (master != UINT16_MAX) {
            mixer_write(kMixerPcmOutVolume, 0U);
            apply_master_volume();

            const uint16_t extended = mixer_read(kMixerExtendedAudioId);
            if ((extended & kExtendedVariableRate) != 0U) {
                const uint16_t status = mixer_read(kMixerExtendedAudioStatus);
                mixer_write(
                    kMixerExtendedAudioStatus,
                    static_cast<uint16_t>(status | kExtendedVariableRate));
                mixer_write(
                    kMixerPcmFrontDacRate,
                    static_cast<uint16_t>(kSampleRate));
            }
            return true;
        }
        arch::pause();
    }
    return false;
}

void release_dma() {
    for (size_t index = 0U; index < detail::kDescriptorCount; ++index) {
        if (g_pcm_pages[index].allocated) {
            static_cast<void>(storage::dma::release_page(&g_pcm_pages[index]));
        }
    }
    if (g_bdl_page.allocated) {
        static_cast<void>(storage::dma::release_page(&g_bdl_page));
    }
}

} // namespace

Status initialize() {
    if (g_initialized) return Status::AlreadyInitialized;

    g_status = Status::NotInitialized;
    g_pci_device = pci::find(kIntelVendor, kIchAc97Device);
    if (g_pci_device == nullptr) {
        g_status = Status::NoDevice;
        return g_status;
    }

    bool mixer_is_io = false;
    bool bus_is_io = false;
    const uint64_t mixer = pci::bar_address(*g_pci_device, 0U, &mixer_is_io);
    const uint64_t bus = pci::bar_address(*g_pci_device, 1U, &bus_is_io);
    if (!valid_io_bar(mixer, mixer_is_io, 0x40U) ||
        !valid_io_bar(bus, bus_is_io, 0x40U)) {
        g_status = Status::InvalidBar;
        return g_status;
    }
    g_mixer_base = static_cast<uint16_t>(mixer);
    g_bus_master_base = static_cast<uint16_t>(bus);

    uint16_t command = pci::read16(g_pci_device->address, 0x04U);
    command = static_cast<uint16_t>(command | UINT16_C(0x0005));
    pci::write16(g_pci_device->address, 0x04U, command);

    if (storage::dma::allocate_page(false, &g_bdl_page) != storage::dma::Status::Ok) {
        release_dma();
        g_status = Status::DmaAllocationFailed;
        return g_status;
    }
    for (size_t index = 0U; index < detail::kDescriptorCount; ++index) {
        if (storage::dma::allocate_page(false, &g_pcm_pages[index]) !=
            storage::dma::Status::Ok) {
            release_dma();
            g_status = Status::DmaAllocationFailed;
            return g_status;
        }
    }

    if (g_bdl_page.physical_address >= storage::dma::DMA32_ADDRESS_LIMIT) {
        release_dma();
        g_status = Status::DmaAllocationFailed;
        return g_status;
    }
    for (size_t index = 0U; index < detail::kDescriptorCount; ++index) {
        if (g_pcm_pages[index].physical_address >= storage::dma::DMA32_ADDRESS_LIMIT) {
            release_dma();
            g_status = Status::DmaAllocationFailed;
            return g_status;
        }
    }

    clear_bytes(g_bdl_page.virtual_address, detail::kDmaPageBytes);
    for (size_t index = 0U; index < detail::kDescriptorCount; ++index) {
        clear_bytes(g_pcm_pages[index].virtual_address, detail::kDmaPageBytes);
    }

    if (!reset_codec() || !reset_pcm_engine()) {
        release_dma();
        g_status = Status::CodecResetFailed;
        return g_status;
    }

    bus_write32(
        kPcmOutBdbase,
        static_cast<uint32_t>(g_bdl_page.physical_address));
    bus_write8(kPcmOutLvi, 0U);
    static_cast<void>(bus_read8(kPcmOutCiv));
    static_cast<void>(bus_read8(kPcmOutPiv));
    static_cast<void>(bus_read16(kPcmOutPicb));

    g_busy = false;
    g_initialized = true;
    g_status = Status::Ok;
    return Status::Ok;
}

bool initialized() { return g_initialized; }
Status initialization_status() { return g_status; }

Capabilities capabilities() {
    return {kSampleRate, kChannels, kBitsPerSample, detail::kMaximumFrames};
}

Status set_master_volume(uint32_t percent, bool muted_value) {
    if (!g_initialized) return Status::NotInitialized;
    if (percent > 100U) return Status::InvalidArgument;
    g_master_volume_percent = percent;
    g_muted = muted_value;
    apply_master_volume();
    return Status::Ok;
}

uint32_t master_volume_percent() { return g_master_volume_percent; }
bool muted() { return g_muted; }

Status play_pcm16_stereo(const int16_t* samples, size_t frame_count) {
    if (!g_initialized) return Status::NotInitialized;
    if (samples == nullptr || frame_count == 0U) return Status::InvalidArgument;
    if (!detail::frame_count_supported(frame_count)) return Status::BufferTooLarge;
    if (g_busy) {
        const Status poll_status = poll();
        if (poll_status == Status::DeviceFault) return poll_status;
        if (g_busy) return Status::DeviceBusy;
    }

    if (!reset_pcm_engine()) {
        g_status = Status::DeviceFault;
        return g_status;
    }

    auto* descriptors = static_cast<BufferDescriptor*>(g_bdl_page.virtual_address);
    clear_bytes(descriptors, detail::kDescriptorCount * sizeof(BufferDescriptor));

    const auto* source = reinterpret_cast<const uint8_t*>(samples);
    const size_t descriptor_count = detail::descriptor_count_for_frames(frame_count);
    size_t source_offset = 0U;
    for (size_t index = 0U; index < descriptor_count; ++index) {
        const size_t descriptor_frames =
            detail::frames_for_descriptor(frame_count, index);
        const size_t descriptor_bytes = descriptor_frames * detail::kBytesPerFrame;

        copy_bytes(
            g_pcm_pages[index].virtual_address,
            source + source_offset,
            descriptor_bytes);
        source_offset += descriptor_bytes;

        descriptors[index].address =
            static_cast<uint32_t>(g_pcm_pages[index].physical_address);
        uint32_t length_control = static_cast<uint32_t>(
            detail::sample_words_for_frames(descriptor_frames));
        if (index + 1U == descriptor_count) {
            length_control |=
                kDescriptorInterruptOnCompletion |
                kDescriptorBufferUnderrunPolicy;
        }
        descriptors[index].length_control = length_control;
    }

    __asm__ volatile("sfence" : : : "memory");
    bus_write16(kPcmOutStatus, kStatusWriteOneToClear);
    bus_write32(
        kPcmOutBdbase,
        static_cast<uint32_t>(g_bdl_page.physical_address));
    bus_write8(
        kPcmOutLvi,
        static_cast<uint8_t>(descriptor_count - 1U));
    bus_write8(kPcmOutControl, kControlRun);
    g_busy = true;
    g_status = Status::Ok;
    return Status::Ok;
}

Status poll() {
    if (!g_initialized) return Status::NotInitialized;
    if (!g_busy) return Status::Ok;

    const uint16_t status = bus_read16(kPcmOutStatus);
    if ((status & kStatusFifoError) != 0U) {
        bus_write16(kPcmOutStatus, kStatusWriteOneToClear);
        bus_write8(kPcmOutControl, 0U);
        g_busy = false;
        g_status = Status::DeviceFault;
        return g_status;
    }

    if ((status & (kStatusCompletionInterrupt |
                   kStatusLastValidInterrupt |
                   kStatusCurrentEqualsLast |
                   kStatusDmaHalted)) != 0U) {
        bus_write16(kPcmOutStatus, kStatusWriteOneToClear);
        if ((status & kStatusDmaHalted) != 0U ||
            (status & kStatusLastValidInterrupt) != 0U) {
            bus_write8(kPcmOutControl, 0U);
            g_busy = false;
        }
    }
    return Status::Ok;
}

bool busy() { return g_busy; }

Status stop() {
    if (!g_initialized) return Status::NotInitialized;
    bus_write8(kPcmOutControl, 0U);
    if (!reset_pcm_engine()) {
        g_status = Status::DeviceFault;
        g_busy = false;
        return g_status;
    }
    bus_write32(
        kPcmOutBdbase,
        static_cast<uint32_t>(g_bdl_page.physical_address));
    g_busy = false;
    g_status = Status::Ok;
    return Status::Ok;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::NotInitialized: return "not initialized";
        case Status::NoDevice: return "Intel ICH AC97 device not found";
        case Status::UnsupportedDevice: return "unsupported AC97 device";
        case Status::InvalidBar: return "invalid AC97 I/O BAR";
        case Status::DmaAllocationFailed: return "AC97 DMA32 allocation failed";
        case Status::CodecResetFailed: return "AC97 codec or PCM reset failed";
        case Status::InvalidArgument: return "invalid AC97 argument";
        case Status::BufferTooLarge: return "AC97 PCM buffer too large";
        case Status::DeviceBusy: return "AC97 PCM engine is busy";
        case Status::DeviceFault: return "AC97 device fault";
    }
    return "unknown AC97 status";
}

} // namespace drivers::audio::ac97
