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
uint64_t bar_bytes = 4096U;
uint16_t msix_control = 0U;
unsigned routes = 0U;
unsigned bar_probes = 0U;
arch::x86_64::interrupts::InterruptHandler irq_handler = nullptr;

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
    g_interrupt_status = InterruptStatus::NotInitialized;
    apic_ready = false; capability_present = true;
    malformed_capability = reject_route = reject_route_cleanup = false;
    separate_pending_bar = false; bar_bytes = 4096U;
    msix_control = 0U; bar_probes = 0U; irq_handler = nullptr;
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
    *output = {0x40U, false, false, 3U, 0U, 0U,
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
Status enable_single(const Device&, uint16_t entry, const MmioRegion& table,
        const MmioRegion& pending, arch::x86_64::interrupts::InterruptHandler handler,
        Route* output) {
    assert(entry == 0U && handler && table.bytes == fixture::bar_bytes);
    assert(fixture::mappings.count(reinterpret_cast<uintptr_t>(table.virtual_address)));
    assert(fixture::mappings.count(reinterpret_cast<uintptr_t>(pending.virtual_address)));
    if (!fixture::separate_pending_bar) assert(table.virtual_address == pending.virtual_address);
    if (fixture::reject_route) return Status::VectorUnavailable;
    *output = {};
    output->active = true;
    output->vector = {0x40U, 1U};
    output->programmed.capability_offset = 0x40U;
    output->programmed.original_command = fixture::command;
    ++fixture::routes;
    fixture::irq_handler = handler;
    fixture::command |= PCI_COMMAND_INTX_DISABLE;
    return Status::Ok;
}
Status disable(Route* route) {
    assert(route->active && fixture::routes == 1U);
    assert(fixture::common[20U] == 0U); // Device reset precedes vector release.
    assert(net::virtio_net::g_msix_table.mapped_pages != 0U);
    if (fixture::reject_route_cleanup) return Status::StaleRoute;
    fixture::command = route->programmed.original_command;
    *route = {};
    --fixture::routes;
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
    assert(interrupt_diagnostics().vector == 0U && !interrupt_diagnostics().pending);

    start(); map_common(); apic_ready = true;
    assert(prepare_queue_interrupts());
    assert(allocate_queue_storage(&g_receive_queue, 8U));
    assert(cleanup_after_reset(Status::DeviceFault, false) == Status::DeviceResetFailed);
    assert(routes == 1U && allocated == 11U && g_msix_table.mapped_pages == 1U);
    assert((msix_control & pci::msix::CONTROL_FUNCTION_MASK) != 0U);
    assert(cleanup_after_reset(Status::DeviceFault, true) == Status::DeviceFault);
    assert(routes == 0U && allocated == 0U && mappings.empty());
    std::puts("VirtIO-net reset, DMA quarantine, MMIO rollback and PCI restore: PASS");
    std::puts("VirtIO-net MSI-X routing, deferred work, fallback and cleanup ownership: PASS");
}
