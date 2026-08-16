#include "audio/ac97.hpp"
#include "core/driver_manager.hpp"

namespace {

constexpr uint16_t kIntelVendor = 0x8086U;
constexpr uint16_t kIchAc97Device = 0x2415U;

drivers::device::DriverId g_ac97_driver =
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
    const KStatus status = drivers::driver::register_driver(
        ac97_driver, &g_ac97_driver);
    return status == KStatus::AlreadyExists ? KStatus::Ok : status;
}
