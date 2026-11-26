#include "../kernel/net/rtl81xx.cpp"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace net::rtl81xx;

constexpr uint32_t realtek_identity = UINT32_C(0x816810EC);
constexpr uint32_t xid_bits = static_cast<uint32_t>(RTL8168H_XID) << 20U;
constexpr uint64_t bar = UINT64_C(0x8000);
constexpr uint8_t pcie_offset = 0x60U;
constexpr size_t page_count = DESCRIPTOR_COUNT * 2U + 2U;
constexpr size_t page_size = memory::virtual_memory::PAGE_SIZE;

alignas(page_size) uint32_t mmio[MMIO_BYTES / sizeof(uint32_t)];
alignas(page_size) uint8_t frames[page_count][page_size];
bool allocated[page_count];
memory::virtual_memory::AddressSpace address_space{};
uint32_t identity;
uint64_t live_bar;
uint8_t capability_offset;
uint16_t pcie_header;
uint16_t pcie_flags;
uint16_t pci_command;
uint16_t transactions;
size_t allocations;
size_t releases;
size_t unmaps;
size_t pci_writes;
size_t reset_requests;
size_t scans;
uint64_t micros;
uint64_t link_deadline;
bool active;
bool mapped;
bool hold_reset;
bool saw_reset;
bool reject_bme_disable;
bool check_quiescence_on_release;
bool timer_present;
size_t fail_release;

uint32_t& reg32(size_t offset) { return mmio[offset / sizeof(uint32_t)]; }
uint8_t& reg8(size_t offset) {
    return reinterpret_cast<uint8_t*>(mmio)[offset];
}

void fixture_reset() {
    g_device = {};
    g_status = Status::NotFound;
    g_mac_xid = 0U;
    g_interface.hardware_address = {};
    std::memset(mmio, 0, sizeof(mmio));
    std::memset(allocated, 0, sizeof(allocated));
    identity = realtek_identity;
    live_bar = bar;
    capability_offset = pcie_offset;
    pcie_header = static_cast<uint8_t>(pci::CapabilityId::PciExpress);
    pcie_flags = 2U;
    pci_command = 0x0102U;
    transactions = 0U;
    allocations = releases = unmaps = pci_writes = reset_requests = scans = 0U;
    micros = link_deadline = 0U;
    active = mapped = hold_reset = saw_reset = reject_bme_disable =
        check_quiescence_on_release = false;
    timer_present = true;
    fail_release = page_count;
}

void attach_device() {
    assert(!mapped);
    g_device.pci_device = {{0U, 3U, 0U}, 0x10ECU, 0x8168U,
        0x02U, 0U, 0U, 0x15U, 0U, 0U};
    g_device.registers = reinterpret_cast<volatile uint8_t*>(mmio);
    g_device.mapped_pages = 1U;
    g_device.bar_index = 2U;
    g_device.bar_physical = bar;
    mapped = true;
    reg32(REG_TX_CONFIG) = xid_bits | TX_FIFO_EMPTY;
    reg8(REG_MCU) = MCU_FIFOS_EMPTY;
    reg8(REG_MAC) = 0x02U;
    reg8(REG_MAC + 5U) = 0xA5U;
}

void start() {
    attach_device();
    // Waiting for carrier uses actual elapsed time rather than pause cycles.
    reg8(REG_PHY_STATUS) = 0U;
    link_deadline = micros + 1500000U;
    assert(start_mapped_controller() == Status::Ok);
    assert(micros >= link_deadline && mac_xid() == RTL8168H_XID);
    assert(allocations == page_count && reset_requests == 1U);
    assert(reg8(REG_CHIP_CMD) == (CMD_RX | CMD_TX));
    assert((reg32(REG_MISC) & RX_GATE) == 0U);
    assert((pci_command & PCI_BUS_MASTER) != 0U);
    check_quiescence_on_release = true;
}

void expect_retained() {
    assert(releases == 0U && unmaps == 0U && mapped);
    for (bool live : allocated) assert(live);
    assert(interface() == nullptr);
}

void test_shutdown_then_initialize_again() {
    fixture_reset();
    start();
    auto* const iface = interface();
    assert(iface != nullptr);
    uint8_t frame[60]{};
    assert(iface->transmit(iface->context, frame, sizeof(frame)) == net::Status::Ok);
    assert((tx_ring()[0].flags & dma::OWN) != 0U);
    assert((rx_ring()[0].flags & dma::OWN) != 0U);

    assert(shutdown() == Status::Ok);
    assert(releases == page_count && unmaps == 1U && reset_requests == 2U);
    assert((pci_command & PCI_BUS_MASTER) == 0U && !mapped);
    assert(iface->transmit(iface->context, frame, sizeof(frame)) ==
        net::Status::NotInitialized);
    assert(interface() == nullptr);
    assert(shutdown() == Status::Ok && releases == page_count && unmaps == 1U);

    // Fake the MMIO mapping delivered by a new initialize() attempt and
    // execute the same startup routine the real driver uses after mapping.
    allocations = reset_requests = 0U;
    start();
    assert(interface() == iface && g_device.next_rx == 0U && g_device.next_tx == 0U);
    assert((tx_ring()[0].flags & dma::OWN) == 0U);
    assert((rx_ring()[0].flags & dma::OWN) != 0U);
    assert(shutdown() == Status::Ok && releases == page_count * 2U && unmaps == 2U);
}

