#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

// Exercise the production driver's ownership, DMA and error lifecycle.
// Host adapters below are not a substitute for the QEMU device matrix.
#include "../kernel/storage/virtio_block.cpp"

namespace fixture {
using namespace storage::virtio_block;
alignas(4096) uint8_t common[4096];
uint16_t notify = 0U;
uint16_t command = 0U;
size_t allocated = 0U;
size_t released = 0U;
bool reject_release = false;
bool reject_command_restore = false;
memory::virtual_memory::AddressSpace space{};

void start() {
    assert(!g_controller_owned && allocated == 0U);
    g_controller = {};
    std::memset(common, 0, sizeof(common));
    command = 6U;
    notify = 0U;
    released = 0U;
    reject_release = reject_command_restore = false;
    g_controller.original_command = 0U;
    g_controller.command_owned = true;
    // A host memory register bank; no virtual mappings need to be released.
    g_controller.common = {0U, common, 0U, sizeof(common)};
    common[20] = kStatusDriverOk;
    assert(allocate_queue_storage(&g_controller.queue, 8U));
    g_controller.queue.configured = true;
    g_controller.queue.notify = &notify;
    assert(protocol::decode_geometry(64U, false, 0U, false, true,
        &g_controller.geometry) == protocol::Status::Ok);
    g_controller.ready = true;
}
}

namespace pci {
uint16_t read16(Address, uint8_t offset) {
    assert(offset == 4U); return fixture::command;
}
void write16(Address, uint8_t offset, uint16_t value) {
    assert(offset == 4U);
    if (fixture::reject_command_restore && value == 0U) return;
    fixture::command = value;
}
const Device* find(uint16_t, uint16_t, size_t) { return nullptr; }
uint64_t bar_address(const Device&, uint8_t, bool* io) { *io = false; return 0U; }
}
namespace drivers::virtio::pci_transport {
Status discover(const pci::Device&, Layout*) { return Status::MissingRequiredCapability; }
bool region_valid(const Capability&, size_t) { return false; }
}
namespace memory::kernel_virtual_memory {
virtual_memory::AddressSpace* address_space() { return &fixture::space; }
}
namespace memory::virtual_memory {
Status query_page(const AddressSpace*, uint64_t, Mapping*) { return Status::NotMapped; }
Status map_page(AddressSpace*, uint64_t, uint64_t, MapFlags) { return Status::Ok; }
Status unmap_page(AddressSpace*, uint64_t, Mapping*) { return Status::Ok; }
}
namespace storage::dma {
Status allocate_page(bool, Page* page) {
    void* data = std::aligned_alloc(4096U, 4096U);
    assert(data);
    *page = {data, reinterpret_cast<uintptr_t>(data), true};
    ++fixture::allocated;
    return Status::Ok;
}
Status release_page(Page* page) {
    assert(page && page->allocated && fixture::allocated);
    // Never free a page still reachable by active device DMA.
    assert(fixture::common[20] == 0U);
    assert((fixture::command & 4U) == 0U);
    if (fixture::reject_release) return Status::ReleaseFailed;
    std::free(page->virtual_address);
    *page = {};
    --fixture::allocated;
    ++fixture::released;
    return Status::Ok;
}
}
namespace storage::device_registry {
bool register_device(Backend, size_t, const block::Device*, const char*) { return true; }
bool unregister_device(const block::Device*) { return true; }
}

int main() {
    using namespace storage::virtio_block;
    uint8_t buffer[8192]{};
    fixture::start();
    std::memset(g_controller.queue.data_page.virtual_address, 0xA5, 4096U);
    {
        ControllerGuard owner;
        assert(owner.acquired());
        std::vector<std::thread> contenders;
        for (unsigned i = 0U; i < 4U; ++i) contenders.emplace_back([&] {
            assert(read_blocks_callback(&g_controller, 0U, 1U, buffer) == storage::block::Status::DeviceBusy);
            assert(write_blocks_callback(&g_controller, 0U, 1U, buffer) == storage::block::Status::DeviceBusy);
            assert(flush_callback(&g_controller) == storage::block::Status::DeviceBusy);
            assert(initialize() == Status::DeviceBusy);
            assert(shutdown() == Status::DeviceBusy);
        });
        for (auto& thread : contenders) thread.join();
        assert(fixture::released == 0U && g_controller.ready);
        for (size_t i = 0; i < 4096U; ++i)
            assert(static_cast<uint8_t*>(g_controller.queue.data_page.virtual_address)[i] == 0xA5);
    }
    // Reject the entire range before copying even the first valid chunk.
    assert(write_blocks_callback(&g_controller, 63U, 2U, buffer) == storage::block::Status::InvalidArgument);
    assert(g_controller.queue.available_index == 0U);
    assert(static_cast<uint8_t*>(g_controller.queue.data_page.virtual_address)[0] == 0xA5);
    assert(read_blocks_callback(&g_controller, UINT64_MAX, 2U, buffer) == storage::block::Status::InvalidArgument);
    assert(!g_controller_owned);
    // No device completes this request: production polling must time out,
    // reset the register bank, stop DMA, and release exactly five pages.
    assert(read_blocks_callback(&g_controller, 0U, 1U, buffer) == storage::block::Status::TimedOut);
    assert(!g_controller.ready && !g_controller_owned);
    assert(fixture::released == 5U && fixture::allocated == 0U);
    assert(shutdown() == Status::Ok);

    fixture::start();
    // An unsolicited completion poisons the queue and forces a reset.
    g_controller.queue.used_header[1] = 1U;
    assert(read_blocks_callback(&g_controller, 0U, 1U, buffer) == storage::block::Status::DeviceFault);
    assert(!g_controller.ready && fixture::allocated == 0U);
    assert(shutdown() == Status::Ok);

    fixture::start();
    // Unknown DMA ownership after reset failure must quarantine every page.
    assert(cleanup_after_reset(Status::TimedOut, false) == Status::DeviceResetFailed);
    assert(g_controller.cleanup_blocked && !g_controller.ready);
    assert(fixture::allocated == 5U && fixture::released == 0U);
    assert(initialize() == Status::DeviceResetFailed);
    assert(shutdown() == Status::Ok); // Retry reset/cleanup successfully.
    assert(fixture::allocated == 0U);

    fixture::start();
    fixture::reject_release = true;
    assert(shutdown() == Status::ResourceReleaseFailed);
    assert(g_controller.cleanup_blocked && fixture::allocated == 5U);
    fixture::reject_release = false;
    assert(shutdown() == Status::Ok);
    assert(fixture::released == 5U && fixture::allocated == 0U);

    fixture::start();
    fixture::reject_command_restore = true;
    assert(shutdown() == Status::ResourceReleaseFailed);
    assert(g_controller.command_owned && g_controller.cleanup_blocked);
    fixture::reject_command_restore = false;
    assert(shutdown() == Status::Ok);
    assert(fixture::allocated == 0U && !g_controller_owned);
    puts("VirtIO block ownership, timeout, DMA quarantine and cleanup retry: PASS");
}
