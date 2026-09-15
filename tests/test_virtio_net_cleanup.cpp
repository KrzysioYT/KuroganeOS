#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>

// Exercise the production driver, including private failure paths, against
// bounded host PCI/MMIO/DMA adapters. This is not hardware qualification.
#include "../kernel/net/virtio_net.cpp"

namespace fixture {
using namespace net::virtio_net;
alignas(4096) uint8_t common[4096];
alignas(4096) uint8_t notify[8192];
memory::virtual_memory::AddressSpace address_space{};
std::map<uint64_t, uint64_t> mappings;
size_t allocated = 0U;
size_t released = 0U;
unsigned found_queries = 0U;
uint16_t command = 0U;
bool reject_restore = false;
bool reject_dma_free = false;
bool reject_unmap = false;
bool reject_second_map = false;
bool expect_reset_before_free = false;
bool apic_ready = false;
bool capability_present = true;
bool malformed_capability = false;
bool reject_route = false;
bool reject_route_cleanup = false;
bool separate_pending_bar = false;
uint16_t table_entries = 1U;
bool reject_two_routes = false;
bool unmasked_entry = false;
bool retain_failed_enable = false;
bool partial_route_release = false;
unsigned enable_attempts = 0U;
uint64_t bar_bytes = 4096U;
uint16_t msix_control = 0U;
unsigned routes = 0U;
unsigned bar_probes = 0U;
arch::x86_64::interrupts::InterruptHandler irq_handler = nullptr;
arch::x86_64::interrupts::InterruptHandler irq_handlers[2]{};

void start() {
    assert(allocated == 0U && mappings.empty() && routes == 0U);
    std::memset(common, 0, sizeof(common));
    std::memset(notify, 0, sizeof(notify));
    g_cleanup_blocked = false;
    g_command_owned = true;
    g_original_command = 0x0407U;
    command = 6U;
    g_initialized = false;
    g_status = Status::NotInitialized;
    g_common = {}; g_notify = {}; g_device_config = {};
    g_receive_queue = {}; g_transmit_queue = {};
    released = 0U;
    reject_restore = reject_dma_free = reject_unmap = reject_second_map = false;
    expect_reset_before_free = false;
    g_msix_table = {}; g_msix_pending = {}; g_msix_route = {};
    g_interrupt_count = 0U; g_interrupt_pending = false;
    g_receive_interrupt_count = g_transmit_interrupt_count = 0U;
    g_interrupt_status = InterruptStatus::NotInitialized;
    apic_ready = false; capability_present = true;
    malformed_capability = reject_route = reject_route_cleanup = false;
    separate_pending_bar = false; bar_bytes = 4096U;
    msix_control = 0U; bar_probes = 0U; irq_handler = nullptr;
    irq_handlers[0] = irq_handlers[1] = nullptr;
    table_entries = 1U; enable_attempts = 0U;
    reject_two_routes = unmasked_entry = retain_failed_enable = partial_route_release = false;
}
void map_common() {
    VirtioCapability cap{true, 0U, 0U, 56U, 0U};
    assert(map_capability(g_device, cap, reinterpret_cast<uintptr_t>(common), &g_common));
}
void add_live_queue() {
    map_common();
    assert(allocate_queue_storage(&g_receive_queue, 8U));
    common[20U] = kStatusDriverOk;
    g_receive_queue.configured = true;
    expect_reset_before_free = true;
}
}

