#include <cassert>
#include <cstring>

#include "../kernel/drivers/pci_msix_group.hpp"
#include "../kernel/arch/x86_64/apic.hpp"

namespace fixture {
using namespace pci::msix;
namespace vectors = arch::x86_64::hardware_vectors;
constexpr uint8_t capability = 0x50U;
constexpr uint32_t table_offset = 0x40U;
constexpr uint16_t table_size = 8U;
constexpr uint16_t command = 7U;
alignas(8) uint8_t bytes[512];
uint16_t current_command;
uint16_t control;
unsigned writes;
bool apic_ready;
bool capability_present;
bool malformed;
uint32_t destination;
uint8_t fail_release;
bool check_release_barrier;
arch::x86_64::interrupts::InterruptHandler handlers[256];

struct Write { bool config; uint32_t offset; uint32_t value; } trace[256];

uint32_t load(uint32_t offset) {
    assert(offset <= sizeof(bytes) - 4U);
    uint32_t value;
    std::memcpy(&value, bytes + offset, sizeof(value));
    return value;
}
void store(uint32_t offset, uint32_t value) {
    assert(offset <= sizeof(bytes) - 4U);
    std::memcpy(bytes + offset, &value, sizeof(value));
}
void record(bool config, uint32_t offset, uint32_t value) {
    assert(writes < 256U);
    trace[writes++] = {config, offset, value};
}
uint16_t read_config(pci::Address, uint8_t offset, void*) {
    assert(offset == 4U || offset == capability + 2U);
    return offset == 4U ? current_command : control;
}
void write_config(pci::Address, uint8_t offset, uint16_t value, void*) {
    record(true, offset, value);
    assert(offset == 4U || offset == capability + 2U);
    (offset == 4U ? current_command : control) = value;
}
uint32_t read_table(uint32_t offset, void*) { return load(offset); }
void write_table(uint32_t offset, uint32_t value, void*) {
    record(false, offset, value);
    store(offset, value);
}
ConfigAccess config() { return {read_config, write_config, nullptr}; }
TableAccess table() { return {read_table, write_table, nullptr}; }
pci::Device device() { return {}; }
MmioRegion region() { return {0U, 0x100000U, bytes, sizeof(bytes)}; }
void handler_a(arch::x86_64::interrupts::InterruptFrame&) {}
void handler_b(arch::x86_64::interrupts::InterruptFrame&) {}
constexpr RouteRequest requests[] = {{1U, handler_a}, {5U, handler_b}};
constexpr Message messages[] = {{1U, 0x40U, 3U}, {5U, 0x42U, 3U}};

void reset() {
    assert(vectors::allocated_count() == 0U);
    vectors::initialize();
    std::memset(bytes, 0xA5, sizeof(bytes));
    std::memset(handlers, 0, sizeof(handlers));
    for (uint16_t i = 0U; i < table_size; ++i) store(table_offset + i * 16U + 12U, 0xA5A50001U);
    current_command = command;
    control = table_size - 1U;
    writes = 0U;
    apic_ready = capability_present = true;
    malformed = check_release_barrier = false;
    destination = 3U;
    fail_release = 0U;
}

Status enable(RouteGroup* output) {
    return enable_group(device(), requests, 2U, region(), region(), output);
}
Status program(GroupState* output, const Message* requested = messages, size_t count = 2U) {
    return program_group({}, capability, table_size, table_offset, requested, count,
        config(), table(), output);
}
} // namespace fixture

namespace pci {
uint32_t read32(Address, uint8_t) { assert(false); return 0U; }
void write32(Address, uint8_t, uint32_t) { assert(false); }
uint16_t read16(Address address, uint8_t offset) { return fixture::read_config(address, offset, nullptr); }
void write16(Address address, uint8_t offset, uint16_t value) {
    fixture::write_config(address, offset, value, nullptr);
}
bool find_capability(const Device&, CapabilityId id, Capability*) {
    assert(id == CapabilityId::MsiX);
    return fixture::capability_present;
}
bool read_msix_info(const Device&, MsiXInfo* output) {
    *output = {fixture::capability, false, false, fixture::table_size, 0U,
        fixture::table_offset, 0U, 0x100U};
    return !fixture::malformed;
}
uint64_t bar_address(const Device&, uint8_t index, bool* io) {
    assert(index == 0U); *io = false; return 0x100000U;
}
} // namespace pci
namespace arch::x86_64::apic {
bool local_enabled() { return fixture::apic_ready; }
uint32_t local_apic_id() { return fixture::destination; }
}
namespace arch::x86_64::interrupts {
hardware_vectors::Status allocate_hardware_vector(InterruptHandler handler, hardware_vectors::Lease* output) {
    const auto status = hardware_vectors::allocate(output);
    if (status == hardware_vectors::Status::Ok) fixture::handlers[output->vector] = handler;
    return status;
}
bool owns_hardware_vector(const hardware_vectors::Lease& lease) { return hardware_vectors::owns(lease); }
hardware_vectors::Status release_hardware_vector(const hardware_vectors::Lease& lease) {
    if (fixture::check_release_barrier) {
        assert((fixture::control & pci::msix::CONTROL_ENABLE) == 0U);
        assert((fixture::current_command & pci::msix::PCI_COMMAND_INTX_DISABLE) != 0U);
        for (const auto& request : fixture::requests) {
            assert(fixture::load(fixture::table_offset + request.entry_index * 16U) == 0xA5A5A5A5U);
        }
    }
    if (lease.vector == fixture::fail_release) return hardware_vectors::Status::StaleLease;
    const auto status = hardware_vectors::release(lease);
    if (status == hardware_vectors::Status::Ok) fixture::handlers[lease.vector] = nullptr;
    return status;
}
} // namespace arch::x86_64::interrupts

