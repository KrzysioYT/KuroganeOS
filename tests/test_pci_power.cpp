#include <assert.h>
#include <stdint.h>

#include "../kernel/drivers/pci_power.hpp"

namespace {

struct FakeConfig {
    uint16_t capabilities;
    uint16_t control;
    uint16_t last_write;
    bool ignore_state_write;
};

uint16_t read16(pci::Address, uint8_t offset, void* context) {
    auto& config = *static_cast<FakeConfig*>(context);
    if (offset == 0x52U) return config.capabilities;
    if (offset == 0x54U) return config.control;
    return 0U;
}

void write16(pci::Address, uint8_t offset, uint16_t value, void* context) {
    auto& config = *static_cast<FakeConfig*>(context);
    if (offset != 0x54U) return;
    config.last_write = value;
    if (!config.ignore_state_write) {
        config.control = value;
    }
}

pci::power::ConfigAccess access(FakeConfig& config) {
    return {read16, write16, &config};
}

} // namespace

int main() {
    using pci::power::State;
    using pci::power::Status;

    assert(pci::power::capability_layout_valid(0x50U));
    assert(pci::power::capability_layout_valid(0xF8U));
    assert(!pci::power::capability_layout_valid(0xFCU));
    assert(!pci::power::capability_layout_valid(0x51U));

    pci::power::Info info{};
    const uint16_t capabilities =
        UINT16_C(3) |
        (UINT16_C(1) << 9U) |
        (UINT16_C(1) << 10U) |
        (UINT16_C(0x1F) << 11U);
    const uint16_t control =
        static_cast<uint16_t>(State::D0) |
        (UINT16_C(1) << 8U) |
        (UINT16_C(1) << 15U);
    assert(pci::power::decode(0x50U, capabilities, control, &info) == Status::Ok);
    assert(info.version == 3U);
    assert(info.d1_supported && info.d2_supported);
    assert(info.pme_enabled && info.pme_status);
    assert(info.state == State::D0);

    FakeConfig config{capabilities, control, 0U, false};
    assert(pci::power::transition(
        {0U, 1U, 0U}, 0x50U, State::D3Hot, access(config), &info) == Status::Ok);
    assert(info.state == State::D3Hot);
    assert((config.last_write & (UINT16_C(1) << 15U)) == 0U);
    assert((config.last_write & (UINT16_C(1) << 8U)) != 0U);

    config = {UINT16_C(3), static_cast<uint16_t>(State::D0), 0U, false};
    assert(pci::power::transition(
        {0U, 1U, 0U}, 0x50U, State::D1, access(config)) ==
        Status::UnsupportedState);
    assert(config.last_write == 0U);

    config = {capabilities, static_cast<uint16_t>(State::D0), 0U, true};
    assert(pci::power::transition(
        {0U, 1U, 0U}, 0x50U, State::D3Hot, access(config)) ==
        Status::VerificationFailed);

    assert(pci::power::decode(0xFCU, capabilities, control, &info) ==
        Status::CapabilityMalformed);
    assert(pci::power::decode(0x50U, 0U, control, &info) ==
        Status::CapabilityMalformed);

    return 0;
}