namespace pci {
uint32_t read32(Address, uint8_t) { return 0U; }
uint16_t read16(Address, uint8_t offset) {
    if (offset == 0x42U) return fixture::msix_control;
    assert(offset == 4U); return fixture::command;
}
void write16(Address, uint8_t offset, uint16_t value) {
    if (offset == 0x42U) { fixture::msix_control = value; return; }
    assert(offset == 4U);
    if (fixture::reject_restore && value == net::virtio_net::g_original_command) return;
    fixture::command = value;
}
uint64_t bar_address(const Device&, uint8_t, bool* io) { *io = false; return 0x100000U; }
const Device* find(uint16_t, uint16_t, size_t) { ++fixture::found_queries; return nullptr; }
bool find_capability(const Device&, CapabilityId id, Capability*) {
    assert(id == CapabilityId::MsiX); return fixture::capability_present;
}
bool read_msix_info(const Device&, MsiXInfo* output) {
    *output = {0x40U, false, false, fixture::table_entries, 0U, 0U,
        static_cast<uint8_t>(fixture::separate_pending_bar ? 2U : 0U), 0x800U};
    return !fixture::malformed_capability;
}
}
namespace pci::bar {
Status probe_disabled(const Device&, uint8_t index, Info* output) {
    assert((fixture::command & COMMAND_DECODE_MASK) == 0U);
    ++fixture::bar_probes;
    *output = {Kind::Memory32, index, 1U, false, 0x100000U + index * 0x10000U,
        fixture::bar_bytes};
    return Status::Ok;
}
}
namespace arch::x86_64::apic {
bool local_enabled() { return fixture::apic_ready; }
}
namespace pci::msix {
Status enable_group(const Device&, const RouteRequest* requests, size_t count,
        const MmioRegion& table, const MmioRegion& pending, RouteGroup* output) {
    assert(count >= 1U && count <= 2U && output->count == 0U);
    assert(requests[0].entry_index == 0U && requests[0].handler && table.bytes == fixture::bar_bytes);
    ++fixture::enable_attempts;
    assert(fixture::mappings.count(reinterpret_cast<uintptr_t>(table.virtual_address)));
    assert(fixture::mappings.count(reinterpret_cast<uintptr_t>(pending.virtual_address)));
    if (!fixture::separate_pending_bar) assert(table.virtual_address == pending.virtual_address);
    if (fixture::reject_route) return Status::VectorUnavailable;
    if (fixture::unmasked_entry) return Status::UnmaskedEntry;
    if (fixture::reject_two_routes && count == 2U) return Status::VectorUnavailable;
    *output = {};
    output->count = count;
    for (size_t i = 0U; i < count; ++i) {
        assert(requests[i].entry_index == i);
        output->vectors[i] = {static_cast<uint8_t>(0x40U + i), 1U};
        fixture::irq_handlers[i] = requests[i].handler;
    }
    fixture::routes = static_cast<unsigned>(count);
    if (fixture::retain_failed_enable) {
        assert(count == 2U);
        output->vectors[0] = {};
        --fixture::routes;
        return Status::StaleRoute;
    }
    output->programmed.active = true;
    output->programmed.command_held = true;
    output->programmed.capability_offset = 0x40U;
    output->programmed.original_command = fixture::command;
    fixture::irq_handler = requests[0].handler;
    fixture::command |= PCI_COMMAND_INTX_DISABLE;
    return Status::Ok;
}
Status disable_group(RouteGroup* route) {
    assert(route->count != 0U && fixture::routes != 0U);
    assert(fixture::common[20U] == 0U); // Device reset precedes vector release.
    assert(net::virtio_net::g_msix_table.mapped_pages != 0U);
    if (fixture::reject_route_cleanup) return Status::StaleRoute;
    route->programmed.active = false;
    for (size_t i = 0U; i < route->count; ++i) {
        if (route->vectors[i].generation == 0U) continue;
        if (fixture::partial_route_release && i == 1U) return Status::StaleRoute;
        route->vectors[i] = {};
        --fixture::routes;
        fixture::irq_handlers[i] = nullptr;
    }
    if (route->programmed.command_held) fixture::command = route->programmed.original_command;
    *route = {};
    fixture::irq_handler = nullptr;
    return Status::Ok;
}
}
namespace memory::kernel_virtual_memory {
virtual_memory::AddressSpace* address_space() { return &fixture::address_space; }
}
namespace memory::virtual_memory {
Status query_page(const AddressSpace*, uint64_t target, Mapping*) {
    return fixture::mappings.count(target) ? Status::Ok : Status::NotMapped;
}
Status map_page(AddressSpace*, uint64_t target, uint64_t physical, MapFlags) {
    if (fixture::reject_second_map && !fixture::mappings.empty()) return Status::OutOfMemory;
    assert(!fixture::mappings.count(target));
    fixture::mappings[target] = physical;
    return Status::Ok;
}
Status unmap_page(AddressSpace*, uint64_t target, Mapping*) {
    if (fixture::reject_unmap) return Status::BackendFailure;
    return fixture::mappings.erase(target) ? Status::Ok : Status::NotMapped;
}
}
namespace storage::dma {
Status allocate_page(bool, Page* output) {
    void* memory = std::aligned_alloc(4096U, 4096U);
    assert(memory);
    *output = {memory, reinterpret_cast<uintptr_t>(memory), true};
    ++fixture::allocated;
    return Status::Ok;
}
Status release_page(Page* page) {
    assert(page && page->allocated && fixture::allocated);
    if (fixture::expect_reset_before_free) assert(fixture::common[20U] == 0U);
    if (fixture::reject_dma_free) return Status::ReleaseFailed;
    std::free(page->virtual_address);
    *page = {};
    --fixture::allocated;
    ++fixture::released;
    return Status::Ok;
}
}

