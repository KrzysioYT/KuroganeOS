#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../kernel/drivers/pci_bar.hpp"

namespace {

struct Write {
    uint8_t offset;
    uint32_t value;
};

struct FakeConfig {
    uint8_t bytes[256];
    uint32_t probe_masks[6];
    bool probing[6];
    Write writes[16];
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

bool bar_index(uint8_t offset, size_t* output) {
    if (offset < 0x10U || offset > 0x24U || (offset & 3U) != 0U) {
        return false;
    }
    *output = static_cast<size_t>((offset - 0x10U) / 4U);
    return true;
}

uint16_t read16(pci::Address, uint8_t offset, void* context) {
    return load16(*static_cast<FakeConfig*>(context), offset);
}

uint32_t read32(pci::Address, uint8_t offset, void* context) {
    auto& config = *static_cast<FakeConfig*>(context);
    size_t index = 0U;
    if (bar_index(offset, &index) && config.probing[index]) {
        return config.probe_masks[index];
    }
    return load32(config, offset);
}

void write32(pci::Address, uint8_t offset, uint32_t value, void* context) {
    auto& config = *static_cast<FakeConfig*>(context);
    assert(config.write_count < 16U);
    config.writes[config.write_count++] = {offset, value};
    size_t index = 0U;
    if (bar_index(offset, &index)) {
        config.probing[index] = value == UINT32_MAX;
    }
    if (value != UINT32_MAX) store32(config, offset, value);
}

pci::bar::ConfigAccess access(FakeConfig& config) {
    return {read16, read32, write32, &config};
}

void expect_write(
    const FakeConfig& config,
    size_t index,
    uint8_t offset,
    uint32_t value) {
    assert(index < config.write_count);
    assert(config.writes[index].offset == offset);
    assert(config.writes[index].value == value);
}

void test_decode() {
    pci::bar::Info info{};
    assert(pci::bar::decode(
        5U, 0xFEBF0000U, 0U, 0xFFFFE000U, 0U, &info) ==
        pci::bar::Status::Ok);
    assert(info.kind == pci::bar::Kind::Memory32);
    assert(info.physical_address == UINT64_C(0xFEBF0000));
    assert(info.size == UINT64_C(0x2000));
    assert(info.consumed_bars == 1U && !info.prefetchable);

    assert(pci::bar::decode(
        2U, 0x0000000CU, 0x00000001U, 0xFFFF000CU, UINT32_MAX,
        &info) == pci::bar::Status::Ok);
    assert(info.kind == pci::bar::Kind::Memory64);
    assert(info.physical_address == UINT64_C(0x100000000));
    assert(info.size == UINT64_C(0x10000));
    assert(info.consumed_bars == 2U && info.prefetchable);

    assert(pci::bar::decode(
        1U, 0x0000C001U, 0U, 0x0000FF01U, 0U, &info) ==
        pci::bar::Status::Ok);
    assert(info.kind == pci::bar::Kind::Io);
    assert(info.physical_address == UINT64_C(0xC000));
    assert(info.size == UINT64_C(0x100));

    assert(pci::bar::decode(
        0U, 0x00000004U, 0U, 0xFFFFF004U, UINT32_MAX, &info) ==
        pci::bar::Status::Unassigned);
    assert(pci::bar::decode(
        0U, 0xFEBF1000U, 0U, 0xFFFFE000U, 0U, &info) ==
        pci::bar::Status::MisalignedBase);
    assert(pci::bar::decode(
        0U, 0x00000002U, 0U, 0xFFFFF002U, 0U, &info) ==
        pci::bar::Status::UnsupportedType);
    assert(pci::bar::decode(
        0U, 0xFEBF0000U, 0U, 0U, 0U, &info) ==
        pci::bar::Status::Unimplemented);
}

void test_probe_32_bit() {
    constexpr pci::Address address{0U, 3U, 0U};
    FakeConfig config{};
    store32(config, 0x24U, 0xFEBF0000U);
    config.probe_masks[5] = 0xFFFFE000U;

    pci::bar::Info info{};
    assert(pci::bar::probe_disabled(
        address, 0x00U, 5U, access(config), &info) ==
        pci::bar::Status::Ok);
    assert(info.kind == pci::bar::Kind::Memory32);
    assert(info.size == UINT64_C(0x2000));
    assert(load32(config, 0x24U) == 0xFEBF0000U);
    assert(config.write_count == 2U);
    expect_write(config, 0U, 0x24U, UINT32_MAX);
    expect_write(config, 1U, 0x24U, 0xFEBF0000U);
}

void test_probe_64_bit_and_upper_half() {
    constexpr pci::Address address{0U, 4U, 0U};
    FakeConfig config{};
    store32(config, 0x10U, 0xD0000000U);
    store32(config, 0x14U, 0x0000C001U);
    store32(config, 0x18U, 0x0000000CU);
    store32(config, 0x1CU, 0x00000001U);
    config.probe_masks[2] = 0xFFFF000CU;
    config.probe_masks[3] = UINT32_MAX;

    pci::bar::Info info{};
    assert(pci::bar::probe_disabled(
        address, 0x00U, 2U, access(config), &info) ==
        pci::bar::Status::Ok);
    assert(info.kind == pci::bar::Kind::Memory64);
    assert(info.size == UINT64_C(0x10000));
    assert(load32(config, 0x18U) == 0x0000000CU);
    assert(load32(config, 0x1CU) == 0x00000001U);
    assert(config.write_count == 4U);
    expect_write(config, 0U, 0x1CU, UINT32_MAX);
    expect_write(config, 1U, 0x18U, UINT32_MAX);
    expect_write(config, 2U, 0x1CU, 0x00000001U);
    expect_write(config, 3U, 0x18U, 0x0000000CU);

    config.write_count = 0U;
    assert(pci::bar::probe_disabled(
        address, 0x00U, 3U, access(config), &info) ==
        pci::bar::Status::UpperHalf);
    assert(config.write_count == 0U);
}

void test_probe_rejections() {
    constexpr pci::Address address{0U, 5U, 0U};
    FakeConfig config{};
    store16(config, 0x04U, pci::bar::COMMAND_DECODE_MASK);
    pci::bar::Info info{};
    assert(pci::bar::probe_disabled(
        address, 0x00U, 0U, access(config), &info) ==
        pci::bar::Status::DecodingEnabled);
    assert(config.write_count == 0U);

    store16(config, 0x04U, 0U);
    assert(pci::bar::probe_disabled(
        address, 0x02U, 0U, access(config), &info) ==
        pci::bar::Status::UnsupportedHeader);
    assert(pci::bar::probe_disabled(
        address, 0x01U, 2U, access(config), &info) ==
        pci::bar::Status::InvalidArgument);
}

} // namespace

int main() {
    test_decode();
    test_probe_32_bit();
    test_probe_64_bit_and_upper_half();
    test_probe_rejections();
    return 0;
}
