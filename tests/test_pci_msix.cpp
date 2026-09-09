#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../kernel/drivers/pci_msix.hpp"

namespace {

enum class Target : uint8_t { Config, Table };

struct Write {
    Target target;
    uint32_t offset;
    uint32_t value;
};

struct FakeDevice {
    uint8_t config[256];
    alignas(4) uint8_t table[512];
    Write writes[64];
    size_t write_count;
};

uint16_t load16(const uint8_t* bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset]) |
        (static_cast<uint16_t>(bytes[offset + 1U]) << 8U);
}

uint32_t load32(const uint8_t* bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<uint32_t>(bytes[offset + 3U]) << 24U);
}

void store16(uint8_t* bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1U] = static_cast<uint8_t>(value >> 8U);
}

void store32(uint8_t* bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1U] = static_cast<uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<uint8_t>(value >> 24U);
}

void record(FakeDevice& device, Target target, uint32_t offset, uint32_t value) {
    assert(device.write_count < 64U);
    device.writes[device.write_count++] = {target, offset, value};
}

uint16_t config_read16(pci::Address, uint8_t offset, void* context) {
    return load16(static_cast<FakeDevice*>(context)->config, offset);
}

void config_write16(
    pci::Address,
    uint8_t offset,
    uint16_t value,
    void* context) {
    auto& device = *static_cast<FakeDevice*>(context);
    record(device, Target::Config, offset, value);
    store16(device.config, offset, value);
}

uint32_t table_read32(uint32_t offset, void* context) {
    return load32(static_cast<FakeDevice*>(context)->table, offset);
}

void table_write32(uint32_t offset, uint32_t value, void* context) {
    auto& device = *static_cast<FakeDevice*>(context);
    record(device, Target::Table, offset, value);
    store32(device.table, offset, value);
}

pci::msix::ConfigAccess config_access(FakeDevice& device) {
    return {config_read16, config_write16, &device};
}

pci::msix::TableAccess table_access(FakeDevice& device) {
    return {table_read32, table_write32, &device};
}

void expect_write(
    const FakeDevice& device,
    size_t index,
    Target target,
    uint32_t offset,
    uint32_t value) {
    assert(index < device.write_count);
    assert(device.writes[index].target == target);
    assert(device.writes[index].offset == offset);
    assert(device.writes[index].value == value);
}

void test_region_validation() {
    alignas(4) uint8_t bytes[512]{};
    pci::MsiXInfo info{};
    info.table_size = 4U;
    info.table_bar = 2U;
    info.table_offset = 0x40U;
    info.pending_bit_array_bar = 2U;
    info.pending_bit_array_offset = 0x100U;
    const pci::msix::MmioRegion region{2U, 0x100000U, bytes, sizeof(bytes)};
    assert(pci::msix::validate_regions(info, region, region) ==
        pci::msix::Status::Ok);

    pci::msix::MmioRegion wrong_bar = region;
    wrong_bar.bar_index = 1U;
    assert(pci::msix::validate_regions(info, wrong_bar, region) ==
        pci::msix::Status::RegionMismatch);

    pci::msix::MmioRegion short_table = region;
    short_table.bytes = 0x70U;
    assert(pci::msix::validate_regions(info, short_table, short_table) ==
        pci::msix::Status::TableOutOfRange);

    pci::msix::MmioRegion short_pba = region;
    short_pba.bytes = 0x107U;
    assert(pci::msix::validate_regions(info, short_pba, short_pba) ==
        pci::msix::Status::PendingArrayOutOfRange);

    info.pending_bit_array_offset = 0x60U;
    assert(pci::msix::validate_regions(info, region, region) ==
        pci::msix::Status::CapabilityMalformed);

    info.table_size = 2049U;
    assert(pci::msix::validate_regions(info, region, region) ==
        pci::msix::Status::InvalidArgument);
}

