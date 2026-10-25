#include <cassert>
#include <cstdio>

#include "../kernel/drivers/usb/xhci.cpp"

namespace {
using drivers::usb::xhci::Controller;
using drivers::usb::xhci::OP_USBCMD;
using drivers::usb::xhci::OP_USBSTS;
using drivers::usb::xhci::CMD_RUN;
using drivers::usb::xhci::STS_HALTED;
uint16_t pci_command = 0U;
bool reject_master_disable = false;
size_t release_calls = 0U;
size_t unmap_calls = 0U;
size_t map_calls = 0U;
size_t fail_map_call = 0U;
uint64_t fail_unmap = 0U;
storage::dma::Page* fail_release = nullptr;
bool require_halt = false;
alignas(4) uint8_t mmio[0x40]{};
alignas(4096) uint8_t dma_frame[4096]{};
memory::virtual_memory::AddressSpace test_space{};

Controller make_controller(bool published = true) {
    using namespace drivers::usb::xhci;
    for (auto& byte : mmio) byte = 0U;
    Controller c{};
    c.operational = mmio;
    c.keyboard_device = drivers::device::INVALID_DEVICE_ID;
    c.dma_published = published;
    c.bus_master_enabled = published;
    c.initialized = published;
    c.mapped_base = 0x100000U;
    c.mapped_pages = 2U;
    c.data_page = {dma_frame, reinterpret_cast<uintptr_t>(dma_frame), true};
    write32(c.operational, OP_USBCMD, published ? CMD_RUN : 0U);
    pci_command = published ? 6U : 2U;
    release_calls = 0U;
    unmap_calls = 0U;
    map_calls = 0U;
    fail_map_call = 0U;
    fail_unmap = 0U;
    fail_release = nullptr;
    reject_master_disable = false;
    require_halt = published;
    return c;
}
} // namespace

namespace pci {
uint16_t read16(Address, uint8_t offset) {
    assert(offset == 0x04U);
    return pci_command;
}
void write16(Address, uint8_t offset, uint16_t value) {
    assert(offset == 0x04U);
    if (!reject_master_disable) pci_command = value;
}
} // namespace pci

namespace storage::dma {
Status release_page(Page* page) {
    assert(page != nullptr && page->allocated);
    if (require_halt) {
        assert((drivers::usb::xhci::read32(mmio, OP_USBCMD) & CMD_RUN) == 0U);
        assert(drivers::usb::xhci::read32(mmio, OP_USBSTS) == STS_HALTED);
        assert((pci_command & 4U) == 0U);
    }
    ++release_calls;
    if (page == fail_release) return Status::ReleaseFailed;
    *page = {};
    return Status::Ok;
}
} // namespace storage::dma

namespace memory::kernel_virtual_memory {
virtual_memory::AddressSpace* address_space() { return &test_space; }
} // namespace memory::kernel_virtual_memory

namespace memory::virtual_memory {
Status query_page(const AddressSpace*, uint64_t, Mapping*) {
    return Status::NotMapped;
}
Status map_page(AddressSpace* space, uint64_t, uint64_t, MapFlags) {
    assert(space == &test_space);
    ++map_calls;
    return map_calls == fail_map_call ? Status::InvalidArgument : Status::Ok;
}
Status unmap_page(AddressSpace* space, uint64_t address, Mapping*) {
    assert(space == &test_space);
    ++unmap_calls;
    if (address == fail_unmap) return Status::InvalidArgument;
    return Status::Ok;
}
} // namespace memory::virtual_memory