int main() {
    using namespace fixture;
    using net::virtio_net::Status;

    start(); add_live_queue();
    assert(fail(Status::DeviceFault) == Status::DeviceFault);
    assert(released == 11U && allocated == 0U && mappings.empty());
    assert(command == g_original_command && !g_command_owned && !g_cleanup_blocked);
    assert(g_common.base == nullptr && !g_receive_queue.configured);
    assert(initialize() == Status::NoDevice); // Clean failure permits a fresh probe.

    start(); add_live_queue();
    const unsigned before_find = found_queries;
    assert(cleanup_after_reset(Status::DeviceFault, false) == Status::DeviceResetFailed);
    assert(allocated == 11U && released == 0U && !mappings.empty());
    assert((command & kCommandBusMaster) == 0U && g_command_owned);
    assert(initialize() == Status::DeviceResetFailed && found_queries == before_find);
    assert(g_common.base && g_receive_queue.descriptor_page.allocated);
    assert(reset_device());
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);

    start(); map_common();
    VirtioCapability notify_cap{true, 0U, 0U, 2U, 4U};
    assert(map_capability(g_device, notify_cap, reinterpret_cast<uintptr_t>(notify), &g_notify));
    mmio_write16(g_common, 24U, 8U);
    mmio_write16(g_common, 30U, 1U); // Notification lies outside the mapped span.
    assert(!configure_queue(0U, notify_cap, &g_receive_queue));
    assert(allocated == 11U && released == 0U); // Published addresses stay owned.
    expect_reset_before_free = true;
    assert(fail(Status::QueueConfigurationFailed) == Status::QueueConfigurationFailed);
    assert(allocated == 0U && mappings.empty());

    start(); add_live_queue(); reject_dma_free = true;
    assert(fail(Status::DeviceFault) == Status::DeviceCleanupFailed);
    assert(g_cleanup_blocked && allocated == 11U && g_receive_queue.descriptor_page.allocated);
    reject_dma_free = false;
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);

    start(); map_common(); reject_unmap = true;
    assert(fail(Status::MappingFailed) == Status::DeviceCleanupFailed);
    assert(g_cleanup_blocked && g_common.mapped_pages == 1U && mappings.size() == 1U);
    reject_unmap = false;
    assert(cleanup_after_reset(Status::MappingFailed, true) == Status::MappingFailed);

    start(); reject_second_map = reject_unmap = true;
    VirtioCapability spanning{true, 0U, 4095U, 2U, 0U};
    assert(!map_capability(g_device, spanning, reinterpret_cast<uintptr_t>(notify), &g_notify));
    assert(g_notify.mapped_pages == 1U && mappings.size() == 1U);
    assert(fail(Status::MappingFailed) == Status::DeviceCleanupFailed);
    reject_unmap = false;
    assert(cleanup_after_reset(Status::MappingFailed, true) == Status::MappingFailed);

    start(); map_common(); reject_restore = true;
    assert(fail(Status::DeviceFault) == Status::PciCommandFailed);
    assert(g_cleanup_blocked && g_command_owned && (command & kCommandBusMaster) == 0U);
    reject_restore = false;
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);
    assert(allocated == 0U && mappings.empty());

    start(); map_common();
    assert(prepare_queue_interrupts());
    assert(interrupt_diagnostics().status == InterruptStatus::LocalApicUnavailable);
    assert(bar_probes == 0U && routes == 0U);
    apic_ready = true; capability_present = false;
    assert(prepare_queue_interrupts());
    assert(g_interrupt_status == InterruptStatus::CapabilityUnavailable && bar_probes == 0U);
    capability_present = true; malformed_capability = true;
    assert(prepare_queue_interrupts());
    assert(g_interrupt_status == InterruptStatus::CapabilityMalformed && bar_probes == 0U);
    malformed_capability = false; bar_bytes = kMaximumMsixBarBytes + 1U;
    const auto original = command;
    assert(prepare_queue_interrupts());
    assert(g_interrupt_status == InterruptStatus::BarUnavailable && command == original);
    assert(fail(Status::DeviceFault) == Status::DeviceFault);

    start(); map_common(); apic_ready = true; reject_route = true;
    assert(prepare_queue_interrupts());
    assert(g_interrupt_status == InterruptStatus::RouteUnavailable);
    assert(mappings.size() == 1U && routes == 0U && !g_msix_table.mapped_pages);
    assert(fail(Status::DeviceFault) == Status::DeviceFault);

    start(); map_common(); apic_ready = true; separate_pending_bar = true;
    assert(prepare_queue_interrupts() && routes == 1U && bar_probes == 2U);
    assert(g_msix_table.mapped_pages == 1U && g_msix_pending.mapped_pages == 1U);
    assert(fail(Status::DeviceFault) == Status::DeviceFault && routes == 0U);
    assert(mappings.empty());

    start(); map_common(); apic_ready = true; reject_route = true; reject_unmap = true;
    assert(!prepare_queue_interrupts());
    assert(g_interrupt_status == InterruptStatus::CleanupFailed && routes == 0U);
    assert(g_msix_table.mapped_pages == 1U && mappings.size() == 2U);
    assert(fail(Status::QueueInterruptFailed) == Status::DeviceCleanupFailed);
    assert(g_cleanup_blocked && g_msix_table.mapped_pages == 1U);
    reject_unmap = false;
    assert(cleanup_after_reset(Status::QueueInterruptFailed, true) == Status::QueueInterruptFailed);
    assert(mappings.empty());

    start(); map_common(); apic_ready = true;
    assert(prepare_queue_interrupts() && routes == 1U && bar_probes == 1U);
    VirtioCapability notify_irq{true, 0U, 0U, 4U, 4U};
    assert(map_capability(g_device, notify_irq, reinterpret_cast<uintptr_t>(notify), &g_notify));
    mmio_write16(g_common, 24U, 8U);
    assert(configure_queue(0U, notify_irq, &g_receive_queue));
    assert(mmio_read16(g_common, 26U) == 0U && g_receive_queue.available[0] == 0U);
    assert(allocate_queue_storage(&g_transmit_queue, 8U));
    g_transmit_queue.buffer_free[0] = false;
    g_transmit_queue.available_index = 1U;
    g_transmit_queue.used_elements[0].id = 0U;
    g_transmit_queue.used_header[1] = 1U;
    g_initialized = true;
    arch::x86_64::interrupts::InterruptFrame frame{};
    irq_handler(frame);
    const auto diagnostics = interrupt_diagnostics();
    assert(diagnostics.status == InterruptStatus::MsiXEnabled && diagnostics.vector == 0x40U);
    assert(diagnostics.delivered == 1U && diagnostics.pending);
    uint8_t packet[64]{}; size_t packet_bytes = 0U;
    assert(receive_frame(nullptr, packet, sizeof(packet), &packet_bytes) == Status::WouldBlock);
    assert(g_transmit_queue.buffer_free[0] && !interrupt_diagnostics().pending);
    reject_route_cleanup = true;
    assert(fail(Status::DeviceFault) == Status::DeviceCleanupFailed);
    assert(routes == 1U && g_cleanup_blocked && allocated == 22U && mappings.size() == 3U);
    reject_route_cleanup = false;
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);
    assert(routes == 0U && allocated == 0U && mappings.empty());

    // Separate RX/TX vectors must not impersonate each other's source.
    start(); map_common(); apic_ready = true; table_entries = 3U;
    assert(prepare_queue_interrupts() && routes == 2U && enable_attempts == 1U);
    assert(irq_handlers[0] != irq_handlers[1]);
    assert(map_capability(g_device, notify_irq, reinterpret_cast<uintptr_t>(notify), &g_notify));
    mmio_write16(g_common, 24U, 8U);
    assert(configure_queue(0U, notify_irq, &g_receive_queue));
    assert(mmio_read16(g_common, 26U) == 0U);
    mmio_write16(g_common, 28U, 0U); // Host register bank for the second queue.
    assert(configure_queue(1U, notify_irq, &g_transmit_queue));
    assert(mmio_read16(g_common, 26U) == 1U);
    g_transmit_queue.buffer_free[0] = false;
    g_transmit_queue.available_index = 1U;
    g_transmit_queue.used_elements[0].id = 0U;
    g_transmit_queue.used_header[1] = 1U;
    g_initialized = true;
    irq_handlers[0](frame);
    auto split = interrupt_diagnostics();
    assert(split.route_count == 2U && split.receive_vector != split.transmit_vector);
    assert(split.receive_delivered == 1U && split.transmit_delivered == 0U);
    assert(receive_frame(nullptr, packet, sizeof(packet), &packet_bytes) == Status::WouldBlock);
    assert(!g_transmit_queue.buffer_free[0]);
    irq_handlers[1](frame);
    split = interrupt_diagnostics();
    assert(split.receive_delivered == 1U && split.transmit_delivered == 1U && split.delivered == 2U);
    assert(receive_frame(nullptr, packet, sizeof(packet), &packet_bytes) == Status::WouldBlock);
    assert(g_transmit_queue.buffer_free[0] && !interrupt_diagnostics().pending);
    partial_route_release = true;
    assert(fail(Status::DeviceFault) == Status::DeviceCleanupFailed);
    assert(routes == 1U && g_msix_route.count == 2U && allocated == 22U && g_msix_table.base);
    assert(g_msix_route.vectors[0].generation == 0U && g_msix_route.vectors[1].generation != 0U);
    assert(g_msix_route.programmed.command_held && g_cleanup_blocked);
    partial_route_release = false;
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);
    assert(routes == 0U && allocated == 0U && mappings.empty());

    // Vector exhaustion permits shared routing only after complete rollback.
    start(); map_common(); apic_ready = true; table_entries = 3U; reject_two_routes = true;
    assert(prepare_queue_interrupts() && routes == 1U && enable_attempts == 2U);
    irq_handler(frame);
    const auto shared = interrupt_diagnostics();
    assert(shared.route_count == 1U && shared.receive_vector == shared.transmit_vector);
    assert(shared.delivered == 1U && shared.receive_delivered == 0U && shared.transmit_delivered == 0U);
    assert(fail(Status::DeviceFault) == Status::DeviceFault);

    start(); map_common(); apic_ready = true; table_entries = 3U; unmasked_entry = true;
    assert(prepare_queue_interrupts() && routes == 0U && enable_attempts == 1U);
    assert(g_interrupt_status == InterruptStatus::RouteUnavailable);
    assert(fail(Status::DeviceFault) == Status::DeviceFault);

    start(); map_common(); apic_ready = true; table_entries = 3U; retain_failed_enable = true;
    assert(!prepare_queue_interrupts() && routes == 1U && enable_attempts == 1U);
    assert(g_msix_route.count == 2U && g_msix_table.mapped_pages == 1U);
    reject_route_cleanup = true;
    assert(fail(Status::QueueInterruptFailed) == Status::DeviceCleanupFailed);
    assert(g_cleanup_blocked && routes == 1U && g_msix_table.mapped_pages == 1U);
    reject_route_cleanup = false;
    assert(cleanup_after_reset(Status::QueueInterruptFailed, true) == Status::QueueInterruptFailed);
    assert(routes == 0U && allocated == 0U && mappings.empty());
    assert(interrupt_diagnostics().vector == 0U && !interrupt_diagnostics().pending);

    start(); map_common(); apic_ready = true;
    assert(prepare_queue_interrupts());
    assert(allocate_queue_storage(&g_receive_queue, 8U));
    assert(cleanup_after_reset(Status::DeviceFault, false) == Status::DeviceResetFailed);
    assert(routes == 1U && allocated == 11U && g_msix_table.mapped_pages == 1U);
    assert((msix_control & pci::msix::CONTROL_FUNCTION_MASK) != 0U);
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);
    assert(routes == 0U && allocated == 0U && mappings.empty());
    // Completion batches are validated before any ownership mutation.
    start(); map_common();
    assert(allocate_queue_storage(&g_transmit_queue, 8U));
    g_initialized = true;
    uint8_t payload[net::ETHERNET_HEADER_SIZE]{};
    g_transmit_queue.buffer_free[0] = false;
    g_transmit_queue.available_index = 1U;
    g_transmit_queue.used_elements[0].id = 8U;
    g_transmit_queue.used_header[1] = 1U;
    assert(transmit_frame(nullptr, payload, sizeof(payload)) == Status::DeviceFault);
    assert(g_transmit_queue.completion_fault &&
        !g_transmit_queue.buffer_free[0] &&
        g_transmit_queue.last_used_index == 0U);
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);

    start(); map_common();
    assert(allocate_queue_storage(&g_transmit_queue, 8U));
    g_initialized = true;
    g_transmit_queue.buffer_free[0] = false;
    g_transmit_queue.available_index = 2U;
    g_transmit_queue.used_elements[0].id = 0U;
    g_transmit_queue.used_elements[1].id = 0U;
    g_transmit_queue.used_header[1] = 2U;
    assert(transmit_frame(nullptr, payload, sizeof(payload)) == Status::DeviceFault);
    assert(g_transmit_queue.completion_fault &&
        !g_transmit_queue.buffer_free[0] &&
        g_transmit_queue.last_used_index == 0U);
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);

    start(); map_common();
    assert(allocate_queue_storage(&g_transmit_queue, 8U));
    g_initialized = true;
    g_transmit_queue.buffer_free[0] = false;
    g_transmit_queue.available_index = 1U;
    g_transmit_queue.used_elements[0].id = 0U;
    g_transmit_queue.used_header[1] = 2U;
    assert(transmit_frame(nullptr, payload, sizeof(payload)) == Status::DeviceFault);
    assert(g_transmit_queue.completion_fault &&
        g_transmit_queue.last_used_index == 0U);
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);

    start(); map_common();
    assert(allocate_queue_storage(&g_transmit_queue, 8U));
    g_transmit_queue.buffer_free[7] = false;
    g_transmit_queue.last_used_index = UINT16_MAX;
    g_transmit_queue.available_index = 0U;
    g_transmit_queue.used_elements[7].id = 7U;
    g_transmit_queue.used_header[1] = 0U;
    assert(reclaim_transmit() == Status::Ok);
    assert(!g_transmit_queue.completion_fault &&
        g_transmit_queue.buffer_free[7] &&
        g_transmit_queue.last_used_index == 0U);
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);

    start(); map_common();
    assert(allocate_queue_storage(&g_receive_queue, 8U));
    g_initialized = true;
    g_receive_queue.available_index = 2U;
    g_receive_queue.buffer_free[0] = false;
    g_receive_queue.buffer_free[1] = false;
    g_receive_queue.used_elements[0].id = 0U;
    g_receive_queue.used_elements[1].id = 0U;
    g_receive_queue.used_header[1] = 2U;
    uint8_t rx_packet[64]{};
    size_t rx_bytes = 0U;
    assert(receive_frame(nullptr, rx_packet, sizeof(rx_packet), &rx_bytes) == Status::DeviceFault);
    assert(g_receive_queue.completion_fault &&
        g_receive_queue.last_used_index == 0U &&
        g_receive_queue.available_index == 2U);
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);

    std::puts("VirtIO-net malformed completion ownership and 16-bit wrap: PASS");
    std::puts("VirtIO-net reset, DMA quarantine, MMIO rollback and PCI restore: PASS");
    std::puts("VirtIO-net MSI-X routing, deferred work, fallback and cleanup ownership: PASS");
    std::puts("VirtIO-net split RX/TX sources, shared fallback and partial group ownership: PASS");
}
