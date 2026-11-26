#include "../kernel/net/e1000.cpp"

#include <cassert>
#include <cstdio>

namespace {

using namespace net::e1000;
constexpr pci::Device endpoint{
    {0U, 3U, 0U}, 0x8086U, 0x10D3U, 2U, 0U, 0U, 1U, 0U, 0U,
};
constexpr uint32_t identity = UINT32_C(0x10D38086);
constexpr uint64_t bar = UINT64_C(0xE0000000);
constexpr uint16_t live_command = PCI_MEMORY_ENABLE | PCI_BUS_MASTER_ENABLE | 0x0140U;
constexpr uint32_t preserved_rctl = RCTL_BAM | RCTL_SECRC;
constexpr uint32_t preserved_tctl = TCTL_PSP;
constexpr size_t dma_page_count = DESCRIPTOR_COUNT * 2U + 2U;
uint32_t mmio[MMIO_BYTES / sizeof(uint32_t)]{};
memory::virtual_memory::AddressSpace address_space{};
TransmitDescriptor pending_tx[DESCRIPTOR_COUNT]{};
size_t pci_device_scans;
size_t msi_disables;
size_t bus_master_writes;
size_t freed;
size_t unmapped;
uint32_t live_identity;
uint64_t live_bar;
uint8_t live_revision;
uint16_t command;
bool stale_after_msi;
bool ignore_bme_clear;
bool fail_msi;
bool fail_page_release;
bool fail_unmap;

void prepare() {
    g_device = {};
    g_status = Status::Ok;
    live_identity = identity;
    live_revision = endpoint.revision;
    live_bar = bar;
    command = live_command;
    pci_device_scans = msi_disables = bus_master_writes = freed = unmapped = 0U;
    stale_after_msi = ignore_bme_clear = fail_msi = fail_page_release = fail_unmap = false;
    g_device.pci_device = endpoint;
    g_device.model = Model::I82574L;
    g_device.bar_physical = bar;
    g_device.mapped_base = MMIO_VIRTUAL_BASE;
    g_device.mapped_pages = 2U;
    g_device.registers = reinterpret_cast<volatile uint8_t*>(mmio);
    g_device.initialized = true;
    g_device.msi_active = true;
    g_device.msi_route.active = true;
    g_device.interface.context = &g_device;
    g_device.interface.transmit = transmit_callback;
    g_device.interface.receive = receive_callback;
    pending_tx[0] = {0x1234000U, 60U, 0U, TX_RS, 0U, 0U, 0U};
    g_device.rx_ring_page = {mmio, 0x2000U, true};
    g_device.tx_ring_page = {pending_tx, 0x3000U, true};
    for (size_t index = 0U; index < DESCRIPTOR_COUNT; ++index) {
        g_device.rx_buffers[index] = {mmio, 0x4000U + index * 0x2000U, true};
        g_device.tx_buffers[index] = {mmio, 0x5000U + index * 0x2000U, true};
    }
    mmio[REG_CTRL / 4U] = CTRL_SLU;
    mmio[REG_STATUS / 4U] = STATUS_LU;
    mmio[REG_RCTL / 4U] = RCTL_EN | preserved_rctl;
    mmio[REG_TCTL / 4U] = TCTL_EN | preserved_tctl;
    mmio[REG_RDT / 4U] = 7U;
    mmio[REG_TDT / 4U] = 1U;
    mmio[REG_IMC / 4U] = 0U;
}

void verify_no_dma_release(Status status) {
    const net::NetworkInterface cached = g_device.interface;
    assert(fail_initialize(Status::DeviceError) == status);
    assert(g_device.resources_retained && g_device.quiescing && !ready());
    arch::x86_64::interrupts::InterruptFrame frame_state{};
    mmio[REG_ICR / 4U] = INTERRUPT_LINK_STATUS_CHANGE;
    interrupt_handler(frame_state);
    assert(delivered_interrupts() == 0U);
    assert(g_device.tx_ring_page.allocated && pending_tx[0].status == 0U);
    assert(freed == 0U && unmapped == 0U);
    uint8_t frame[64]{};
    size_t received = 17U;
    assert(cached.transmit(cached.context, frame, sizeof(frame)) == net::Status::NotInitialized);
    assert(cached.receive(cached.context, frame, sizeof(frame), &received) ==
        net::Status::NotInitialized);
    assert(received == 0U && mmio[REG_TDT / 4U] == 1U && mmio[REG_RDT / 4U] == 7U);
    assert(initialize() == status && pci_device_scans == 0U);
}

} // namespace

