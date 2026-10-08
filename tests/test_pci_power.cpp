#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
    uint8_t pmcsr_offset;
    uint32_t waits[32];
    size_t wait_count;
    uint32_t pending_wait;
    bool fail_wait;
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
    auto& config = *static_cast<FakeConfig*>(context);
    assert(config.pending_wait == 0U); // No accesses before settling.
    return load16(config, offset);
}

void write16(pci::Address, uint8_t offset, uint16_t value, void* context) {
    auto& config = *static_cast<FakeConfig*>(context);
    assert(config.pending_wait == 0U);
    assert(config.write_count < 32U);
    config.writes[config.write_count++] = {offset, value};
    if (offset == config.pmcsr_offset) {
        const uint16_t old = load16(config, offset);
        const auto from = old & 3U;
        const auto to = value & 3U;
        assert(to == 0U || to >= from);
        if (from != to) {
            config.pending_wait = (from == 3U || to == 3U) ? 10000U :
                ((from == 2U || to == 2U) ? 200U : 0U);
        }
        if (config.ignore_pmcsr_writes) return;
        // Model actual write-one-to-clear PME status, not ordinary RAM.
        const uint16_t event = static_cast<uint16_t>(
            (old & pci::power::PMCSR_PME_STATUS) & ~value);
        store16(config, offset, static_cast<uint16_t>(
            (value & ~pci::power::PMCSR_PME_STATUS) | event));
    } else {
        store16(config, offset, value);
    }
}

bool wait_us(uint32_t duration, void* context) {
    auto& config = *static_cast<FakeConfig*>(context);
    assert(config.wait_count < 32U);
    config.waits[config.wait_count++] = duration;
    assert(duration >= config.pending_wait);
    if (config.fail_wait) return false;
    config.pending_wait = 0U;
    return true;
}

pci::power::ConfigAccess access(FakeConfig& config) {
    return {read16, write16, &config, wait_us};
}

void seed_pm(
    FakeConfig& config,
    uint8_t capability,
    uint16_t pmcap,
    uint16_t pmcsr) {
    store16(config, 0x04U, 0U);
    config.pmcsr_offset = static_cast<uint8_t>(capability + 4U);
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
    assert((programmed & pci::power::PMCSR_PME_STATUS) != 0U);
    assert((config.writes[0].value & pci::power::PMCSR_PME_STATUS) == 0U);

    assert(pci::power::restore(access(config), &transaction) ==
        pci::power::Status::Ok);
    assert(!transaction.active);
    const uint16_t restored =
        load16(config, static_cast<uint8_t>(cap + 4U));
    assert((restored & pci::power::PMCSR_POWER_STATE_MASK) == 0U);
    assert((restored & pci::power::PMCSR_PME_ENABLE) != 0U);
    assert((restored & pci::power::PMCSR_PME_STATUS) != 0U);
    assert((config.writes[1].value & pci::power::PMCSR_PME_STATUS) == 0U);
    assert(config.wait_count == 2U);
    assert(config.waits[0] == 10000U && config.waits[1] == 10000U);
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
    assert(transaction.active);
    assert(pci::power::restore(access(config), &transaction) ==
        pci::power::Status::Ok);
    assert(!transaction.active);
}

void test_transition_matrix_and_restore() {
    using namespace pci::power;
    for (unsigned from = 0U; from < 4U; ++from) {
        for (unsigned to = 0U; to < 4U; ++to) {
            FakeConfig config{};
            seed_pm(config, 0x50U, 0x0603U, static_cast<uint16_t>(from));
            Transaction transaction{};
            const auto result = transition({0U, 1U, 0U}, 0x50U,
                static_cast<State>(to), access(config), &transaction);
            if (to != 0U && to < from) {
                assert(result == Status::InvalidTransition);
                assert(!transaction.active && config.write_count == 0U);
                continue;
            }
            assert(result == Status::Ok && transaction.active);
            const size_t before_restore = config.write_count;
            assert(restore(access(config), &transaction) == Status::Ok);
            assert(!transaction.active);
            assert((load16(config, 0x54U) & 3U) == from);
            if (from == to) assert(config.write_count == 0U);
            if (from != 0U && to > from) {
                assert((config.writes[before_restore].value & 3U) == 0U);
                assert(config.write_count == before_restore + 2U);
            }
        }
    }
}

