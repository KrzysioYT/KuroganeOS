#include "acpi_power.hpp"

#include "acpi.hpp"
#include "io.hpp"

#include <stddef.h>

namespace arch::x86_64::acpi_power {
namespace {

constexpr size_t kSdtHeaderSize = 36U;
constexpr uint32_t kMaximumTableSize = 1024U * 1024U;
constexpr size_t kFadtMinimumSize = 116U;
constexpr size_t kFadtResetRegisterOffset = 116U;
constexpr size_t kFadtResetValueOffset = 128U;
constexpr size_t kFadtXDsdtOffset = 140U;
constexpr uint32_t kFadtResetSupported = UINT32_C(1) << 10U;
constexpr uint32_t kHardwareReducedAcpi = UINT32_C(1) << 20U;
constexpr uint8_t kSystemIoSpace = 1U;
constexpr uint16_t kSleepTypeMask = UINT16_C(7) << 10U;
constexpr uint16_t kSleepEnable = UINT16_C(1) << 13U;
constexpr uint16_t kSciEnable = UINT16_C(1);
constexpr uint32_t kAcpiEnablePollBudget = UINT32_C(1000000);

Configuration g_configuration{};
uint8_t g_initialized = 0U;

uint16_t read_u16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(
            static_cast<uint16_t>(bytes[1]) << 8U);
}

uint32_t read_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8U) |
        (static_cast<uint32_t>(bytes[2]) << 16U) |
        (static_cast<uint32_t>(bytes[3]) << 24U);
}

uint64_t read_u64(const uint8_t* bytes) {
    return static_cast<uint64_t>(read_u32(bytes)) |
        (static_cast<uint64_t>(read_u32(bytes + 4U)) << 32U);
}

bool signature_equal(const uint8_t* bytes, const char signature[4]) {
    if (bytes == nullptr || signature == nullptr) return false;
    for (size_t index = 0U; index < 4U; ++index) {
        if (bytes[index] != static_cast<uint8_t>(signature[index])) return false;
    }
    return true;
}

bool checksum_valid(const uint8_t* bytes, size_t size) {
    if (bytes == nullptr || size == 0U) return false;
    uint8_t sum = 0U;
    for (size_t index = 0U; index < size; ++index) {
        sum = static_cast<uint8_t>(sum + bytes[index]);
    }
    return sum == 0U;
}

bool valid_sdt(const uint8_t* table, const char signature[4]) {
    if (table == nullptr || !signature_equal(table, signature)) return false;
    const uint32_t length = read_u32(table + 4U);
    return length >= kSdtHeaderSize && length <= kMaximumTableSize &&
        checksum_valid(table, static_cast<size_t>(length));
}

ResetRegister read_gas(const uint8_t* bytes) {
    return ResetRegister{
        bytes[0], bytes[1], bytes[2], bytes[3], read_u64(bytes + 4U)};
}

bool io_port_from_gas(
    const ResetRegister& gas,
    uint8_t minimum_width,
    uint16_t* output) {
    if (output == nullptr || gas.address_space_id != kSystemIoSpace ||
        gas.bit_offset != 0U || gas.address == 0U ||
        gas.address > UINT16_MAX ||
        (gas.bit_width != 0U && gas.bit_width < minimum_width)) {
        return false;
    }
    *output = static_cast<uint16_t>(gas.address);
    return true;
}

bool valid_reset_register(const ResetRegister& gas) {
    return gas.address_space_id == kSystemIoSpace &&
        gas.bit_width == 8U &&
        gas.bit_offset == 0U &&
        (gas.access_size == 0U || gas.access_size == 1U) &&
        gas.address != 0U &&
        gas.address <= UINT16_MAX;
}

bool decode_pkg_length(
    const uint8_t* bytes,
    size_t available,
    size_t* value,
    size_t* consumed) {
    if (bytes == nullptr || value == nullptr || consumed == nullptr ||
        available == 0U) return false;

    const uint8_t lead = bytes[0];
    const size_t following = static_cast<size_t>(lead >> 6U);
    if (following > 3U || available < following + 1U) return false;
    size_t result = following == 0U
        ? static_cast<size_t>(lead & UINT8_C(0x3F))
        : static_cast<size_t>(lead & UINT8_C(0x0F));
    for (size_t index = 0U; index < following; ++index) {
        result |= static_cast<size_t>(bytes[index + 1U])
            << (4U + index * 8U);
    }
    *value = result;
    *consumed = following + 1U;
    return true;
}

