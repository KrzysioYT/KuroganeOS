from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: anchor count={count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    'kernel/drivers/pci.hpp',
    '''using VisitCallback = bool (*)(const Device& device, void* context);\n''',
    '''constexpr size_t MAX_CAPABILITIES_PER_DEVICE = 48U;\n\nenum class CapabilityId : uint8_t {\n    PowerManagement = 0x01U,\n    Msi = 0x05U,\n    PciExpress = 0x10U,\n    MsiX = 0x11U,\n};\n\nenum class CapabilityWalkStatus : uint8_t {\n    Ok = 0,\n    NotPresent,\n    UnsupportedHeader,\n    InvalidArgument,\n    MalformedList,\n    IterationStopped,\n};\n\nstruct Capability {\n    uint8_t id;\n    uint8_t offset;\n    uint8_t next;\n};\n\nstruct MsiInfo {\n    uint8_t offset;\n    bool enabled;\n    bool address_64_bit;\n    bool per_vector_masking;\n    uint8_t multiple_message_capable;\n    uint8_t multiple_message_enabled;\n};\n\nstruct MsiXInfo {\n    uint8_t offset;\n    bool enabled;\n    bool function_mask;\n    uint16_t table_size;\n    uint8_t table_bar;\n    uint32_t table_offset;\n    uint8_t pending_bit_array_bar;\n    uint32_t pending_bit_array_offset;\n};\n\nusing VisitCallback = bool (*)(const Device& device, void* context);\nusing CapabilityCallback = bool (*)(\n    const Device& device,\n    const Capability& capability,\n    void* context);\n''',
)
replace_once(
    'kernel/drivers/pci.hpp',
    '''void visit(VisitCallback callback, void* context);\nuint64_t bar_address''',
    '''void visit(VisitCallback callback, void* context);\nCapabilityWalkStatus visit_capabilities(\n    const Device& device,\n    CapabilityCallback callback,\n    void* context);\nbool find_capability(\n    const Device& device,\n    CapabilityId id,\n    Capability* output);\nbool read_msi_info(const Device& device, MsiInfo* output);\nbool read_msix_info(const Device& device, MsiXInfo* output);\nconst char* capability_walk_status_name(CapabilityWalkStatus status);\nuint64_t bar_address''',
)

source = Path('kernel/drivers/pci.cpp')
text = source.read_text()
anchor = '''bool function_exists(Address address) {\n    return read16(address, 0x00) != 0xFFFFu;\n}\n'''
if text.count(anchor) != 1:
    raise SystemExit('PCI function_exists anchor mismatch')
helpers = r'''

bool capability_header_supported(const Device& device) {
    const uint8_t type = static_cast<uint8_t>(device.header_type & 0x7FU);
    return type == 0x00U || type == 0x01U;
}

bool capability_list_present(const Device& device) {
    constexpr uint16_t kStatusCapabilitiesList = UINT16_C(1) << 4U;
    return (read16(device.address, 0x06U) & kStatusCapabilitiesList) != 0U;
}

bool valid_capability_offset(uint8_t offset) {
    return offset >= 0x40U && offset <= 0xFCU && (offset & 0x03U) == 0U;
}
'''
text = text.replace(anchor, anchor + helpers, 1)

insert_anchor = '''uint64_t bar_address(const Device& device, uint8_t bar_index, bool* is_io) {\n'''
if text.count(insert_anchor) != 1:
    raise SystemExit('PCI capability API insertion anchor mismatch')
