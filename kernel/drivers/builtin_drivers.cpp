#include "audio/ac97.hpp"
#include "video/display_adapter.hpp"
#include "core/driver_manager.hpp"
#include "../core/log.hpp"

namespace {

constexpr uint16_t kIntelVendor = 0x8086U;
constexpr uint16_t kIchAc97Device = 0x2415U;

drivers::device::DriverId g_ac97_driver =
    drivers::device::INVALID_DRIVER_ID;
drivers::device::DriverId g_display_driver =
    drivers::device::INVALID_DRIVER_ID;

bool ac97_match(const drivers::device::Device& device, void*) {
    return device.bus == drivers::device::Bus::Pci &&
        device.vendor_id == kIntelVendor &&
        device.device_id == kIchAc97Device;
}

KStatus ac97_probe(
    const drivers::device::Device& device,
    uint32_t timeout_ticks,
    void*) {
    return timeout_ticks != 0U && ac97_match(device, nullptr)
        ? KStatus::Ok
        : KStatus::NotSupported;
}

KStatus ac97_attach(
    const drivers::device::Device&,
    uint32_t timeout_ticks,
    void*) {
    if (timeout_ticks == 0U) return KStatus::InvalidArgument;
    const auto status = drivers::audio::ac97::initialize();
    if (status == drivers::audio::ac97::Status::Ok ||
        status == drivers::audio::ac97::Status::AlreadyInitialized) {
        log::write(
            log::Level::Info,
            "AC97",
            "Intel ICH AC97 PCM output ready (48 kHz S16LE stereo)");
        return KStatus::Ok;
    }
    log::write(
        log::Level::Warn,
        "AC97",
        drivers::audio::ac97::status_message(status));
    switch (status) {
        case drivers::audio::ac97::Status::Ok:
        case drivers::audio::ac97::Status::AlreadyInitialized:
            return KStatus::Ok;
        case drivers::audio::ac97::Status::NoDevice:
            return KStatus::NoDevice;
        case drivers::audio::ac97::Status::DmaAllocationFailed:
            return KStatus::NoMemory;
        case drivers::audio::ac97::Status::InvalidArgument:
            return KStatus::InvalidArgument;
        case drivers::audio::ac97::Status::InvalidBar:
        case drivers::audio::ac97::Status::UnsupportedDevice:
            return KStatus::NotSupported;
        case drivers::audio::ac97::Status::CodecResetFailed:
            return KStatus::Timeout;
        case drivers::audio::ac97::Status::DeviceFault:
            return KStatus::DeviceFault;
        case drivers::audio::ac97::Status::NotInitialized:
        case drivers::audio::ac97::Status::BufferTooLarge:
        case drivers::audio::ac97::Status::DeviceBusy:
            return KStatus::BadState;
    }
    return KStatus::DeviceFault;
}

bool display_match(const drivers::device::Device& device, void*) {
    return device.bus == drivers::device::Bus::Pci &&
        device.class_code == UINT8_C(0x03);
}

KStatus display_probe(
    const drivers::device::Device& device,
    uint32_t timeout_ticks,
    void*) {
    return timeout_ticks != 0U && display_match(device, nullptr)
        ? KStatus::Ok : KStatus::NotSupported;
}

KStatus display_attach(
    const drivers::device::Device& device,
    uint32_t timeout_ticks,
    void*) {
    if (timeout_ticks == 0U) return KStatus::InvalidArgument;
    drivers::video::display_adapter::probe();
    const auto& info = drivers::video::display_adapter::info();
    if (!info.present) return KStatus::NoDevice;
    if (info.vendor_id != device.vendor_id || info.device_id != device.device_id) {
        return KStatus::NotSupported;
    }
    log::write(
        log::Level::Info,
        "DISPLAY",
        info.gop_scanout
            ? "PCI display adapter + UEFI GOP scanout / software compositor ready"
            : "PCI display adapter detected without active GOP scanout");
    if (!info.accelerated_3d) {
        log::write(
            log::Level::Info,
            "DISPLAY",
            "hardware 3D command submission is not enabled in 3.3.3-dev");
    }
    return info.gop_scanout ? KStatus::Ok : KStatus::Degraded;
}

} // namespace

extern "C" KStatus kurogane_register_builtin_drivers() {
    const drivers::driver::Descriptor ac97_driver{
        "ac97",
        90,
        250,
        ac97_match,
        ac97_probe,
        ac97_attach,
        nullptr,
        nullptr,
    };
    KStatus status = drivers::driver::register_driver(
        ac97_driver, &g_ac97_driver);
    if (status != KStatus::Ok && status != KStatus::AlreadyExists) return status;

    const drivers::driver::Descriptor display_driver{
        "redflux-display",
        80,
        250,
        display_match,
        display_probe,
        display_attach,
        nullptr,
        nullptr,
    };
    status = drivers::driver::register_driver(
        display_driver, &g_display_driver);
    return status == KStatus::AlreadyExists ? KStatus::Ok : status;
}
