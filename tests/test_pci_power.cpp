#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../kernel/drivers/pci_power.hpp"

namespace {

struct Write {
    uint8_t offset;
    uint16_t value;
};

struct FakeConfig {
    uint8_t bytes[256];
    Write writes[32];
    size_t write_count;
    bool ignore_pmcsr_writes;
};

uint16_t load16(const FakeConfig& config, uint8_t offset) {
    return static_cast<uint16_t>(config.bytes[offset]) |
        (static_cast<uint16_t>(config.bytes[offset + 1U]) << 8U);
}

void store16(FakeConfig& config, uint8_t offset, uint16_t value) {
    config.bytes[offset] = static_cast<uint8_t>(value);
    config.bytes[offset + 1U] = static_cast<uint8_t>(value >> 8U);
}

uint16_t read16(pci::Address, uint8_t offset, void* context) {
    return load16(*static_cast<FakeConfig*>(context), offset);
}

void write16(pci::Address, uint8_t offset, uint16_t value, void* context) {
    auto& config = *static_cast<FakeConfig*>(context);
    assert(config.write_count < 32U);
    config.writes[config.write_count++] = {offset, value};
    if (config.ignore_pmcsr_writes && offset >= 0x44U) return;
    store16(config, offset, value);
}

pci::power::ConfigAccess access(FakeConfig& config) {
    return {read16, write16, &config};
}

void seed_pm(
    FakeConfig& config,
    uint8_t capability,
    uint16_t pmcap,
    uint16_t pmcsr) {
    store16(config, 0x04U, 0U);
    store16(config, capability, UINT16_C(0x0001));
    store16(config, static_cast<uint8_t>(capability + 2U), pmcap);
    store16(config, static_cast<uint8_t>(capability + 4U), pmcsr);
}

void test_inspection_and_supported_states() {
    constexpr uint8_t cap = 0x50U;
    FakeConfig config{};
    seed_pm(
        config,
        cap,
        UINT16_C(0x0003) | (UINT16_C(1) << 9U) |
            (UINT16_C(1) << 10U) | UINT16_C(0xA800),
        UINT16_C(0x0102));

    pci::power::CapabilityInfo info{};
    assert(pci::power::inspect_capability(
        {0U, 3U, 0U}, cap, access(config), &info) ==
        pci::power::Status::Ok);
    assert(info.version == 3U);
    assert(info.d1_supported);
    assert(info.d2_supported);
    assert(info.pme_support == 0x15U);
    assert(info.current_state == pci::power::State::D2);
}

void test_d3hot_requires_dma_quiesce_and_preserves_pme() {
    constexpr uint8_t cap = 0x60U;
    const pci::Address address{0U, 4U, 0U};
    FakeConfig config{};
    seed_pm(
        config,
        cap,
        UINT16_C(0x0003),
        pci::power::PMCSR_PME_ENABLE | pci::power::PMCSR_PME_STATUS);

    store16(config, 0x04U, pci::power::PCI_COMMAND_BUS_MASTER);
    pci::power::Transaction transaction{};
    assert(pci::power::transition(
        address, cap, pci::power::State::D3Hot, access(config), &transaction) ==
        pci::power::Status::BusMasterActive);
    assert(config.write_count == 0U);

    store16(config, 0x04U, 0U);
    assert(pci::power::transition(
        address, cap, pci::power::State::D3Hot, access(config), &transaction) ==
        pci::power::Status::Ok);
    assert(transaction.active);
    const uint16_t programmed =
        load16(config, static_cast<uint8_t>(cap + 4U));
    assert((programmed & pci::power::PMCSR_POWER_STATE_MASK) == 3U);
    assert((programmed & pci::power::PMCSR_PME_ENABLE) != 0U);
    assert((programmed & pci::power::PMCSR_PME_STATUS) == 0U);

    assert(pci::power::restore(access(config), &transaction) ==
        pci::power::Status::Ok);
    assert(!transaction.active);
    const uint16_t restored =
        load16(config, static_cast<uint8_t>(cap + 4U));
    assert((restored & pci::power::PMCSR_POWER_STATE_MASK) == 0U);
    assert((restored & pci::power::PMCSR_PME_ENABLE) != 0U);
    assert((restored & pci::power::PMCSR_PME_STATUS) == 0U);
}

void test_unsupported_and_malformed_capabilities() {
    constexpr uint8_t cap = 0x70U;
    FakeConfig config{};
    seed_pm(config, cap, UINT16_C(0x0003), 0U);
    pci::power::Transaction transaction{};
    assert(pci::power::transition(
        {0U, 5U, 0U}, cap, pci::power::State::D1, access(config), &transaction) ==
        pci::power::Status::UnsupportedState);

    store16(config, cap, UINT16_C(0x0005));
    pci::power::CapabilityInfo info{};
    assert(pci::power::inspect_capability(
        {0U, 5U, 0U}, cap, access(config), &info) ==
        pci::power::Status::CapabilityMalformed);
    assert(pci::power::inspect_capability(
        {0U, 5U, 0U}, 0xFCU, access(config), &info) ==
        pci::power::Status::CapabilityMalformed);
}

void test_write_verification() {
    constexpr uint8_t cap = 0x80U;
    FakeConfig config{};
    seed_pm(config, cap, UINT16_C(0x0003), 0U);
    config.ignore_pmcsr_writes = true;
    pci::power::Transaction transaction{};
    assert(pci::power::transition(
        {0U, 6U, 0U}, cap, pci::power::State::D3Hot, access(config), &transaction) ==
        pci::power::Status::VerificationFailed);
    assert(!transaction.active);
}

} // namespace

int main() {
    test_inspection_and_supported_states();
    test_d3hot_requires_dma_quiesce_and_preserves_pme();
    test_unsupported_and_malformed_capabilities();
    test_write_verification();
    return 0;
}