functions = r'''CapabilityWalkStatus visit_capabilities(
    const Device& device,
    CapabilityCallback callback,
    void* context) {
    if (callback == nullptr) return CapabilityWalkStatus::InvalidArgument;
    if (!capability_header_supported(device)) {
        return CapabilityWalkStatus::UnsupportedHeader;
    }
    if (!capability_list_present(device)) {
        return CapabilityWalkStatus::NotPresent;
    }

    bool visited[64]{};
    uint8_t offset = static_cast<uint8_t>(read8(device.address, 0x34U) & 0xFCU);
    if (offset == 0U) return CapabilityWalkStatus::NotPresent;
    for (size_t count = 0U; count < MAX_CAPABILITIES_PER_DEVICE; ++count) {
        if (!valid_capability_offset(offset)) {
            return CapabilityWalkStatus::MalformedList;
        }
        const size_t visited_index = static_cast<size_t>(offset >> 2U);
        if (visited[visited_index]) return CapabilityWalkStatus::MalformedList;
        visited[visited_index] = true;

        Capability capability{};
        capability.id = read8(device.address, offset);
        capability.offset = offset;
        capability.next = static_cast<uint8_t>(
            read8(device.address, static_cast<uint8_t>(offset + 1U)) & 0xFCU);
        if (!callback(device, capability, context)) {
            return CapabilityWalkStatus::IterationStopped;
        }
        if (capability.next == 0U) return CapabilityWalkStatus::Ok;
        offset = capability.next;
    }
    return CapabilityWalkStatus::MalformedList;
}

bool find_capability(
    const Device& device,
    CapabilityId id,
    Capability* output) {
    if (output == nullptr || !capability_header_supported(device) ||
        !capability_list_present(device)) {
        return false;
    }
    *output = {};
    bool visited[64]{};
    uint8_t offset = static_cast<uint8_t>(read8(device.address, 0x34U) & 0xFCU);
    for (size_t count = 0U;
         count < MAX_CAPABILITIES_PER_DEVICE && offset != 0U;
         ++count) {
        if (!valid_capability_offset(offset)) return false;
        const size_t visited_index = static_cast<size_t>(offset >> 2U);
        if (visited[visited_index]) return false;
        visited[visited_index] = true;
        Capability capability{};
        capability.id = read8(device.address, offset);
        capability.offset = offset;
        capability.next = static_cast<uint8_t>(
            read8(device.address, static_cast<uint8_t>(offset + 1U)) & 0xFCU);
        if (capability.id == static_cast<uint8_t>(id)) {
            *output = capability;
            return true;
        }
        offset = capability.next;
    }
    return false;
}

bool read_msi_info(const Device& device, MsiInfo* output) {
    if (output == nullptr) return false;
    *output = {};
    Capability capability{};
    if (!find_capability(device, CapabilityId::Msi, &capability)) return false;
    const uint16_t control = read16(
        device.address, static_cast<uint8_t>(capability.offset + 2U));
    output->offset = capability.offset;
    output->enabled = (control & UINT16_C(1)) != 0U;
    output->multiple_message_capable = static_cast<uint8_t>((control >> 1U) & 0x07U);
    output->multiple_message_enabled = static_cast<uint8_t>((control >> 4U) & 0x07U);
    output->address_64_bit = (control & (UINT16_C(1) << 7U)) != 0U;
    output->per_vector_masking = (control & (UINT16_C(1) << 8U)) != 0U;
    return true;
}

bool read_msix_info(const Device& device, MsiXInfo* output) {
    if (output == nullptr) return false;
    *output = {};
    Capability capability{};
    if (!find_capability(device, CapabilityId::MsiX, &capability)) return false;
    const uint16_t control = read16(
        device.address, static_cast<uint8_t>(capability.offset + 2U));
    const uint32_t table = read32(
        device.address, static_cast<uint8_t>(capability.offset + 4U));
    const uint32_t pba = read32(
        device.address, static_cast<uint8_t>(capability.offset + 8U));
    output->offset = capability.offset;
    output->table_size = static_cast<uint16_t>((control & UINT16_C(0x07ff)) + 1U);
    output->function_mask = (control & (UINT16_C(1) << 14U)) != 0U;
    output->enabled = (control & (UINT16_C(1) << 15U)) != 0U;
    output->table_bar = static_cast<uint8_t>(table & 0x07U);
    output->table_offset = table & ~UINT32_C(0x07);
    output->pending_bit_array_bar = static_cast<uint8_t>(pba & 0x07U);
    output->pending_bit_array_offset = pba & ~UINT32_C(0x07);
    return true;
}

const char* capability_walk_status_name(CapabilityWalkStatus status) {
    switch (status) {
        case CapabilityWalkStatus::Ok: return "OK";
        case CapabilityWalkStatus::NotPresent: return "NOT_PRESENT";
        case CapabilityWalkStatus::UnsupportedHeader: return "UNSUPPORTED_HEADER";
        case CapabilityWalkStatus::InvalidArgument: return "INVALID_ARGUMENT";
        case CapabilityWalkStatus::MalformedList: return "MALFORMED_LIST";
        case CapabilityWalkStatus::IterationStopped: return "ITERATION_STOPPED";
    }
    return "UNKNOWN";
}

'''
text = text.replace(insert_anchor, functions + insert_anchor, 1)
source.write_text(text)

# Defer tests in one canonical backlog.
todo = Path('TODO-DEFERRED-TESTS.md')
text = todo.read_text()
anchor = '## Filesystem foundation\n'
addition = (
    '## 5.0 Steel / Hardware\n'
    '- PCI capability regressions: status-bit gating, type-0/type-1 headers, bounded linked-list traversal, invalid offset/alignment, cycle detection, PM/MSI/MSI-X/PCIe lookup, MSI control decode, MSI-X table/PBA BIR+offset decode, absent capability behavior.\n\n'
    + anchor
)
if text.count(anchor) != 1:
    raise SystemExit('PCI 5.0 TODO insertion anchor mismatch')
todo.write_text(text.replace(anchor, addition, 1))

print('PCI capability/MSI/MSI-X discovery foundation applied')
