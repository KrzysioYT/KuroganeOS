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

void start() {
    assert(allocated == 0U && mappings.empty());
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
uint16_t read16(Address, uint8_t offset) { assert(offset == 4U); return fixture::command; }
void write16(Address, uint8_t offset, uint16_t value) {
    assert(offset == 4U);
    if (fixture::reject_restore && value == net::virtio_net::g_original_command) return;
    fixture::command = value;
}
uint64_t bar_address(const Device&, uint8_t, bool* io) { *io = false; return 0x100000U; }
const Device* find(uint16_t, uint16_t, size_t) { ++fixture::found_queries; return nullptr; }
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
    std::puts("VirtIO-net reset, DMA quarantine, MMIO rollback and PCI restore: PASS");
}