void test_all_low_power_states_require_quiesce() {
    using namespace pci::power;
    for (unsigned state = 1U; state <= 3U; ++state) {
        FakeConfig config{};
        seed_pm(config, 0x50U, 0x0603U, 0U);
        store16(config, 0x04U, PCI_COMMAND_BUS_MASTER);
        Transaction transaction{};
        assert(transition({0U, 1U, 0U}, 0x50U, static_cast<State>(state),
            access(config), &transaction) == Status::BusMasterActive);
        assert(!transaction.active && config.write_count == 0U);

        // Waking is allowed, but restoring the low-power state must recheck DMA.
        store16(config, 0x54U, static_cast<uint16_t>(state));
        assert(transition({0U, 1U, 0U}, 0x50U, State::D0,
            access(config), &transaction) == Status::Ok);
        const size_t writes = config.write_count;
        assert(restore(access(config), &transaction) == Status::BusMasterActive);
        assert(transaction.active && config.write_count == writes);
        store16(config, 0x04U, 0U);
        assert(restore(access(config), &transaction) == Status::Ok);
    }
}

void test_delay_failure_and_token_ownership() {
    using namespace pci::power;
    FakeConfig config{};
    seed_pm(config, 0x50U, 0x0603U, 0U);
    Transaction transaction{};
    auto untimed = access(config);
    untimed.wait_us = nullptr;
    assert(transition({0U, 1U, 0U}, 0x50U, State::D3Hot,
        untimed, &transaction) == Status::DelayUnavailable);
    assert(!transaction.active && config.write_count == 0U);
    config.fail_wait = true;
    assert(transition({0U, 1U, 0U}, 0x50U, State::D3Hot,
        access(config), &transaction) == Status::DelayFailed);
    assert(transaction.active && transaction.pending_delay_us == 10000U);
    assert(transition({0U, 2U, 0U}, 0x60U, State::D0,
        access(config), &transaction) == Status::AlreadyActive);
    assert(transaction.address.slot == 1U);
    assert(restore(untimed, &transaction) == Status::DelayUnavailable);
    assert(restore(access(config), &transaction) == Status::DelayFailed);
    assert(config.write_count == 1U);
    config.fail_wait = false;
    assert(restore(access(config), &transaction) == Status::Ok);
    assert(!transaction.active && (load16(config, 0x54U) & 3U) == 0U);
    assert(restore(access(config), &transaction) == Status::NotActive);
}

void test_restore_failure_and_removed_device() {
    using namespace pci::power;
    FakeConfig config{};
    seed_pm(config, 0x50U, 0x0603U, 0U);
    Transaction transaction{};
    assert(transition({0U, 1U, 0U}, 0x50U, State::D3Hot,
        access(config), &transaction) == Status::Ok);
    store16(config, 0x50U, 0xFFFFU);
    assert(restore(access(config), &transaction) == Status::CapabilityMalformed);
    assert(transaction.active && config.write_count == 1U);
    store16(config, 0x50U, 0x0001U);
    config.ignore_pmcsr_writes = true;
    assert(restore(access(config), &transaction) == Status::VerificationFailed);
    assert(transaction.active);
    config.ignore_pmcsr_writes = false;
    assert(restore(access(config), &transaction) == Status::Ok);
    assert(!transaction.active);
}

} // namespace

int main() {
    test_inspection_and_supported_states();
    test_d3hot_requires_dma_quiesce_and_preserves_pme();
    test_unsupported_and_malformed_capabilities();
    test_write_verification();
    test_transition_matrix_and_restore();
    test_all_low_power_states_require_quiesce();
    test_delay_failure_and_token_ownership();
    test_restore_failure_and_removed_device();
    puts("PCI power transitions, timing, W1C and recovery: PASS");
    return 0;
}