int main() {
    using namespace drivers::usb::xhci;
    namespace device = drivers::device;
    assert(device::initialize() == KStatus::Ok);

    Controller c = make_controller();
    assert(release_resources(&c) == Status::ControllerHaltTimeout);
    assert(release_calls == 0U && unmap_calls == 0U);
    assert(c.cleanup_pending && c.dma_published && c.data_page.allocated);
    assert(!c.initialized && (pci_command & 4U) == 0U);
    write32(c.operational, OP_USBSTS, STS_HALTED);
    assert(release_resources(&c) == Status::Ok);
    assert(release_calls == 1U && unmap_calls == 2U);
    assert(!c.cleanup_pending && !c.data_page.allocated);
    assert(release_resources(&c) == Status::Ok);
    assert(release_calls == 1U && unmap_calls == 2U);

    c = make_controller();
    write32(c.operational, OP_USBSTS, UINT32_MAX);
    assert(release_resources(&c) == Status::ControllerHaltTimeout);
    assert(c.cleanup_pending && release_calls == 0U);
    write32(c.operational, OP_USBSTS, STS_HALTED);
    assert(release_resources(&c) == Status::Ok);

    c = make_controller();
    write32(c.operational, OP_USBCMD, UINT32_MAX);
    assert(release_resources(&c) == Status::ControllerHaltTimeout);
    assert(c.cleanup_pending && release_calls == 0U && unmap_calls == 0U);
    assert((pci_command & 4U) == 0U); // Contain DMA even if MMIO is gone.
    write32(c.operational, OP_USBCMD, 0U);
    write32(c.operational, OP_USBSTS, STS_HALTED);
    assert(release_resources(&c) == Status::Ok);

    c = make_controller();
    write32(c.operational, OP_USBSTS, STS_HALTED);
    reject_master_disable = true;
    assert(release_resources(&c) == Status::ResourceReleaseFailed);
    assert(c.cleanup_pending && release_calls == 0U && unmap_calls == 0U);
    reject_master_disable = false;
    assert(release_resources(&c) == Status::Ok);

    c = make_controller();
    write32(c.operational, OP_USBSTS, STS_HALTED);
    fail_release = &c.data_page;
    assert(release_resources(&c) == Status::ResourceReleaseFailed);
    assert(c.cleanup_pending && c.data_page.allocated && unmap_calls == 0U);
    fail_release = nullptr;
    assert(release_resources(&c) == Status::Ok);
    assert(release_calls == 2U && unmap_calls == 2U);

    c = make_controller(false);
    // Unpublished pages can be released even if firmware never acknowledged
    // ownership. Cleanup must not touch firmware's command register.
    write32(c.operational, OP_USBCMD, CMD_RUN);
    fail_unmap = c.mapped_base;
    assert(release_resources(&c) == Status::ResourceReleaseFailed);
    assert(c.cleanup_pending && c.mapped_pages == 1U && release_calls == 1U);
    assert(read32(mmio, OP_USBCMD) == CMD_RUN);
    fail_unmap = 0U;
    assert(release_resources(&c) == Status::Ok);
    assert(release_calls == 1U && unmap_calls == 3U);

    c = make_controller(false);
    fail_map_call = 3U;
    assert(!map_mmio(0xF0000000U, &c));
    assert(c.mapped_pages == 2U && unmap_calls == 0U);
    fail_unmap = c.mapped_base;
    assert(release_resources(&c) == Status::ResourceReleaseFailed);
    assert(c.mapped_pages == 1U && c.cleanup_pending);
    fail_unmap = 0U;
    assert(release_resources(&c) == Status::Ok);
    assert(unmap_calls == 3U && release_calls == 1U);

    // A failed claim must not strand a newly registered USB child slot.
    c = make_controller(false);
    c.parent_device = device::INVALID_DEVICE_ID;
    c.owner_driver = device::INVALID_DRIVER_ID; // Force the real claim failure.
    assert(!register_keyboard(c));
    assert(c.keyboard_device != device::INVALID_DEVICE_ID);
    const auto stale = device::handle_for(c.keyboard_device);
    assert(device::active_count() == 1U);
    assert(release_resources(&c) == Status::Ok);
    assert(device::active_count() == 0U && device::resolve(stale) == nullptr);

    c = make_controller(false);
    c.parent_device = device::INVALID_DEVICE_ID;
    c.owner_driver = 7U;
    assert(register_keyboard(c));
    const auto live = device::handle_for(c.keyboard_device);
    assert(device::resolve(live) != nullptr);
    assert(release_resources(&c) == Status::Ok);
    assert(device::active_count() == 0U && device::resolve(live) == nullptr);

    std::puts("xHCI halt, DMA quarantine and cleanup retry: PASS");
    return 0;
}