namespace pci {

uint32_t read32(Address, uint8_t offset) {
    assert(offset == 0);
    return live_identity;
}

uint8_t read8(Address, uint8_t offset) {
    assert(offset == 0x08U);
    return live_revision;
}

uint16_t read16(Address, uint8_t offset) {
    assert(offset == PCI_COMMAND);
    return command;
}

void write16(Address, uint8_t offset, uint16_t value) {
    assert(offset == PCI_COMMAND);
    assert(live_identity == identity && live_bar == bar);
    assert(mmio[REG_IMC / 4U] == UINT32_MAX);
    assert(mmio[REG_RCTL / 4U] == preserved_rctl);
    assert(mmio[REG_TCTL / 4U] == preserved_tctl);
    assert((mmio[REG_CTRL / 4U] & CTRL_GIO_MASTER_DISABLE) != 0U);
    assert((mmio[REG_STATUS / 4U] & STATUS_GIO_MASTER_ENABLE) == 0U);
    assert(msi_disables == 1U && !g_device.msi_route.active);
    assert(value == (live_command & ~PCI_BUS_MASTER_ENABLE));
    if (!ignore_bme_clear) command = value;
    ++bus_master_writes;
}

uint64_t bar_address(const Device&, uint8_t index, bool* is_io) {
    assert(index == 0U);
    *is_io = false;
    return live_bar;
}

size_t device_count() {
    ++pci_device_scans;
    return 0U;
}

const Device* device_at(size_t) { return nullptr; }

void enable_bus_mastering(const Device&) { assert(false); }

} // namespace pci

namespace pci::msi {

Status enable(const pci::Device&, arch::x86_64::interrupts::InterruptHandler, Route*) {
    assert(false);
    return Status::InvalidArgument;
}

Status disable(Route* route) {
    assert(route->active && !g_device.initialized);
    assert((mmio[REG_STATUS / 4U] & STATUS_GIO_MASTER_ENABLE) == 0U);
    assert(bus_master_writes == 0U);
    ++msi_disables;
    if (fail_msi) return Status::StaleRoute;
    route->active = false;
    command = live_command;
    if (stale_after_msi) live_identity = UINT32_MAX;
    return Status::Ok;
}

} // namespace pci::msi

namespace storage::dma {

Status allocate_page(bool, Page*) {
    assert(false);
    return Status::InvalidArgument;
}

Status release_page(Page* page) {
    assert((command & PCI_BUS_MASTER_ENABLE) == 0U);
    assert(freed < dma_page_count && bus_master_writes == 1U && !g_device.initialized);
    if (fail_page_release) return Status::ReleaseFailed;
    assert(page->allocated);
    *page = {};
    ++freed;
    return Status::Ok;
}

} // namespace storage::dma

namespace memory::kernel_virtual_memory {

virtual_memory::AddressSpace* address_space() {
    return &::address_space;
}

} // namespace memory::kernel_virtual_memory

