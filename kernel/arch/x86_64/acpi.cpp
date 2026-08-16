#include "acpi.hpp"

namespace arch::x86_64::acpi {
namespace {

constexpr size_t RSDP_V1_SIZE = 20U;
constexpr size_t RSDP_V2_MINIMUM_SIZE = 36U;
constexpr size_t MAXIMUM_RSDP_SIZE = 64U;
constexpr uint32_t MAXIMUM_TABLE_SIZE = 1024U * 1024U;
constexpr size_t SDT_HEADER_SIZE = 36U;
constexpr size_t MADT_HEADER_SIZE = 44U;

Topology g_topology{};
bool g_available = false;

uint16_t read_u16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(bytes[1]) << 8U;
}

uint32_t read_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        static_cast<uint32_t>(bytes[1]) << 8U |
        static_cast<uint32_t>(bytes[2]) << 16U |
        static_cast<uint32_t>(bytes[3]) << 24U;
}

uint64_t read_u64(const uint8_t* bytes) {
    return static_cast<uint64_t>(read_u32(bytes)) |
        static_cast<uint64_t>(read_u32(bytes + 4U)) << 32U;
}

bool signature_equal(const uint8_t* bytes, const char* signature, size_t size) {
    if (bytes == nullptr || signature == nullptr) return false;
    for (size_t index = 0U; index < size; ++index) {
        if (bytes[index] != static_cast<uint8_t>(signature[index])) {
            return false;
        }
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

bool valid_sdt(const uint8_t* table) {
    if (table == nullptr) return false;
    const uint32_t length = read_u32(table + 4U);
    return length >= SDT_HEADER_SIZE && length <= MAXIMUM_TABLE_SIZE &&
        checksum_valid(table, length);
}

Status parse_madt(const uint8_t* table, Topology* output) {
    if (!valid_sdt(table) || !signature_equal(table, "APIC", 4U)) {
        return Status::InvalidMadt;
    }
    const uint32_t length = read_u32(table + 4U);
    if (length < MADT_HEADER_SIZE) return Status::InvalidMadt;

    Topology result{};
    result.local_apic_address = read_u32(table + 36U);
    result.madt_flags = read_u32(table + 40U);
    result.legacy_pic_present = (result.madt_flags & 1U) != 0U;
    size_t offset = MADT_HEADER_SIZE;
    while (offset < length) {
        if (length - offset < 2U) return Status::InvalidMadt;
        const uint8_t type = table[offset];
        const uint8_t entry_length = table[offset + 1U];
        if (entry_length < 2U || entry_length > length - offset) {
            return Status::InvalidMadt;
        }
        const uint8_t* entry = table + offset;
        if (type == 0U) {
            if (entry_length < 8U) return Status::InvalidMadt;
            if (result.processor_count >= MAXIMUM_PROCESSORS) {
                return Status::TooManyEntries;
            }
            const uint32_t flags = read_u32(entry + 4U);
            if ((flags & 3U) != 0U) {
                result.processors[result.processor_count++] = {
                    entry[2U], entry[3U], flags
                };
            }
        } else if (type == 1U) {
            if (entry_length < 12U) return Status::InvalidMadt;
            if (result.io_apic_count >= MAXIMUM_IO_APICS) {
                return Status::TooManyEntries;
            }
            result.io_apics[result.io_apic_count++] = {
                entry[2U], read_u32(entry + 4U), read_u32(entry + 8U)
            };
        } else if (type == 2U) {
            if (entry_length < 10U) return Status::InvalidMadt;
            if (result.override_count >= MAXIMUM_OVERRIDES) {
                return Status::TooManyEntries;
            }
            result.overrides[result.override_count++] = {
                entry[2U], entry[3U], read_u32(entry + 4U),
                read_u16(entry + 8U)
            };
        } else if (type == 5U) {
            if (entry_length < 12U) return Status::InvalidMadt;
            result.local_apic_address = read_u64(entry + 4U);
        }
        offset += entry_length;
    }
    if (result.local_apic_address == 0U || result.processor_count == 0U ||
        result.io_apic_count == 0U) {
        return Status::InvalidMadt;
    }
    *output = result;
    return Status::Ok;
}

} // namespace

Status parse_rsdp(const void* rsdp, Topology* output) {
    if (rsdp == nullptr || output == nullptr) return Status::InvalidArgument;
    *output = {};
    const auto* bytes = static_cast<const uint8_t*>(rsdp);
    if (!signature_equal(bytes, "RSD PTR ", 8U) ||
        !checksum_valid(bytes, RSDP_V1_SIZE)) {
        return Status::InvalidRsdp;
    }

    const uint8_t revision = bytes[15U];
    const uint8_t* root = nullptr;
    size_t entry_size = 0U;
    const char* root_signature = nullptr;
    if (revision >= 2U) {
        const uint32_t rsdp_length = read_u32(bytes + 20U);
        if (rsdp_length < RSDP_V2_MINIMUM_SIZE ||
            rsdp_length > MAXIMUM_RSDP_SIZE ||
            !checksum_valid(bytes, rsdp_length)) {
            return Status::InvalidRsdp;
        }
        const uint64_t xsdt_address = read_u64(bytes + 24U);
        if (xsdt_address != 0U) {
            root = reinterpret_cast<const uint8_t*>(
                static_cast<uintptr_t>(xsdt_address));
            entry_size = 8U;
            root_signature = "XSDT";
        }
    }
    if (root == nullptr) {
        const uint32_t rsdt_address = read_u32(bytes + 16U);
        if (rsdt_address == 0U) return Status::InvalidRootTable;
        root = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(rsdt_address));
        entry_size = 4U;
        root_signature = "RSDT";
    }
    if (!valid_sdt(root) ||
        !signature_equal(root, root_signature, 4U)) {
        return Status::InvalidRootTable;
    }
    const uint32_t root_length = read_u32(root + 4U);
    const size_t payload = root_length - SDT_HEADER_SIZE;
    if ((payload % entry_size) != 0U || payload / entry_size > 256U) {
        return Status::InvalidRootTable;
    }
    for (size_t offset = SDT_HEADER_SIZE; offset < root_length;
         offset += entry_size) {
        const uint64_t address = entry_size == 8U
            ? read_u64(root + offset)
            : read_u32(root + offset);
        if (address == 0U) continue;
        const auto* table = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(address));
        if (valid_sdt(table) && signature_equal(table, "APIC", 4U)) {
            return parse_madt(table, output);
        }
    }
    return Status::MadtNotFound;
}

Status discover(uint64_t rsdp_physical_address) {
    g_available = false;
    g_topology = {};
    if (rsdp_physical_address == 0U) return Status::InvalidArgument;
    const Status status = parse_rsdp(
        reinterpret_cast<const void*>(
            static_cast<uintptr_t>(rsdp_physical_address)),
        &g_topology);
    if (status == Status::Ok) g_available = true;
    return status;
}

bool available() { return g_available; }
const Topology* topology() { return g_available ? &g_topology : nullptr; }

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidArgument: return "missing ACPI RSDP";
        case Status::InvalidRsdp: return "invalid ACPI RSDP";
        case Status::InvalidRootTable: return "invalid ACPI root table";
        case Status::MadtNotFound: return "ACPI MADT not found";
        case Status::InvalidMadt: return "invalid ACPI MADT";
        case Status::TooManyEntries: return "ACPI topology exceeds bounds";
    }
    return "unknown ACPI status";
}

} // namespace arch::x86_64::acpi
