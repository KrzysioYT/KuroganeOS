#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../kernel/drivers/pci_msi.hpp"

namespace {

struct Write {
    uint8_t offset;
    uint8_t width;
    uint32_t value;
};

struct FakeConfig {
    uint8_t bytes[256];
    Write writes[64];
    size_t write_count;
};

uint16_t load16(const FakeConfig& config, uint8_t offset) {
    return static_cast<uint16_t>(config.bytes[offset]) |
        (static_cast<uint16_t>(config.bytes[offset + 1U]) << 8U);
}

uint32_t load32(const FakeConfig& config, uint8_t offset) {
    return static_cast<uint32_t>(config.bytes[offset]) |
        (static_cast<uint32_t>(config.bytes[offset + 1U]) << 8U) |
        (static_cast<uint32_t>(config.bytes[offset + 2U]) << 16U) |
        (static_cast<uint32_t>(config.bytes[offset + 3U]) << 24U);
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

uint16_t read16(pci::Address, uint8_t offset, void* context) {
    return load16(*static_cast<FakeConfig*>(context), offset);
}

uint32_t read32(pci::Address, uint8_t offset, void* context) {
    return load32(*static_cast<FakeConfig*>(context), offset);
}

void write16(pci::Address, uint8_t offset, uint16_t value, void* context) {
    auto& config = *static_cast<FakeConfig*>(context);
    assert(config.write_count < 64U);
    config.writes[config.write_count++] = {offset, 2U, value};
    store16(config, offset, value);
}

void write32(pci::Address, uint8_t offset, uint32_t value, void* context) {
    auto& config = *static_cast<FakeConfig*>(context);
    assert(config.write_count < 64U);
    config.writes[config.write_count++] = {offset, 4U, value};
    store32(config, offset, value);
}

pci::msi::ConfigAccess access(FakeConfig& config) {
    return {read16, read32, write16, write32, &config};
}

size_t find_write(
    const FakeConfig& config,
    uint8_t offset,
    uint8_t width,
    uint32_t value,
    size_t start = 0U) {
    for (size_t index = start; index < config.write_count; ++index) {
        const Write& write = config.writes[index];
        if (write.offset == offset && write.width == width &&
            write.value == value) {
            return index;
        }
    }
    return config.write_count;
}

void test_64_bit_masked_transaction() {
    constexpr uint8_t capability = 0x50U;
    constexpr uint16_t control =
        (UINT16_C(1) << 7U) | (UINT16_C(1) << 8U) | UINT16_C(0x0006);
    constexpr uint16_t command = 0x0007U;
    constexpr uint32_t old_address_low = 0x11223344U;
    constexpr uint32_t old_address_high = 0x55667788U;
    constexpr uint16_t old_data = 0x99AAU;
    constexpr uint32_t old_mask = 0xA5A50001U;
    const pci::Address address{0U, 3U, 0U};

    FakeConfig config{};
    store16(config, 0x04U, command);
    store16(config, capability + 2U, control);
    store32(config, capability + 4U, old_address_low);
    store32(config, capability + 8U, old_address_high);
    store16(config, capability + 12U, old_data);
    store32(config, capability + 16U, old_mask);

    pci::msi::ProgrammedState state{};
    assert(pci::msi::program_single(
        address, capability, 0x40U, 2U, access(config), &state) ==
        pci::msi::Status::Ok);
    assert(state.active && state.command_held);
    assert(state.address_64_bit && state.per_vector_masking);
    assert(load32(config, capability + 4U) == 0xFEE02000U);
    assert(load32(config, capability + 8U) == 0U);
    assert(load16(config, capability + 12U) == 0x40U);
    assert(load16(config, 0x04U) ==
        static_cast<uint16_t>(command | pci::msi::PCI_COMMAND_INTX_DISABLE));
    assert((load16(config, capability + 2U) & UINT16_C(1)) != 0U);
    assert((load16(config, capability + 2U) & UINT16_C(0x70)) == 0U);
    assert(load32(config, capability + 16U) == (old_mask & ~UINT32_C(1)));

    const size_t mask_first = find_write(
        config, capability + 16U, 4U, old_mask | UINT32_C(1));
    const size_t data_write = find_write(config, capability + 12U, 2U, 0x40U);
    const size_t enable_write = find_write(
        config,
        capability + 2U,
        2U,
        static_cast<uint16_t>((control & ~UINT16_C(0x71)) | UINT16_C(1)));
    const size_t unmask_write = find_write(
        config, capability + 16U, 4U, old_mask & ~UINT32_C(1), enable_write);
    assert(mask_first < data_write);
    assert(data_write < enable_write);
    assert(enable_write < unmask_write);

    assert(pci::msi::quiesce(access(config), &state) == pci::msi::Status::Ok);
    assert(!state.active && state.command_held);
    assert(load32(config, capability + 4U) == old_address_low);
    assert(load32(config, capability + 8U) == old_address_high);
    assert(load16(config, capability + 12U) == old_data);
    assert(load32(config, capability + 16U) == old_mask);
    assert(load16(config, capability + 2U) == control);
    assert(load16(config, 0x04U) ==
        static_cast<uint16_t>(command | pci::msi::PCI_COMMAND_INTX_DISABLE));

    assert(pci::msi::finish_restore(access(config), &state) ==
        pci::msi::Status::Ok);
    assert(!state.command_held);
    assert(load16(config, 0x04U) == command);
    assert(pci::msi::quiesce(access(config), &state) ==
        pci::msi::Status::NotActive);
    assert(pci::msi::finish_restore(access(config), &state) ==
        pci::msi::Status::NotActive);
}

void test_32_bit_unmasked_transaction() {
    constexpr uint8_t capability = 0x60U;
    constexpr uint16_t control = 0x0004U;
    FakeConfig config{};
    store16(config, 0x04U, 0x0003U);
    store16(config, capability + 2U, control);
    store32(config, capability + 4U, 0xCAFEBABEU);
    store16(config, capability + 8U, 0x1234U);

    pci::msi::ProgrammedState state{};
    const pci::Address address{0U, 4U, 1U};
    assert(pci::msi::program_single(
        address, capability, 0xEFU, 0xFFU, access(config), &state) ==
        pci::msi::Status::Ok);
    assert(!state.address_64_bit && !state.per_vector_masking);
    assert(load32(config, capability + 4U) == 0xFEEFF000U);
    assert(load16(config, capability + 8U) == 0x00EFU);
    assert(pci::msi::quiesce(access(config), &state) == pci::msi::Status::Ok);
    assert(pci::msi::finish_restore(access(config), &state) ==
        pci::msi::Status::Ok);
    assert(load32(config, capability + 4U) == 0xCAFEBABEU);
    assert(load16(config, capability + 8U) == 0x1234U);
}

void test_rejections() {
    FakeConfig config{};
    pci::msi::ProgrammedState state{};
    const pci::Address address{0U, 1U, 0U};
    store16(config, 0x52U, UINT16_C(1));
    assert(pci::msi::program_single(
        address, 0x50U, 0x40U, 0U, access(config), &state) ==
        pci::msi::Status::AlreadyEnabled);

    config = {};
    store16(config, 0xF2U, (UINT16_C(1) << 7U) | (UINT16_C(1) << 8U));
    assert(pci::msi::program_single(
        address, 0xF0U, 0x40U, 0U, access(config), &state) ==
        pci::msi::Status::CapabilityMalformed);
    assert(config.write_count == 0U);

    assert(pci::msi::program_single(
        address, 0x50U, 0x80U, 0U, access(config), &state) ==
        pci::msi::Status::InvalidArgument);
    assert(pci::msi::program_single(
        address, 0x50U, 0x40U, 256U, access(config), &state) ==
        pci::msi::Status::InvalidArgument);

    state = {};
    state.active = true;
    state.command_held = true;
    state.capability_offset = 0xF0U;
    state.address_64_bit = true;
    state.per_vector_masking = true;
    assert(pci::msi::quiesce(access(config), &state) ==
        pci::msi::Status::CapabilityMalformed);
    assert(pci::msi::finish_restore(access(config), &state) ==
        pci::msi::Status::CapabilityMalformed);
}

} // namespace

int main() {
    test_64_bit_masked_transaction();
    test_32_bit_unmasked_transaction();
    test_rejections();
    return 0;
}