namespace memory::virtual_memory {

Status query_page(const AddressSpace*, uint64_t, Mapping*) { return Status::NotMapped; }

Status map_page(AddressSpace*, uint64_t, uint64_t, MapFlags) {
    assert(false);
    return Status::InvalidArgument;
}

Status unmap_page(AddressSpace*, uint64_t address, Mapping*) {
    assert((command & PCI_BUS_MASTER_ENABLE) == 0U && freed == dma_page_count);
    assert(address == MMIO_VIRTUAL_BASE + (1U - unmapped) * PAGE_SIZE);
    if (fail_unmap && unmapped == 1U) return Status::InvalidArgument;
    ++unmapped;
    return Status::Ok;
}

} // namespace memory::virtual_memory

int main() {
    prepare();
    g_device.initialized = false;
    arch::x86_64::interrupts::InterruptFrame frame_state{};
    mmio[REG_ICR / 4U] = INTERRUPT_LINK_STATUS_CHANGE;
    interrupt_handler(frame_state);
    assert(delivered_interrupts() == 1U);

    prepare();
    assert(fail_initialize(Status::DeviceError) == Status::DeviceError);
    assert(!g_device.resources_retained && !ready());
    assert(msi_disables == 1U && bus_master_writes == 1U);
    assert(freed == dma_page_count && unmapped == 2U);
    assert(g_device.registers == nullptr && !g_device.tx_ring_page.allocated);
    assert(initialize() == Status::NotFound && pci_device_scans == 1U);

    prepare();
    mmio[REG_STATUS / 4U] |= STATUS_GIO_MASTER_ENABLE;
    verify_no_dma_release(Status::DmaQuiesceFailed);
    assert(msi_disables == 0U && bus_master_writes == 0U);

    prepare();
    ignore_bme_clear = true;
    verify_no_dma_release(Status::DmaQuiesceFailed);
    assert(msi_disables == 1U && bus_master_writes == 1U && command == live_command);

    prepare();
    stale_after_msi = true;
    verify_no_dma_release(Status::DmaQuiesceFailed);
    assert(msi_disables == 1U && bus_master_writes == 0U);

    prepare();
    fail_msi = true;
    verify_no_dma_release(Status::ResourceReleaseFailed);
    assert(g_device.msi_route.active && bus_master_writes == 0U);

    prepare();
    live_identity = UINT32_MAX;
    verify_no_dma_release(Status::DmaQuiesceFailed);
    assert(mmio[REG_IMC / 4U] == 0U && mmio[REG_CTRL / 4U] == CTRL_SLU);

    prepare();
    live_revision ^= 1U;
    verify_no_dma_release(Status::DmaQuiesceFailed);
    assert(mmio[REG_IMC / 4U] == 0U);

    prepare();
    live_bar += 0x20000U;
    verify_no_dma_release(Status::DmaQuiesceFailed);
    assert(mmio[REG_IMC / 4U] == 0U);

    prepare();
    command &= ~PCI_MEMORY_ENABLE;
    verify_no_dma_release(Status::DmaQuiesceFailed);
    assert(mmio[REG_IMC / 4U] == 0U);

    prepare();
    mmio[REG_CTRL / 4U] = UINT32_MAX;
    verify_no_dma_release(Status::DmaQuiesceFailed);
    assert(mmio[REG_IMC / 4U] == 0U);

    prepare();
    mmio[REG_CTRL / 4U] |= CTRL_RST;
    verify_no_dma_release(Status::DmaQuiesceFailed);
    assert(mmio[REG_IMC / 4U] == 0U);

    prepare();
    fail_page_release = true;
    verify_no_dma_release(Status::ResourceReleaseFailed);
    assert(bus_master_writes == 1U);

    prepare();
    fail_unmap = true;
    assert(fail_initialize(Status::DeviceError) == Status::ResourceReleaseFailed);
    assert(freed == dma_page_count && unmapped == 1U && g_device.mapped_pages == 1U);
    assert(!g_device.tx_ring_page.allocated && g_device.resources_retained);
    assert(initialize() == Status::ResourceReleaseFailed && pci_device_scans == 0U);

    std::puts("Intel E1000e retirement tests passed");
}