namespace {
using namespace fixture;

void test_transaction_order_and_restore() {
    reset();
    uint8_t original[sizeof(bytes)];
    std::memcpy(original, bytes, sizeof(bytes));
    GroupState state{};
    assert(program(&state) == Status::Ok);
    assert(state.active && state.command_held && state.count == 2U);
    assert(trace[0].config && trace[0].offset == capability + 2U &&
        trace[0].value == (table_size - 1U + CONTROL_FUNCTION_MASK));
    // Every entry is masked before any address/data programming.
    assert(!trace[1].config && trace[1].offset == table_offset + 16U + 12U);
    assert(!trace[2].config && trace[2].offset == table_offset + 80U + 12U);
    assert(trace[1].value & VECTOR_MASK);
    assert(trace[2].value & VECTOR_MASK);
    for (size_t i = 0U; i < 2U; ++i) {
        const uint32_t offset = table_offset + messages[i].entry_index * 16U;
        assert(load(offset) == 0xFEE03000U);
        assert(load(offset + 4U) == 0U);
        assert(load(offset + 8U) == messages[i].vector);
        assert(load(offset + 12U) == 0xA5A50000U);
    }
    assert(current_command == (command | PCI_COMMAND_INTX_DISABLE));
    assert(control == ((table_size - 1U) | CONTROL_ENABLE));
    assert(trace[writes - 1U].config && trace[writes - 1U].value == control);
    const unsigned programmed_writes = writes;
    assert(program(&state) == Status::AlreadyEnabled);
    assert(writes == programmed_writes && state.active);
    assert(finish_group_restore(config(), &state) == Status::NotActive);
    assert(quiesce_group(config(), table(), &state) == Status::Ok);
    assert(!state.active && state.command_held);
    assert(std::memcmp(original, bytes, sizeof(bytes)) == 0);
    assert(control == table_size - 1U);
    assert(current_command == (command | PCI_COMMAND_INTX_DISABLE));
    assert(program(&state) == Status::AlreadyEnabled);
    assert(quiesce_group(config(), table(), &state) == Status::NotActive);
    assert(finish_group_restore(config(), &state) == Status::Ok);
    assert(current_command == command && !state.command_held);
    assert(finish_group_restore(config(), &state) == Status::NotActive);
}

void test_program_rejections() {
    reset();
    GroupState state{};
    Message bad[] = {messages[0], messages[1]};
    assert(program(&state, nullptr) == Status::InvalidArgument);
    assert(program(&state, messages, 0U) == Status::InvalidArgument);
    assert(program(&state, messages, MAX_GROUP_ROUTES + 1U) == Status::InvalidArgument);
    bad[1].entry_index = bad[0].entry_index;
    assert(program(&state, bad) == Status::InvalidArgument);
    bad[1] = messages[1]; bad[1].vector = bad[0].vector;
    assert(program(&state, bad) == Status::InvalidArgument);
    bad[1] = messages[1]; bad[1].vector = 0x80U;
    assert(program(&state, bad) == Status::InvalidArgument);
    bad[1] = messages[1]; bad[1].destination_apic_id = 256U;
    assert(program(&state, bad) == Status::InvalidArgument);
    bad[1] = messages[1]; bad[1].entry_index = table_size;
    assert(program(&state, bad) == Status::EntryOutOfRange);
    assert(program_group({}, capability, table_size, 0xFFFFFFF8U, messages, 2U,
        config(), table(), &state) == Status::CapabilityMalformed);
    assert(program_group({}, 0xF8U, table_size, table_offset, messages, 2U,
        config(), table(), &state) == Status::CapabilityMalformed);
    control |= CONTROL_ENABLE;
    assert(program(&state) == Status::AlreadyEnabled);
    control = 2U;
    assert(program(&state) == Status::CapabilityMalformed);
    control = table_size - 1U;
    store(table_offset + 12U, 0U);
    assert(program(&state) == Status::UnmaskedEntry);
    assert(writes == 0U && !state.active && !state.command_held);
}

void test_group_ownership() {
    reset();
    RouteGroup group{};
    assert(enable(&group) == Status::Ok);
    assert(vectors::allocated_count() == 2U && group.programmed.active);
    assert(group.vectors[0].vector != group.vectors[1].vector);
    assert(handlers[group.vectors[0].vector] == handler_a);
    assert(handlers[group.vectors[1].vector] == handler_b);
    const RouteGroup old = group;
    const unsigned enabled_writes = writes;
    assert(enable(&group) == Status::AlreadyEnabled);
    assert(writes == enabled_writes && vectors::allocated_count() == 2U);
    check_release_barrier = true;
    assert(disable_group(&group) == Status::Ok);
    assert(group.count == 0U && vectors::allocated_count() == 0U);
    assert(current_command == command && control == table_size - 1U);
    assert(disable_group(&group) == Status::NotActive);
    check_release_barrier = false;
    assert(enable(&group) == Status::Ok);
    assert(group.vectors[0].vector == old.vectors[0].vector);
    assert(group.vectors[0].generation != old.vectors[0].generation);
    RouteGroup stale = old;
    const unsigned before = writes;
    assert(disable_group(&stale) == Status::StaleRoute);
    assert(writes == before && vectors::allocated_count() == 2U);
    // Even a stale last lease must be found before masking the first entry.
    RouteGroup mixed = group;
    mixed.vectors[1] = old.vectors[1];
    assert(disable_group(&mixed) == Status::StaleRoute);
    mixed = group; mixed.vectors[1] = group.vectors[0];
    assert(disable_group(&mixed) == Status::StaleRoute);
    mixed = group; mixed.table_bytes = table_offset;
    assert(disable_group(&mixed) == Status::TableOutOfRange);
    assert(writes == before);
    assert(disable_group(&group) == Status::Ok);
}

void test_exhaustion_and_rollback() {
    reset();
    vectors::Lease held[vectors::VECTOR_CAPACITY]{};
    for (size_t i = 0U; i < vectors::VECTOR_CAPACITY - 1U; ++i) {
        assert(vectors::allocate(&held[i]) == vectors::Status::Ok);
    }
    RouteGroup group{};
    assert(enable(&group) == Status::VectorUnavailable);
    assert(group.count == 0U && writes == 0U);
    assert(vectors::allocated_count() == vectors::VECTOR_CAPACITY - 1U);
    for (size_t i = 0U; i < vectors::VECTOR_CAPACITY - 1U; ++i) {
        assert(vectors::release(held[i]) == vectors::Status::Ok);
    }
    // A later programming rejection also releases every allocated handler.
    control |= CONTROL_ENABLE;
    assert(enable(&group) == Status::AlreadyEnabled);
    assert(vectors::allocated_count() == 0U && writes == 0U);
    control = table_size - 1U;
    destination = 256U;
    assert(enable(&group) == Status::InvalidArgument);
    assert(vectors::allocated_count() == 0U && writes == 0U);
    destination = 3U;
    store(table_offset + 12U, 0U);
    fail_release = vectors::FIRST_VECTOR;
    assert(enable(&group) == Status::StaleRoute);
    assert(group.count == 2U && !group.programmed.active);
    assert(vectors::allocated_count() == 1U && writes == 0U);
    assert(enable(&group) == Status::AlreadyEnabled);
    fail_release = 0U;
    assert(disable_group(&group) == Status::Ok);
    assert(vectors::allocated_count() == 0U && writes == 0U);
}

void test_cleanup_retry_and_bounds() {
    reset();
    RouteGroup group{};
    assert(enable(&group) == Status::Ok);
    fail_release = group.vectors[1].vector;
    check_release_barrier = true;
    assert(disable_group(&group) == Status::StaleRoute);
    assert(!group.programmed.active && group.programmed.command_held);
    assert(group.vectors[0].generation == 0U && group.vectors[1].generation != 0U);
    assert(vectors::allocated_count() == 1U);
    assert(current_command == (command | PCI_COMMAND_INTX_DISABLE));
    fail_release = 0U;
    assert(disable_group(&group) == Status::Ok);
    assert(vectors::allocated_count() == 0U && current_command == command);
    check_release_barrier = false;
    RouteRequest all[MAX_GROUP_ROUTES]{};
    for (uint16_t i = 0U; i < MAX_GROUP_ROUTES; ++i) all[i] = {i, handler_a};
    assert(enable_group(device(), all, MAX_GROUP_ROUTES, region(), region(), &group) == Status::Ok);
    assert(vectors::allocated_count() == MAX_GROUP_ROUTES);
    assert(disable_group(&group) == Status::Ok);
    assert(enable_group(device(), all, 1U, region(), region(), &group) == Status::Ok);
    assert(vectors::allocated_count() == 1U);
    assert(disable_group(&group) == Status::Ok);
    apic_ready = false;
    assert(enable(&group) == Status::LocalApicUnavailable);
    apic_ready = true; capability_present = false;
    assert(enable(&group) == Status::CapabilityMissing);
    capability_present = true; malformed = true;
    assert(enable(&group) == Status::CapabilityMalformed);
    assert(vectors::allocated_count() == 0U);
}
} // namespace

int main() {
    test_transaction_order_and_restore();
    test_program_rejections();
    test_group_ownership();
    test_exhaustion_and_rollback();
    test_cleanup_retry_and_bounds();
    return 0;
}