void test_stop_stages_retain_and_retry() {
    fixture_reset();
    start();

    // FIFO evidence is device-specific. A clear PCI Transactions Pending bit
    // does not replace either of the controller's empty indicators.
    reg32(REG_TX_CONFIG) &= ~TX_FIFO_EMPTY;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();
    assert((pci_command & PCI_BUS_MASTER) == 0U && reset_requests == 1U);

    reg32(REG_TX_CONFIG) |= TX_FIFO_EMPTY;
    reg8(REG_MCU) = UINT8_C(0x20);
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();
    assert(reset_requests == 1U);

    reg8(REG_MCU) = MCU_FIFOS_EMPTY;
    hold_reset = true;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();
    assert(reset_requests == 2U);
    assert(initialize() == Status::DmaQuiescenceUnverified && scans == 0U);

    hold_reset = false;
    transactions = PCI_TRANSACTIONS_PENDING;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();
    assert(reset_requests == 2U);

    transactions = UINT16_MAX;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();
    transactions = 0U;
    pcie_header = 0x0005U;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();

    pcie_header = static_cast<uint8_t>(pci::CapabilityId::PciExpress);
    fail_release = 5U;
    assert(shutdown() == Status::ResourceReleaseFailed);
    assert(releases > 0U && releases < page_count && mapped && unmaps == 0U);
    assert(initialize() == Status::ResourceReleaseFailed && scans == 0U);

    fail_release = page_count;
    assert(shutdown() == Status::Ok);
    assert(releases == page_count && unmaps == 1U && reset_requests == 2U);
}

void test_unverified_bme_and_device_change() {
    fixture_reset();
    start();
    reject_bme_disable = true;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();
    assert((pci_command & PCI_BUS_MASTER) != 0U && reset_requests == 2U);
    reject_bme_disable = false;
    assert(shutdown() == Status::Ok && releases == page_count);

    fixture_reset();
    start();
    identity = UINT32_MAX;
    const size_t writes_before = pci_writes;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();
    assert(pci_writes == writes_before && reset_requests == 1U);
    identity = realtek_identity;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();

    fixture_reset();
    start();
    live_bar += page_size;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();
    assert((pci_command & PCI_BUS_MASTER) == 0U && reset_requests == 1U);
    live_bar = bar;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();

    fixture_reset();
    start();
    reg32(REG_TX_CONFIG) = UINT32_C(0x6C0) << 20U;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();
    assert((pci_command & PCI_BUS_MASTER) == 0U && reset_requests == 1U);
    reg32(REG_TX_CONFIG) = xid_bits | TX_FIFO_EMPTY;
    assert(shutdown() == Status::DmaQuiescenceUnverified);
    expect_retained();
}

void test_admission_and_concurrent_ownership() {
    fixture_reset();
    attach_device();
    reg32(REG_TX_CONFIG) = UINT32_C(0x502) << 20U;
    assert(start_mapped_controller() == Status::UnsupportedRevision);
    assert(mac_xid() == UINT16_C(0x502) && allocations == 0U);
    assert(reset_requests == 0U && (pci_command & PCI_BUS_MASTER) == 0U);

    fixture_reset();
    attach_device();
    capability_offset = 0xF8U;
    assert(start_mapped_controller() == Status::DmaQuiescenceUnverified);
    assert(allocations == 0U && reset_requests == 0U);

    fixture_reset();
    attach_device();
    timer_present = false;
    assert(start_mapped_controller() == Status::DmaQuiescenceUnverified);
    assert(allocations == 0U && reset_requests == 0U);

    fixture_reset();
    start();
    auto* const iface = interface();
    ControllerGuard owner;
    assert(owner.acquired());
    uint8_t frame[60]{};
    assert(iface->transmit(iface->context, frame, sizeof(frame)) ==
        net::Status::QueueFull);
    size_t length = 1U;
    assert(iface->receive(iface->context, frame, sizeof(frame), &length) ==
        net::Status::WouldBlock && length == 0U);
    assert(initialize() == Status::Busy && shutdown() == Status::Busy);
    assert(g_device.next_tx == 0U && g_device.next_rx == 0U && releases == 0U);
    assert(reg8(REG_CHIP_CMD) == (CMD_RX | CMD_TX));
}
} // namespace