bool parse_integer(
    const uint8_t* bytes,
    size_t available,
    uint64_t* value,
    size_t* consumed) {
    if (bytes == nullptr || value == nullptr || consumed == nullptr ||
        available == 0U) return false;
    switch (bytes[0]) {
        case 0x00U:
            *value = 0U; *consumed = 1U; return true;
        case 0x01U:
            *value = 1U; *consumed = 1U; return true;
        case 0x0AU:
            if (available < 2U) return false;
            *value = bytes[1]; *consumed = 2U; return true;
        case 0x0BU:
            if (available < 3U) return false;
            *value = read_u16(bytes + 1U); *consumed = 3U; return true;
        case 0x0CU:
            if (available < 5U) return false;
            *value = read_u32(bytes + 1U); *consumed = 5U; return true;
        case 0x0EU:
            if (available < 9U) return false;
            *value = read_u64(bytes + 1U); *consumed = 9U; return true;
        default:
            return false;
    }
}

bool parse_s5(const uint8_t* dsdt, uint8_t* type_a, uint8_t* type_b) {
    if (dsdt == nullptr || type_a == nullptr || type_b == nullptr ||
        !valid_sdt(dsdt, "DSDT")) return false;

    const size_t length = static_cast<size_t>(read_u32(dsdt + 4U));
    for (size_t offset = kSdtHeaderSize; offset + 7U < length; ++offset) {
        if (dsdt[offset] != UINT8_C(0x08)) continue;
        size_t name = offset + 1U;
        if (name < length && dsdt[name] == UINT8_C(0x5C)) ++name;
        while (name < length && dsdt[name] == UINT8_C(0x5E)) ++name;
        if (name + 5U >= length ||
            dsdt[name] != static_cast<uint8_t>('_') ||
            dsdt[name + 1U] != static_cast<uint8_t>('S') ||
            dsdt[name + 2U] != static_cast<uint8_t>('5') ||
            dsdt[name + 3U] != static_cast<uint8_t>('_') ||
            dsdt[name + 4U] != UINT8_C(0x12)) {
            continue;
        }

        const size_t length_offset = name + 5U;
        size_t package_length = 0U;
        size_t length_bytes = 0U;
        if (!decode_pkg_length(
                dsdt + length_offset,
                length - length_offset,
                &package_length,
                &length_bytes) ||
            package_length < length_bytes + 1U ||
            package_length > length - length_offset) {
            continue;
        }

        const size_t package_end = length_offset + package_length;
        size_t cursor = length_offset + length_bytes;
        const uint8_t elements = dsdt[cursor++];
        if (elements < 2U || cursor >= package_end) continue;

        uint64_t first = 0U;
        uint64_t second = 0U;
        size_t consumed = 0U;
        if (!parse_integer(
                dsdt + cursor, package_end - cursor, &first, &consumed)) {
            continue;
        }
        cursor += consumed;
        if (cursor >= package_end ||
            !parse_integer(
                dsdt + cursor, package_end - cursor, &second, &consumed)) {
            continue;
        }
        if (first > 7U || second > 7U) continue;
        *type_a = static_cast<uint8_t>(first);
        *type_b = static_cast<uint8_t>(second);
        return true;
    }
    return false;
}

bool select_pm1_control(
    const uint8_t* fadt,
    size_t fadt_length,
    size_t extended_offset,
    size_t legacy_offset,
    uint16_t* output) {
    if (fadt_length >= extended_offset + 12U) {
        const ResetRegister gas = read_gas(fadt + extended_offset);
        if (io_port_from_gas(gas, 16U, output)) return true;
    }
    const uint32_t legacy = read_u32(fadt + legacy_offset);
    if (legacy == 0U || legacy > UINT16_MAX) return false;
    *output = static_cast<uint16_t>(legacy);
    return true;
}

void enable_acpi_if_needed() {
    if (!initialized() || g_configuration.pm1a_control_port == 0U ||
        (arch::in16(g_configuration.pm1a_control_port) & kSciEnable) != 0U ||
        g_configuration.smi_command_port == 0U ||
        g_configuration.smi_command_port > UINT16_MAX ||
        g_configuration.acpi_enable_value == 0U) {
        return;
    }
    arch::out8(
        static_cast<uint16_t>(g_configuration.smi_command_port),
        g_configuration.acpi_enable_value);
    for (uint32_t attempt = 0U; attempt < kAcpiEnablePollBudget; ++attempt) {
        if ((arch::in16(g_configuration.pm1a_control_port) & kSciEnable) != 0U)
            return;
        arch::pause();
    }
}

void write_reset_register(const ResetRegister& reset, uint8_t value) {
    if (!valid_reset_register(reset)) return;
    arch::out8(static_cast<uint16_t>(reset.address), value);
}

} // namespace

