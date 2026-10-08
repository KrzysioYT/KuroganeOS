#include <assert.h>
#include <stdint.h>

#include "../kernel/drivers/virtio/pci_transport.hpp"

namespace {

struct FakeConfig {
    uint8_t bytes[256];
};

uint8_t read8(pci::Address, uint8_t offset, void* context) {
    return static_cast<FakeConfig*>(context)->bytes[offset];
}

uint16_t read16(pci::Address, uint8_t offset, void* context) {
    const auto* bytes = static_cast<FakeConfig*>(context)->bytes;
    return static_cast<uint16_t>(bytes[offset]) |
        (static_cast<uint16_t>(bytes[offset + 1U]) << 8U);
}

uint32_t read32(pci::Address, uint8_t offset, void* context) {
    const auto* bytes = static_cast<FakeConfig*>(context)->bytes;
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<uint32_t>(bytes[offset + 3U]) << 24U);
}

void store16(FakeConfig& config, uint8_t offset, uint16_t value) {
    config.bytes[offset] = static_cast<uint8_t>(value);
    config.bytes[offset + 1U] = static_cast<uint8_t>(value >> 8U);
}

void store32(FakeConfig& config, uint8_t offset, uint32_t value) {
    config.bytes[offset] = static_cast<uint8_t>(value);
    config.bytes[offset + 1U] = static_cast<uint8_t>(value >> 8U);
    config.bytes[offset + 2U] = static_cast<uint8_t>(value >> 16U);
    config.bytes[offset + 3U] = static_cast<uint8_t>(value >> 24U);
}

void add_capability(
    FakeConfig& config,
    uint8_t at,
    uint8_t next,
    uint8_t length,
    uint8_t type,
    uint8_t bar,
    uint32_t offset,
    uint32_t region_length,
    uint32_t notify_multiplier = 0U) {
    store32(
        config,
        at,
        UINT32_C(0x09) |
            (static_cast<uint32_t>(next) << 8U) |
            (static_cast<uint32_t>(length) << 16U) |
            (static_cast<uint32_t>(type) << 24U));
    store32(config, static_cast<uint8_t>(at + 4U), bar);
    store32(config, static_cast<uint8_t>(at + 8U), offset);
    store32(config, static_cast<uint8_t>(at + 12U), region_length);
    if (length >= 20U) {
        store32(
            config,
            static_cast<uint8_t>(at + 16U),
            notify_multiplier);
    }
}

drivers::virtio::pci_transport::ConfigAccess access(FakeConfig& config) {
    return {read8, read16, read32, &config};
}

FakeConfig valid_layout() {
    FakeConfig config{};
    store16(config, 0x06U, UINT16_C(1) << 4U);
    config.bytes[0x34U] = 0x40U;
    add_capability(config, 0x40U, 0x54U, 16U, 1U, 4U, 0x0000U, 56U);
    add_capability(config, 0x54U, 0x68U, 20U, 2U, 4U, 0x1000U, 64U, 4U);
    add_capability(config, 0x68U, 0U, 16U, 4U, 4U, 0x2000U, 16U);
    return config;
}

} // namespace

int main() {
    using namespace drivers::virtio::pci_transport;
    const pci::Address address{0U, 2U, 0U};

    FakeConfig config = valid_layout();
    Layout layout{};
    assert(discover(address, 0x00U, access(config), &layout) == Status::Ok);
    assert(layout.common.present && layout.common.bar == 4U);
    assert(layout.common.length == 56U);
    assert(layout.notify.present && layout.notify.notify_multiplier == 4U);
    assert(layout.device.present && layout.device.offset == 0x2000U);
    assert(!layout.isr.present);
    assert(region_valid(layout.common, 4096U));
    assert(!region_valid(layout.common, 32U));

    config = valid_layout();
    store16(config, 0x06U, 0U);
    assert(discover(address, 0x00U, access(config), &layout) ==
        Status::NoCapabilityList);

    config = valid_layout();
    config.bytes[0x55U] = 0x40U;
    assert(discover(address, 0x00U, access(config), &layout) ==
        Status::MalformedList);

    config = valid_layout();
    config.bytes[0x42U] = 12U;
    assert(discover(address, 0x00U, access(config), &layout) ==
        Status::MalformedList);

    config = valid_layout();
    config.bytes[0x41U] = 0U;
    assert(discover(address, 0x00U, access(config), &layout) ==
        Status::MissingRequiredCapability);

    config = valid_layout();
    store32(config, 0x48U, UINT32_MAX);
    store32(config, 0x4CU, 2U);
    assert(discover(address, 0x00U, access(config), &layout) ==
        Status::MalformedList);

    config = valid_layout();
    assert(discover(address, 0x02U, access(config), &layout) ==
        Status::UnsupportedHeader);

    return 0;
}