namespace pci {
const Device* find(uint16_t, uint16_t, size_t) {
    ++scans;
    return nullptr;
}
uint32_t read32(Address, uint8_t offset) {
    assert(offset == 0U);
    return identity;
}
uint16_t read16(Address, uint8_t offset) {
    switch (offset) {
        case 4U: return pci_command;
        case pcie_offset: return pcie_header;
        case pcie_offset + 2U: return pcie_flags;
        case pcie_offset + 0xAU:
            assert(reset_requests == 2U && reg8(REG_CHIP_CMD) == 0U);
            assert((pci_command & PCI_BUS_MASTER) == 0U);
            return transactions;
    }
    assert(false);
    return UINT16_MAX;
}
void write16(Address, uint8_t offset, uint16_t command) {
    assert(offset == 4U && identity == realtek_identity);
    ++pci_writes;
    if (reject_bme_disable && (command & PCI_BUS_MASTER) == 0U) return;
    if ((command & PCI_BUS_MASTER) != 0U &&
        (pci_command & PCI_BUS_MASTER) == 0U) {
        // Hardware preserves the read-only XID across TxConfig writes.
        reg32(REG_TX_CONFIG) |= xid_bits | TX_FIFO_EMPTY;
        assert(reg8(REG_CHIP_CMD) == 0U);
        active = true;
    }
    pci_command = command;
}
uint64_t bar_address(const Device&, uint8_t index, bool* io) {
    assert(index == 2U);
    *io = false;
    return live_bar;
}
bool find_capability(const Device&, CapabilityId id, Capability* cap) {
    assert(id == CapabilityId::PciExpress);
    *cap = {static_cast<uint8_t>(id), capability_offset, 0U};
    return true;
}
} // namespace pci

namespace arch::x86_64::hpet {
const Capabilities* capabilities() {
    static Capabilities caps{};
    caps.counter_period_femtoseconds = UINT32_C(100000000);
    caps.counter_64_bit = true;
    return timer_present ? &caps : nullptr;
}
uint64_t counter() {
    micros += 500U;
    if (link_deadline != 0U && micros >= link_deadline)
        reg8(REG_PHY_STATUS) = PHY_LINK_UP;
    if ((reg8(REG_CHIP_CMD) & CMD_RESET) != 0U) {
        if (!saw_reset) {
            saw_reset = true;
            ++reset_requests;
            if (active) {
                assert((reg32(REG_RX_CONFIG) & RX_ACCEPT_MASK) == 0U);
                assert((reg32(REG_MISC) & RX_GATE) != 0U);
                assert((reg32(REG_TX_CONFIG) & TX_FIFO_EMPTY) != 0U);
                assert((reg8(REG_MCU) & MCU_FIFOS_EMPTY) == MCU_FIFOS_EMPTY);
                assert(releases == 0U || releases == page_count);
            }
        }
        if (!hold_reset) {
            reg8(REG_CHIP_CMD) = 0U;
            saw_reset = false;
            active = false;
        }
    }
    return micros * 10U;
}
} // namespace arch::x86_64::hpet

namespace net {
bool mac_is_zero(const MacAddress& address) {
    const MacAddress zero{};
    return mac_equal(address, zero);
}
bool mac_is_broadcast(const MacAddress& address) {
    const MacAddress broadcast{{0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU}};
    return mac_equal(address, broadcast);
}
bool mac_is_multicast(const MacAddress& address) {
    return (address.bytes[0] & 1U) != 0U;
}
bool mac_equal(const MacAddress& left, const MacAddress& right) {
    return std::memcmp(left.bytes, right.bytes, MAC_ADDRESS_LENGTH) == 0;
}
} // namespace net

namespace storage::dma {
Status allocate_page(bool supports_64_bits, Page* page) {
    assert(!supports_64_bits && allocations < page_count);
    const size_t index = allocations++;
    assert(!allocated[index]);
    allocated[index] = true;
    *page = {frames[index], 0x10000U + index * page_size, true};
    return Status::Ok;
}
Status release_page(Page* page) {
    const size_t index = (page->physical_address - 0x10000U) / page_size;
    assert(index < page_count && allocated[index] && page->allocated);
    if (check_quiescence_on_release) {
        assert((pci_command & PCI_BUS_MASTER) == 0U && transactions == 0U);
        assert(reset_requests == 2U && reg8(REG_CHIP_CMD) == 0U);
    }
    if (index == fail_release) return Status::ReleaseFailed;
    allocated[index] = false;
    *page = {};
    ++releases;
    return Status::Ok;
}
} // namespace storage::dma

namespace memory::kernel_virtual_memory {
virtual_memory::AddressSpace* address_space() { return &::address_space; }
}
namespace memory::virtual_memory {
Status query_page(const AddressSpace*, uint64_t, Mapping*) {
    return Status::NotMapped;
}
Status map_page(AddressSpace*, uint64_t, uint64_t, MapFlags) {
    assert(false);
    return Status::InvalidArgument;
}
Status unmap_page(AddressSpace*, uint64_t address, Mapping*) {
    assert(address == MMIO_VIRTUAL_BASE && mapped);
    mapped = false;
    ++unmaps;
    return Status::Ok;
}
} // namespace memory::virtual_memory

int main() {
    test_shutdown_then_initialize_again();
    test_stop_stages_retain_and_retry();
    test_unverified_bme_and_device_change();
    test_admission_and_concurrent_ownership();
    std::puts("RTL8168H active shutdown and restart: PASS");
}