Status parse(const void* rsdp, Configuration* output) {
    if (rsdp == nullptr || output == nullptr) return Status::InvalidArgument;
    *output = {};

    acpi::TableView fadt_view{};
    const acpi::Status lookup = acpi::find_table(rsdp, "FACP", &fadt_view);
    if (lookup == acpi::Status::TableNotFound) return Status::FadtNotFound;
    if (lookup != acpi::Status::Ok || fadt_view.address == nullptr ||
        fadt_view.length < kFadtMinimumSize) return Status::InvalidFadt;

    const auto* fadt = static_cast<const uint8_t*>(fadt_view.address);
    const size_t fadt_length = fadt_view.length;
    const uint32_t flags = read_u32(fadt + 112U);
    Configuration staged{};
    staged.smi_command_port = read_u32(fadt + 48U);
    staged.acpi_enable_value = fadt[52U];

    if (fadt_length > kFadtResetValueOffset &&
        (flags & kFadtResetSupported) != 0U &&
        fadt_length >= kFadtResetRegisterOffset + 12U) {
        staged.reset_register = read_gas(fadt + kFadtResetRegisterOffset);
        staged.reset_supported = valid_reset_register(staged.reset_register);
        staged.reset_value = fadt[kFadtResetValueOffset];
    }

    if ((flags & kHardwareReducedAcpi) == 0U) {
        static_cast<void>(select_pm1_control(
            fadt, fadt_length, 172U, 64U, &staged.pm1a_control_port));
        static_cast<void>(select_pm1_control(
            fadt, fadt_length, 184U, 68U, &staged.pm1b_control_port));

        uint64_t dsdt_address = read_u32(fadt + 40U);
        if (fadt_length >= kFadtXDsdtOffset + 8U) {
            const uint64_t extended = read_u64(fadt + kFadtXDsdtOffset);
            if (extended != 0U) dsdt_address = extended;
        }
        if (dsdt_address == 0U || dsdt_address > UINTPTR_MAX) {
            if (!staged.reset_supported) return Status::DsdtNotFound;
        } else {
            const auto* dsdt = reinterpret_cast<const uint8_t*>(
                static_cast<uintptr_t>(dsdt_address));
            if (!valid_sdt(dsdt, "DSDT")) {
                if (!staged.reset_supported) return Status::InvalidDsdt;
            } else {
                staged.shutdown_supported =
                    staged.pm1a_control_port != 0U &&
                    parse_s5(dsdt, &staged.sleep_type_a, &staged.sleep_type_b);
            }
        }
    }

    if (!staged.shutdown_supported && !staged.reset_supported)
        return Status::NoPowerMethod;
    *output = staged;
    return staged.shutdown_supported ? Status::Ok : Status::SleepStateNotFound;
}

Status initialize(uint64_t rsdp_physical_address) {
    g_configuration = {};
    __atomic_store_n(&g_initialized, uint8_t{0U}, __ATOMIC_RELEASE);
    if (rsdp_physical_address == 0U || rsdp_physical_address > UINTPTR_MAX)
        return Status::InvalidArgument;

    Configuration staged{};
    const Status status = parse(
        reinterpret_cast<const void*>(
            static_cast<uintptr_t>(rsdp_physical_address)),
        &staged);
    if (status != Status::Ok && status != Status::SleepStateNotFound)
        return status;
    g_configuration = staged;
    __atomic_store_n(&g_initialized, uint8_t{1U}, __ATOMIC_RELEASE);
    return status;
}

bool initialized() {
    return __atomic_load_n(&g_initialized, __ATOMIC_ACQUIRE) != 0U;
}

bool shutdown_available() {
    return initialized() && g_configuration.shutdown_supported;
}

bool reset_available() {
    return initialized() && g_configuration.reset_supported;
}

const Configuration* configuration() {
    return initialized() ? &g_configuration : nullptr;
}

uint16_t compose_sleep_control(uint16_t current, uint8_t sleep_type) {
    const uint16_t encoded = static_cast<uint16_t>(
        static_cast<uint16_t>(sleep_type & UINT8_C(7)) << 10U);
    return static_cast<uint16_t>(
        (current & static_cast<uint16_t>(~kSleepTypeMask)) |
        encoded | kSleepEnable);
}

void request_poweroff() {
    if (!shutdown_available()) return;
    enable_acpi_if_needed();

    const uint16_t current_a = arch::in16(g_configuration.pm1a_control_port);
    arch::out16(
        g_configuration.pm1a_control_port,
        compose_sleep_control(current_a, g_configuration.sleep_type_a));

    if (g_configuration.pm1b_control_port != 0U) {
        const uint16_t current_b =
            arch::in16(g_configuration.pm1b_control_port);
        arch::out16(
            g_configuration.pm1b_control_port,
            compose_sleep_control(current_b, g_configuration.sleep_type_b));
    }
}

void request_reset() {
    if (!reset_available()) return;
    write_reset_register(
        g_configuration.reset_register,
        g_configuration.reset_value);
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidArgument: return "missing ACPI RSDP";
        case Status::FadtNotFound: return "ACPI FADT not found";
        case Status::InvalidFadt: return "invalid ACPI FADT";
        case Status::DsdtNotFound: return "ACPI DSDT not found";
        case Status::InvalidDsdt: return "invalid ACPI DSDT";
        case Status::SleepStateNotFound:
            return "ACPI reset available but S5 sleep state is unavailable";
        case Status::NoPowerMethod: return "no usable ACPI power method";
    }
    return "unknown ACPI power status";
}

} // namespace arch::x86_64::acpi_power