void test_program_and_restore() {
    constexpr pci::Address address{0U, 5U, 0U};
    constexpr uint8_t capability = 0x50U;
    constexpr uint16_t table_size = 4U;
    constexpr uint16_t control = table_size - 1U;
    constexpr uint32_t entry = 0x80U;
    constexpr uint16_t command = 0x0007U;
    constexpr uint32_t old_low = 0x11223344U;
    constexpr uint32_t old_high = 0x55667788U;
    constexpr uint32_t old_data = 0x99AABBCDU;
    constexpr uint32_t old_vector_control = 0xA5A50001U;

    FakeDevice device{};
    store16(device.config, 0x04U, command);
    store16(device.config, capability + 2U, control);
    store32(device.table, entry, old_low);
    store32(device.table, entry + 4U, old_high);
    store32(device.table, entry + 8U, old_data);
    store32(device.table, entry + 12U, old_vector_control);

    pci::msix::ProgrammedState state{};
    assert(pci::msix::program_single(
        address,
        capability,
        table_size,
        2U,
        entry,
        0x40U,
        3U,
        config_access(device),
        table_access(device),
        &state) == pci::msix::Status::Ok);
    assert(state.active && state.command_held);
    assert(load32(device.table, entry) == 0xFEE03000U);
    assert(load32(device.table, entry + 4U) == 0U);
    assert(load32(device.table, entry + 8U) == 0x40U);
    assert(load32(device.table, entry + 12U) ==
        (old_vector_control & ~UINT32_C(1)));
    assert(load16(device.config, 0x04U) ==
        static_cast<uint16_t>(command | pci::msix::PCI_COMMAND_INTX_DISABLE));
    assert(load16(device.config, capability + 2U) ==
        static_cast<uint16_t>(control | pci::msix::CONTROL_ENABLE));

    expect_write(device, 0U, Target::Config, capability + 2U,
        control | pci::msix::CONTROL_FUNCTION_MASK);
    expect_write(device, 1U, Target::Table, entry + 12U,
        old_vector_control | pci::msix::VECTOR_MASK);
    expect_write(device, 2U, Target::Table, entry, 0xFEE03000U);
    expect_write(device, 3U, Target::Table, entry + 4U, 0U);
    expect_write(device, 4U, Target::Table, entry + 8U, 0x40U);
    expect_write(device, 5U, Target::Config, 0x04U,
        command | pci::msix::PCI_COMMAND_INTX_DISABLE);
    expect_write(device, 6U, Target::Config, capability + 2U,
        control | pci::msix::CONTROL_FUNCTION_MASK |
            pci::msix::CONTROL_ENABLE);
    expect_write(device, 7U, Target::Table, entry + 12U,
        old_vector_control & ~pci::msix::VECTOR_MASK);
    expect_write(device, 8U, Target::Config, capability + 2U,
        control | pci::msix::CONTROL_ENABLE);

    assert(pci::msix::quiesce(
        config_access(device), table_access(device), &state) ==
        pci::msix::Status::Ok);
    assert(!state.active && state.command_held);
    assert(load32(device.table, entry) == old_low);
    assert(load32(device.table, entry + 4U) == old_high);
    assert(load32(device.table, entry + 8U) == old_data);
    assert(load32(device.table, entry + 12U) == old_vector_control);
    assert(load16(device.config, capability + 2U) == control);
    assert(load16(device.config, 0x04U) ==
        static_cast<uint16_t>(command | pci::msix::PCI_COMMAND_INTX_DISABLE));
    assert(pci::msix::finish_restore(config_access(device), &state) ==
        pci::msix::Status::Ok);
    assert(load16(device.config, 0x04U) == command);
}

void test_rejections() {
    constexpr pci::Address address{0U, 5U, 0U};
    FakeDevice device{};
    pci::msix::ProgrammedState state{};
    store16(device.config, 0x52U,
        pci::msix::CONTROL_ENABLE | UINT16_C(3));
    assert(pci::msix::program_single(
        address, 0x50U, 4U, 0U, 0U, 0x40U, 0U,
        config_access(device), table_access(device), &state) ==
        pci::msix::Status::AlreadyEnabled);
    assert(device.write_count == 0U);

    device = {};
    store16(device.config, 0x52U, UINT16_C(7));
    assert(pci::msix::program_single(
        address, 0x50U, 4U, 0U, 0U, 0x40U, 0U,
        config_access(device), table_access(device), &state) ==
        pci::msix::Status::CapabilityMalformed);
    assert(pci::msix::program_single(
        address, 0x50U, 8U, 8U, 0U, 0x40U, 0U,
        config_access(device), table_access(device), &state) ==
        pci::msix::Status::InvalidArgument);
    assert(pci::msix::program_single(
        address, 0x50U, 8U, 0U, 0U, 0x80U, 0U,
        config_access(device), table_access(device), &state) ==
        pci::msix::Status::InvalidArgument);
    assert(pci::msix::program_single(
        address, 0x50U, 8U, 0U, UINT32_C(0xFFFFFFF8), 0x40U, 0U,
        config_access(device), table_access(device), &state) ==
        pci::msix::Status::InvalidArgument);
}

} // namespace

int main() {
    test_region_validation();
    test_program_and_restore();
    test_rejections();
    return 0;
}
