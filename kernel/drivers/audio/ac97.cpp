#include "ac97.hpp"

#include "../../arch/x86_64/io.hpp"
#include "../../terminal.hpp"
#include "../pci.hpp"
#include "../../storage/dma.hpp"

namespace drivers::audio::ac97 {
namespace {

constexpr uint16_t kIntelVendor = 0x8086U;
constexpr uint16_t kIchAc97Device = 0x2415U;
constexpr uint32_t kSampleRate = 48000U;
constexpr uint8_t kChannels = 2U;
constexpr uint8_t kBitsPerSample = 16U;
constexpr size_t kBytesPerFrame = 4U;
constexpr size_t kMaximumFrames = 4096U / kBytesPerFrame;

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
constexpr uint16_t kPcmOutLvi = 0x15U;
constexpr uint16_t kPcmOutStatus = 0x16U;
constexpr uint16_t kPcmOutControl = 0x1BU;
constexpr uint8_t kPciCommand = 0x04U;
constexpr uint16_t kPciIoSpace = UINT16_C(1) << 0U;
constexpr uint16_t kPciBusMaster = UINT16_C(1) << 2U;

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
constexpr uint32_t kPcmProbePollBudget = 8000000U;

struct __attribute__((packed)) BufferDescriptor {
    uint32_t address;
    uint32_t length_control;
};

static_assert(sizeof(BufferDescriptor) == 8U, "AC97 BDL entry ABI");
static_assert(32U * sizeof(BufferDescriptor) <= 4096U,
              "AC97 BDL must fit one DMA page");

const pci::Device* g_pci_device = nullptr;
uint16_t g_mixer_base = 0U;
uint16_t g_bus_master_base = 0U;
storage::dma::Page g_bdl_page{};
storage::dma::Page g_pcm_page{};
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
        const uint8_t control = bus_read8(kPcmOutControl);
        const uint16_t status = bus_read16(kPcmOutStatus);
        if (control == UINT8_MAX || status == UINT16_MAX) return false;
        if ((control & (kControlReset | kControlRun)) == 0U &&
            (status & kStatusDmaHalted) != 0U) {
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
    if (g_pcm_page.allocated) {
        static_cast<void>(storage::dma::release_page(&g_pcm_page));
    }
    if (g_bdl_page.allocated) {
        static_cast<void>(storage::dma::release_page(&g_bdl_page));
    }
}

void start_pcm(size_t sample_words) {
    auto* descriptors = static_cast<BufferDescriptor*>(g_bdl_page.virtual_address);
    clear_bytes(descriptors, 32U * sizeof(BufferDescriptor));
    descriptors[0].address = static_cast<uint32_t>(g_pcm_page.physical_address);
    descriptors[0].length_control =
        static_cast<uint32_t>(sample_words) |
        kDescriptorInterruptOnCompletion |
        kDescriptorBufferUnderrunPolicy;

    __asm__ volatile("sfence" : : : "memory");
    bus_write16(kPcmOutStatus, kStatusWriteOneToClear);
    bus_write32(kPcmOutBdbase, static_cast<uint32_t>(g_bdl_page.physical_address));
    bus_write8(kPcmOutLvi, 0U);
    bus_write8(kPcmOutControl, kControlRun);
}

bool pcm_probe_completed() {
    for (uint32_t attempt = 0U; attempt < kPcmProbePollBudget; ++attempt) {
        const uint16_t status = bus_read16(kPcmOutStatus);
        if ((status & kStatusFifoError) != 0U) return false;
        // DCH alone could be the initial halted state. Require completion
        // of the final descriptor before accepting this DMA buffer.
        constexpr uint16_t completed = kStatusDmaHalted | kStatusLastValidInterrupt;
        if ((status & completed) == completed) return true;
        arch::pause();
    }
    return false;
}

Status fail_initialization(Status status, uint16_t restore_command) {
    pci::write16(g_pci_device->address, kPciCommand, restore_command);
    release_dma();
    g_status = status;
    return g_status;
}

} // namespace

Status initialize() {
    if (g_initialized) return Status::AlreadyInitialized;
    if (g_bdl_page.allocated || g_pcm_page.allocated) return Status::DeviceFault;

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

    const pci::Address address = g_pci_device->address;
    const uint16_t command = pci::read16(address, kPciCommand);
    if (command == UINT16_MAX) {
        g_status = Status::DeviceFault;
        return g_status;
    }
    const uint16_t io_only = static_cast<uint16_t>((command | kPciIoSpace) & ~kPciBusMaster);
    constexpr uint16_t decode_bits = kPciIoSpace | kPciBusMaster;
    pci::write16(address, kPciCommand, io_only);
    if ((pci::read16(address, kPciCommand) & decode_bits) != kPciIoSpace) {
        return fail_initialization(Status::DeviceFault, command);
    }
    if (!reset_pcm_engine() || !reset_codec()) {
        return fail_initialization(Status::CodecResetFailed,
            static_cast<uint16_t>(command & ~kPciBusMaster));
    }

    if (storage::dma::allocate_page(false, &g_bdl_page) !=
            storage::dma::Status::Ok ||
        storage::dma::allocate_page(false, &g_pcm_page) !=
            storage::dma::Status::Ok) {
        return fail_initialization(Status::DmaAllocationFailed, command);
    }
    pci::write16(address, kPciCommand, static_cast<uint16_t>(command | decode_bits));
    const uint16_t enabled = pci::read16(address, kPciCommand);
    if (enabled == UINT16_MAX || (enabled & decode_bits) != decode_bits) {
        return fail_initialization(Status::DeviceFault, command);
    }

    constexpr size_t probe_bytes = 1024U;
    clear_bytes(g_pcm_page.virtual_address, probe_bytes);
    start_pcm(probe_bytes / sizeof(int16_t));
    const bool completed = pcm_probe_completed();
    if (!reset_pcm_engine()) {
        // Without an acknowledged engine reset, leave the hardware's DMA
        // pages pinned even if we asked PCI to disable bus mastering.
        pci::write16(address, kPciCommand,
            static_cast<uint16_t>(command & ~kPciBusMaster));
        terminal::println("[TEST] ac97_pcm_dma: FAIL (engine reset; DMA retained)");
        g_status = Status::DeviceFault;
        return g_status;
    }
    if (!completed) {
        terminal::println("[TEST] ac97_pcm_dma: FAIL (no DMA completion)");
        return fail_initialization(Status::DeviceFault, command);
    }
    terminal::println("[TEST] ac97_pcm_dma: PASS");

    g_busy = false;
    g_initialized = true;
    g_status = Status::Ok;
    return Status::Ok;
}

bool initialized() { return g_initialized; }
Status initialization_status() { return g_status; }

Capabilities capabilities() {
    return {kSampleRate, kChannels, kBitsPerSample, kMaximumFrames};
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
    if (frame_count > kMaximumFrames) return Status::BufferTooLarge;
    if (g_busy) {
        const Status poll_status = poll();
        if (poll_status == Status::DeviceFault) return poll_status;
        if (g_busy) return Status::DeviceBusy;
    }

    const size_t bytes = frame_count * kBytesPerFrame;
    copy_bytes(g_pcm_page.virtual_address, samples, bytes);

    start_pcm(bytes / sizeof(int16_t));
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
