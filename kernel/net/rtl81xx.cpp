#include "rtl81xx.hpp"

#include "rtl81xx_detection.hpp"
#include "rtl81xx_ring.hpp"
#include "../arch/x86_64/hpet.hpp"
#include "../drivers/pci.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/virtual_memory.hpp"
#include "../storage/dma.hpp"

namespace net::rtl81xx {
namespace {

constexpr size_t MMIO_BYTES = 4096U;
constexpr uint64_t MMIO_VIRTUAL_BASE = UINT64_C(0xFFFFB80000000000);
constexpr size_t DESCRIPTOR_COUNT = 8U;
constexpr uint32_t TIMER_POLL_LIMIT = UINT32_C(20000000);
constexpr uint32_t LINK_TIMEOUT_US = UINT32_C(5000000);
constexpr uint32_t RX_GATE_SETTLE_US = 2000U;
constexpr uint32_t FIFO_DRAIN_TIMEOUT_US = 10000U;
constexpr uint32_t RESET_TIMEOUT_US = 10000U;
constexpr uint32_t PCI_PENDING_TIMEOUT_US = 100000U;
constexpr uint8_t PCI_COMMAND = 0x04U;
constexpr uint16_t PCI_MEMORY_SPACE = UINT16_C(0x0002);
constexpr uint16_t PCI_BUS_MASTER = UINT16_C(0x0004);

constexpr size_t REG_MAC = 0x00U;
constexpr size_t REG_TX_DESC = 0x20U;
constexpr size_t REG_CHIP_CMD = 0x37U;
constexpr size_t REG_TX_POLL = 0x38U;
constexpr size_t REG_INT_MASK = 0x3CU;
constexpr size_t REG_INT_STATUS = 0x3EU;
constexpr size_t REG_TX_CONFIG = 0x40U;
constexpr size_t REG_RX_CONFIG = 0x44U;
constexpr size_t REG_CONFIG_LOCK = 0x50U;
constexpr size_t REG_PHY_STATUS = 0x6CU;
constexpr size_t REG_MCU = 0xD3U;
constexpr size_t REG_RX_MAX_SIZE = 0xDAU;
constexpr size_t REG_RX_DESC = 0xE4U;
constexpr size_t REG_MISC = 0xF0U;

constexpr uint8_t CMD_RESET = UINT8_C(0x10);
constexpr uint8_t CMD_RX = UINT8_C(0x08);
constexpr uint8_t CMD_TX = UINT8_C(0x04);
constexpr uint8_t PHY_LINK_UP = UINT8_C(0x02);
constexpr uint8_t TX_POLL_NORMAL = UINT8_C(0x40);
constexpr uint32_t RX_ACCEPT_PHYSICAL_BROADCAST = UINT32_C(0x0000E70A);
constexpr uint32_t TX_CONFIG_DEFAULT = UINT32_C(0x03000700);
constexpr uint16_t RTL8168H_XID = UINT16_C(0x541);
constexpr uint32_t TX_XID_MASK = UINT32_C(0xFCF);
constexpr uint32_t TX_FIFO_EMPTY = UINT32_C(1) << 11U;
constexpr uint32_t RX_ACCEPT_MASK = UINT32_C(0x3F);
constexpr uint32_t RX_GATE = UINT32_C(1) << 19U;
constexpr uint8_t MCU_FIFOS_EMPTY = UINT8_C(0x30);
constexpr uint16_t PCI_TRANSACTIONS_PENDING = UINT16_C(1) << 5U;

enum class StopStage : uint8_t {
    Unpublished,
    DrainFifos,
    WaitForReset,
    DisableBusMaster,
    WaitForTransactions,
    Release,
};

struct Device {
    pci::Device pci_device;
    volatile uint8_t* registers;
    size_t mapped_pages;
    uint64_t bar_physical;
    uint8_t bar_index;
    storage::dma::Page rx_ring_page;
    storage::dma::Page tx_ring_page;
    storage::dma::Page rx_buffers[DESCRIPTOR_COUNT];
    storage::dma::Page tx_buffers[DESCRIPTOR_COUNT];
    MacAddress mac;
    size_t next_rx;
    size_t next_tx;
    uint64_t rx_frames;
    uint64_t tx_frames;
    uint64_t dropped;
    StopStage stop_stage;
    uint8_t pcie_capability;
    bool identity_lost;
    bool stale_mmio;
    bool started;
    // An invalid DMA binding is terminal until an explicit, verified
    // controller shutdown. Never overwrite live/ambiguous DMA ownership.
    bool faulted;
};

Device g_device{};
Status g_status = Status::NotFound;
uint16_t g_mac_xid = 0U;

bool g_controller_owned = false;
class ControllerGuard {
public:
    ControllerGuard() : owned_(!__atomic_test_and_set(
        &g_controller_owned, __ATOMIC_ACQUIRE)) {}
    ~ControllerGuard() {
        if (owned_) __atomic_clear(&g_controller_owned, __ATOMIC_RELEASE);
    }
    bool acquired() const { return owned_; }
    ControllerGuard(const ControllerGuard&) = delete;
    ControllerGuard& operator=(const ControllerGuard&) = delete;

private:
    bool owned_;
};

net::Status transmit(void* context, const uint8_t* data, size_t count);
net::Status receive(void* context, uint8_t* out, size_t capacity,
                    size_t* out_length);
// NetworkStack can retain this pointer while the hardware is stopped.
NetworkInterface g_interface{&g_device, transmit, receive, {}, ETHERNET_MTU};
bool link_up_owned();

inline uint8_t read8(const Device& device, size_t offset) {
    return *(device.registers + offset);
}
inline uint32_t read32(const Device& device, size_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(device.registers + offset);
}
inline void write8(Device& device, size_t offset, uint8_t value) {
    *(device.registers + offset) = value;
}
inline void write16(Device& device, size_t offset, uint16_t value) {
    *reinterpret_cast<volatile uint16_t*>(device.registers + offset) = value;
}
inline void write32(Device& device, size_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(device.registers + offset) = value;
}
inline void relax() { __asm__ volatile("pause" : : : "memory"); }

bool pci_identity_matches(const pci::Device& device) {
    const uint32_t expected = static_cast<uint32_t>(device.device_id) << 16U |
        device.vendor_id;
    return pci::read32(device, 0x00U) == expected;
}

bool configure_pci_command(const pci::Device& device, bool enable_dma) {
    if (!pci_identity_matches(device)) return false;
    const uint16_t current = pci::read16(device, PCI_COMMAND);
    if (current == UINT16_MAX) return false;
    constexpr uint16_t mask = PCI_MEMORY_SPACE | PCI_BUS_MASTER;
    const uint16_t required = enable_dma ? mask : PCI_MEMORY_SPACE;
    if (!pci_identity_matches(device)) return false;
    pci::write16(device, PCI_COMMAND, static_cast<uint16_t>((current & ~mask) | required));
    const uint16_t applied = pci::read16(device, PCI_COMMAND);
    return applied != UINT16_MAX && (applied & mask) == required &&
        pci_identity_matches(device);
}

// Validate the selected live BAR before dereferencing its cached mapping.
// A relocated/unreadable BAR must not redirect I/O or replace DMA ownership.
bool registers_available(bool require_dma) {
    if (g_device.registers == nullptr || g_device.bar_physical == 0U ||
        !pci_identity_matches(g_device.pci_device)) return false;
    bool io_space = false;
    const uint64_t live_bar = pci::bar_address(
        g_device.pci_device, g_device.bar_index, &io_space);
    if (io_space || live_bar != g_device.bar_physical) return false;
    const uint16_t command = pci::read16(g_device.pci_device, PCI_COMMAND);
    const uint16_t required = PCI_MEMORY_SPACE | (require_dma ? PCI_BUS_MASTER : 0U);
    return command != UINT16_MAX && (command & required) == required &&
        pci_identity_matches(g_device.pci_device);
}

// Keep observing the specific function and BAR throughout retirement.
bool owns_pci_function() {
    if (g_device.identity_lost) return false;
    if (!pci_identity_matches(g_device.pci_device)) {
        g_device.identity_lost = true;
        return false;
    }
    return true;
}

bool owns_mapped_bar() {
    if (g_device.stale_mmio || !owns_pci_function()) return false;
    bool io_space = false;
    const uint64_t live_bar = pci::bar_address(
        g_device.pci_device, g_device.bar_index, &io_space);
    if (io_space || live_bar != g_device.bar_physical) {
        g_device.stale_mmio = true;
        return false;
    }
    return owns_pci_function();
}

bool disable_bus_master() {
    if (!owns_pci_function()) return false;
    const auto& device = g_device.pci_device;
    const uint16_t command = pci::read16(device, PCI_COMMAND);
    if (command == UINT16_MAX || !owns_pci_function()) return false;
    if ((command & PCI_BUS_MASTER) != 0U) {
        pci::write16(device, PCI_COMMAND,
            static_cast<uint16_t>(command & ~PCI_BUS_MASTER));
    }
    const uint16_t applied = pci::read16(device, PCI_COMMAND);
    return owns_pci_function() && applied != UINT16_MAX &&
        (applied & PCI_BUS_MASTER) == 0U;
}

bool stop_target_available(bool require_dma = false) {
    if (g_device.registers == nullptr || !owns_mapped_bar()) return false;
    const uint16_t command = pci::read16(g_device.pci_device, PCI_COMMAND);
    const uint16_t required = PCI_MEMORY_SPACE | (require_dma ? PCI_BUS_MASTER : 0U);
    if (command == UINT16_MAX || (command & required) != required ||
        !owns_pci_function()) return false;
    const uint32_t tx = read32(g_device, REG_TX_CONFIG);
    if (tx == UINT32_MAX) return false;
    const uint16_t xid = static_cast<uint16_t>((tx >> 20U) & TX_XID_MASK);
    if (xid != RTL8168H_XID) {
        g_device.stale_mmio = true;
        return false;
    }
    return true;
}

uint16_t read_mac_xid() {
    const uint32_t tx = read32(g_device, REG_TX_CONFIG);
    return tx == UINT32_MAX ? 0U : static_cast<uint16_t>((tx >> 20U) & TX_XID_MASK);
}

struct Deadline {
    uint64_t start;
    uint64_t ticks;
    bool counter_64_bit;

    bool elapsed() const {
        const uint64_t delta = arch::x86_64::hpet::counter() - start;
        return (counter_64_bit ? delta : static_cast<uint32_t>(delta)) >= ticks;
    }
};

bool start_deadline(uint32_t microseconds, Deadline* deadline) {
    namespace hpet = arch::x86_64::hpet;
    const auto* caps = hpet::capabilities();
    if (caps == nullptr || caps->counter_period_femtoseconds == 0U) return false;
    const uint64_t period = caps->counter_period_femtoseconds;
    const uint64_t ticks =
        (static_cast<uint64_t>(microseconds) * UINT64_C(1000000000) +
            period - 1U) / period;
    if (!caps->counter_64_bit && ticks > UINT32_MAX) return false;
    *deadline = {hpet::counter(), ticks, caps->counter_64_bit};
    return true;
}

enum class Poll : uint8_t { Waiting, Complete, Failed };

template <typename Check>
bool poll_until(uint32_t microseconds, Check check) {
    Deadline deadline{};
    if (!start_deadline(microseconds, &deadline)) return false;
    for (uint32_t attempt = 0U; attempt < TIMER_POLL_LIMIT; ++attempt) {
        const Poll state = check();
        if (state != Poll::Waiting) return state == Poll::Complete;
        if (deadline.elapsed()) return false;
        relax();
    }
    return false;
}

bool wait_for_rx_gate() {
    Deadline deadline{};
    if (!start_deadline(RX_GATE_SETTLE_US, &deadline)) return false;
    for (uint32_t attempt = 0U; attempt < TIMER_POLL_LIMIT; ++attempt) {
        if (deadline.elapsed()) return true;
        relax();
    }
    return false;
}

bool set_rx_gate(bool gate) {
    if (!stop_target_available()) return false;
    const uint32_t misc = read32(g_device, REG_MISC);
    if (misc == UINT32_MAX) return false;
    write32(g_device, REG_MISC, gate ? misc | RX_GATE : misc & ~RX_GATE);
    const uint32_t readback = read32(g_device, REG_MISC);
    return readback != UINT32_MAX && ((readback & RX_GATE) != 0U) == gate;
}

bool valid_pcie_capability(uint8_t offset) {
    if (offset < 0x40U || offset > 0xF4U || (offset & 3U) != 0U ||
        !owns_pci_function()) return false;
    const auto& device = g_device.pci_device;
    const uint16_t header = pci::read16(device, offset);
    const uint16_t flags = pci::read16(device, static_cast<uint8_t>(offset + 2U));
    const uint16_t version = flags & UINT16_C(0x000F);
    const uint16_t port_type = (flags >> 4U) & UINT16_C(0x000F);
    return owns_pci_function() && (header & UINT16_C(0x00FF)) ==
        static_cast<uint8_t>(pci::CapabilityId::PciExpress) &&
        (version == 1U || version == 2U) && port_type <= 1U;
}

bool find_pcie_capability() {
    pci::Capability cap{};
    if (!pci::find_capability(
            g_device.pci_device, pci::CapabilityId::PciExpress, &cap) ||
        !valid_pcie_capability(cap.offset)) return false;
    g_device.pcie_capability = cap.offset;
    return true;
}

bool drain_fifos() {
    if (!stop_target_available()) return false;
    const uint32_t rx_config = read32(g_device, REG_RX_CONFIG);
    if (rx_config == UINT32_MAX) return false;
    write16(g_device, REG_INT_MASK, 0U);
    write32(g_device, REG_RX_CONFIG, rx_config & ~RX_ACCEPT_MASK);
    const uint32_t readback = read32(g_device, REG_RX_CONFIG);
    if (readback == UINT32_MAX || (readback & RX_ACCEPT_MASK) != 0U ||
        !set_rx_gate(true) || !wait_for_rx_gate()) return false;

    // RTL8168H requires both the TxConfig FIFO and the MCU RX/TX FIFOs
    // empty before reset. The read-only MAC XID gates this sequence.
    return poll_until(FIFO_DRAIN_TIMEOUT_US, [] {
        if (!stop_target_available()) return Poll::Failed;
        const uint32_t misc = read32(g_device, REG_MISC);
        const uint32_t rx = read32(g_device, REG_RX_CONFIG);
        const uint32_t tx = read32(g_device, REG_TX_CONFIG);
        const uint8_t mcu = read8(g_device, REG_MCU);
        if (misc == UINT32_MAX || (misc & RX_GATE) == 0U ||
            rx == UINT32_MAX || (rx & RX_ACCEPT_MASK) != 0U ||
            tx == UINT32_MAX || mcu == UINT8_MAX) return Poll::Failed;
        return (tx & TX_FIFO_EMPTY) != 0U &&
            (mcu & MCU_FIFOS_EMPTY) == MCU_FIFOS_EMPTY
            ? Poll::Complete : Poll::Waiting;
    });
}

bool wait_for_reset() {
    return poll_until(RESET_TIMEOUT_US, [] {
        if (!stop_target_available()) return Poll::Failed;
        const uint8_t command = read8(g_device, REG_CHIP_CMD);
        if (command == UINT8_MAX) return Poll::Failed;
        return (command & (CMD_RESET | CMD_RX | CMD_TX)) == 0U
            ? Poll::Complete : Poll::Waiting;
    });
}

bool wait_for_pci_transactions() {
    return poll_until(PCI_PENDING_TIMEOUT_US, [] {
        const uint8_t offset = g_device.pcie_capability;
        if (!owns_mapped_bar() || !valid_pcie_capability(offset)) return Poll::Failed;
        const auto& device = g_device.pci_device;
        const uint16_t command = pci::read16(device, PCI_COMMAND);
        const uint16_t status = pci::read16(device,
            static_cast<uint8_t>(offset + 0x0AU));
        if (!owns_pci_function() || command == UINT16_MAX ||
            (command & PCI_BUS_MASTER) != 0U || status == UINT16_MAX) return Poll::Failed;
        return (status & PCI_TRANSACTIONS_PENDING) == 0U
            ? Poll::Complete : Poll::Waiting;
    });
}

bool prove_dma_quiescence() {
    if (g_device.stop_stage == StopStage::DrainFifos) {
        if (!drain_fifos() || !stop_target_available()) return false;
        write8(g_device, REG_CHIP_CMD, CMD_RESET);
        g_device.stop_stage = StopStage::WaitForReset;
    }
    if (g_device.stop_stage == StopStage::WaitForReset) {
        if (!wait_for_reset()) return false;
        g_device.stop_stage = StopStage::DisableBusMaster;
    }
    if (g_device.stop_stage == StopStage::DisableBusMaster) {
        if (!disable_bus_master()) return false;
        g_device.stop_stage = StopStage::WaitForTransactions;
    }
    if (!wait_for_pci_transactions() || !owns_mapped_bar()) return false;
    const uint16_t command = pci::read16(g_device.pci_device, PCI_COMMAND);
    if (!owns_pci_function() || command == UINT16_MAX ||
        (command & PCI_BUS_MASTER) != 0U) return false;
    g_device.stop_stage = StopStage::Release;
    return true;
}

bool cleanup_before_start(Device* device);

bool map_registers(uint64_t physical, Device* device) {
    if (device == nullptr || physical == 0U ||
        physical > UINT64_MAX - (MMIO_BYTES - 1U) ||
        device->registers != nullptr || device->mapped_pages != 0U ||
        device->started) return false;
    auto* space = memory::kernel_virtual_memory::address_space();
    if (space == nullptr) return false;
    constexpr uint64_t page_size = memory::virtual_memory::PAGE_SIZE;
    constexpr uint64_t mask = page_size - 1U;
    const uint64_t base = physical & ~mask;
    const uint64_t offset = physical & mask;
    const size_t pages = static_cast<size_t>((offset + MMIO_BYTES + mask) / page_size);
    if (pages == 0U || pages > 2U) return false;
    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    size_t mapped = 0U;
    for (; mapped < pages; ++mapped) {
        memory::virtual_memory::Mapping existing{};
        if (memory::virtual_memory::query_page(
                space, MMIO_VIRTUAL_BASE + mapped * page_size, &existing) !=
                memory::virtual_memory::Status::NotMapped ||
            memory::virtual_memory::map_page(
                space, MMIO_VIRTUAL_BASE + mapped * page_size,
                base + mapped * page_size, flags) !=
                memory::virtual_memory::Status::Ok)
            break;
    }
    // Publish only the prefix installed by this attempt. Failed rollback
    // retains its owner and must never unmap a pre-existing collision.
    device->mapped_pages = mapped;
    if (mapped != 0U)
        device->registers = reinterpret_cast<volatile uint8_t*>(MMIO_VIRTUAL_BASE + offset);
    if (mapped != pages) {
        static_cast<void>(cleanup_before_start(device));
        return false;
    }
    return true;
}

bool cleanup_before_start(Device* device) {
    if (device == nullptr || device->started ||
        (device->stop_stage != StopStage::Unpublished &&
         device->stop_stage != StopStage::Release)) return false;
    for (size_t i = 0U; i < DESCRIPTOR_COUNT; ++i) {
        if (device->rx_buffers[i].allocated &&
            storage::dma::release_page(&device->rx_buffers[i]) !=
                storage::dma::Status::Ok) return false;
        if (device->tx_buffers[i].allocated &&
            storage::dma::release_page(&device->tx_buffers[i]) !=
                storage::dma::Status::Ok) return false;
    }
    if (device->rx_ring_page.allocated &&
        storage::dma::release_page(&device->rx_ring_page) !=
            storage::dma::Status::Ok) return false;
    if (device->tx_ring_page.allocated &&
        storage::dma::release_page(&device->tx_ring_page) !=
            storage::dma::Status::Ok) return false;
    auto* space = memory::kernel_virtual_memory::address_space();
    if (device->mapped_pages != 0U && space == nullptr) return false;
    // Remove the mapping suffix so a retry owns exactly the remaining prefix.
    while (device->mapped_pages != 0U) {
        const uint64_t address = MMIO_VIRTUAL_BASE +
            (device->mapped_pages - 1U) * memory::virtual_memory::PAGE_SIZE;
        if (memory::virtual_memory::unmap_page(space, address) !=
            memory::virtual_memory::Status::Ok) return false;
        --device->mapped_pages;
    }
    *device = {};
    return true;
}

Status fail_before_start(Status cause) {
    return g_status = cleanup_before_start(&g_device)
        ? cause : Status::ResourceReleaseFailed;
}

inline dma::Descriptor* rx_ring() {
    return static_cast<dma::Descriptor*>(g_device.rx_ring_page.virtual_address);
}
inline dma::Descriptor* tx_ring() {
    return static_cast<dma::Descriptor*>(g_device.tx_ring_page.virtual_address);
}

bool allocate_dma() {
    if (storage::dma::allocate_page(false, &g_device.rx_ring_page) !=
            storage::dma::Status::Ok ||
        storage::dma::allocate_page(false, &g_device.tx_ring_page) !=
            storage::dma::Status::Ok) return false;
    auto* rx = rx_ring();
    auto* tx = tx_ring();
    for (size_t i = 0U; i < DESCRIPTOR_COUNT; ++i) {
        if (storage::dma::allocate_page(false, &g_device.rx_buffers[i]) !=
                storage::dma::Status::Ok ||
            storage::dma::allocate_page(false, &g_device.tx_buffers[i]) !=
                storage::dma::Status::Ok) return false;
        rx[i] = {dma::rx_rearm(i + 1U == DESCRIPTOR_COUNT, dma::RX_BUFFER_BYTES),
                 0U, g_device.rx_buffers[i].physical_address};
        tx[i] = {i + 1U == DESCRIPTOR_COUNT ? dma::END_OF_RING : 0U,
                 0U, g_device.tx_buffers[i].physical_address};
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    return true;
}

net::Status transmit(void* context, const uint8_t* data, size_t count) {
    if (context != &g_device)
        return net::Status::NotInitialized;
    ControllerGuard ownership;
    if (!ownership.acquired()) return net::Status::QueueFull;
    if (!g_device.started)
        return net::Status::NotInitialized;
    if (data == nullptr || count < dma::MIN_ETHERNET_FRAME)
        return net::Status::InvalidArgument;
    if (count > dma::MAX_ETHERNET_FRAME) return net::Status::FrameTooLarge;
    /* The controller may lose carrier or disappear after initialization.
     * Never offer fresh DMA descriptors when PHY status is down/invalid. */
    if (!link_up_owned()) return net::Status::InterfaceError;
    const size_t index = g_device.next_tx;
    dma::Descriptor& slot = tx_ring()[index];
    const uint32_t flags = __atomic_load_n(&slot.flags, __ATOMIC_ACQUIRE);
    if (!dma::valid_descriptor_binding(slot, flags,
            g_device.tx_buffers[index].physical_address,
            index + 1U == DESCRIPTOR_COUNT, dma::RX_BUFFER_BYTES)) {
        __atomic_store_n(&g_device.faulted, true, __ATOMIC_RELEASE);
        return net::Status::InterfaceError;
    }
    if ((flags & dma::OWN) != 0U) {
        return net::Status::QueueFull;
    }
    auto* bytes = static_cast<uint8_t*>(g_device.tx_buffers[index].virtual_address);
    /* Ethernet requires at least 60 payload/header bytes before the FCS.
     * Zero-pad short frames rather than leaking stale DMA buffer contents. */
    const size_t wire_length = count < 60U ? 60U : count;
    for (size_t i = 0U; i < count; ++i) bytes[i] = data[i];
    for (size_t i = count; i < wire_length; ++i) bytes[i] = 0U;
    slot.vlan = 0U;
    __atomic_store_n(&slot.flags,
        dma::tx_submit(index + 1U == DESCRIPTOR_COUNT, wire_length),
        __ATOMIC_RELEASE);
    g_device.next_tx = (index + 1U) % DESCRIPTOR_COUNT;
    write8(g_device, REG_TX_POLL, TX_POLL_NORMAL);
    ++g_device.tx_frames;
    return net::Status::Ok;
}

net::Status receive(void* context, uint8_t* out, size_t capacity,
                    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    if (context != &g_device)
        return net::Status::NotInitialized;
    ControllerGuard ownership;
    if (!ownership.acquired()) return net::Status::WouldBlock;
    if (!g_device.started)
        return net::Status::NotInitialized;
    if (out == nullptr || out_length == nullptr)
        return net::Status::InvalidArgument;
    /* A removed controller or lost link must not expose stale RX DMA
     * buffers as a valid frame. Preserve the descriptor for recovery. */
    if (!link_up_owned()) return net::Status::InterfaceError;
    const size_t index = g_device.next_rx;
    dma::Descriptor& slot = rx_ring()[index];
    const uint32_t flags = __atomic_load_n(&slot.flags, __ATOMIC_ACQUIRE);
    if (!dma::valid_descriptor_binding(slot, flags,
            g_device.rx_buffers[index].physical_address,
            index + 1U == DESCRIPTOR_COUNT, dma::RX_BUFFER_BYTES)) {
        __atomic_store_n(&g_device.faulted, true, __ATOMIC_RELEASE);
        return net::Status::InterfaceError;
    }
    const dma::ReceiveInfo info = dma::inspect_rx(flags, capacity);
    if (info.result == dma::Receive::NotReady) {
        return net::Status::WouldBlock;
    }
    if (info.result == dma::Receive::OutputTooSmall) {
        *out_length = info.payload_length;
        return net::Status::BufferTooSmall;
    }
    if (info.result == dma::Receive::Ready) {
        const auto* data = static_cast<const uint8_t*>(
            g_device.rx_buffers[index].virtual_address);
        for (size_t i = 0U; i < info.payload_length; ++i) out[i] = data[i];
        *out_length = info.payload_length;
        ++g_device.rx_frames;
    } else {
        ++g_device.dropped;
    }
    slot.vlan = 0U;
    __atomic_store_n(&slot.flags,
        dma::rx_rearm(index + 1U == DESCRIPTOR_COUNT, dma::RX_BUFFER_BYTES),
        __ATOMIC_RELEASE);
    g_device.next_rx = (index + 1U) % DESCRIPTOR_COUNT;
    return info.result == dma::Receive::Ready
        ? net::Status::Ok : net::Status::InterfaceError;
}

Status start_mapped_controller() {
    /* Firmware may leave memory decoding off or bus mastering on. Both
     * bits must be confirmed before touching registers or allocating rings. */
    if (!configure_pci_command(g_device.pci_device, false) || !registers_available(false)) {
        return fail_before_start(Status::DeviceFault);
    }
    g_mac_xid = read_mac_xid();
    if (g_mac_xid != RTL8168H_XID)
        return fail_before_start(Status::UnsupportedRevision);
    Deadline deadline{};
    if (!find_pcie_capability() || !start_deadline(RESET_TIMEOUT_US, &deadline))
        return fail_before_start(Status::DmaQuiescenceUnverified);

    write16(g_device, REG_INT_MASK, 0U);
    write16(g_device, REG_INT_STATUS, UINT16_MAX);
    write8(g_device, REG_CHIP_CMD, CMD_RESET);
    if (!wait_for_reset()) return g_status = Status::ResetTimedOut;

    for (size_t i = 0U; i < MAC_ADDRESS_LENGTH; ++i)
        g_device.mac.bytes[i] = read8(g_device, REG_MAC + i);
    if (mac_is_zero(g_device.mac) || mac_is_broadcast(g_device.mac) ||
        mac_is_multicast(g_device.mac)) {
        return fail_before_start(Status::InvalidMac);
    }
    // Callbacks held by the network stack retain their original MAC.
    if (!mac_is_zero(g_interface.hardware_address) &&
        !mac_equal(g_interface.hardware_address, g_device.mac)) {
        return fail_before_start(Status::InvalidMac);
    }
    if (!set_rx_gate(false))
        return fail_before_start(Status::DmaQuiescenceUnverified);
    if (!allocate_dma()) {
        return fail_before_start(Status::DmaAllocationFailed);
    }

    if (!stop_target_available()) return fail_before_start(Status::DeviceFault);
    write8(g_device, REG_CONFIG_LOCK, UINT8_C(0xC0));
    write32(g_device, REG_TX_CONFIG, TX_CONFIG_DEFAULT);
    write32(g_device, REG_RX_CONFIG, RX_ACCEPT_PHYSICAL_BROADCAST);
    write16(g_device, REG_RX_MAX_SIZE, static_cast<uint16_t>(dma::RX_BUFFER_BYTES));
    write32(g_device, REG_TX_DESC,
        static_cast<uint32_t>(g_device.tx_ring_page.physical_address));
    write32(g_device, REG_TX_DESC + 4U, 0U);
    write32(g_device, REG_RX_DESC,
        static_cast<uint32_t>(g_device.rx_ring_page.physical_address));
    write32(g_device, REG_RX_DESC + 4U, 0U);
    write8(g_device, REG_CONFIG_LOCK, 0U);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    g_device.stop_stage = StopStage::DrainFifos;
    if (!configure_pci_command(g_device.pci_device, true) ||
        !stop_target_available(true)) {
        static_cast<void>(disable_bus_master());
        return g_status = Status::DeviceFault;
    }
    write8(g_device, REG_CHIP_CMD, static_cast<uint8_t>(CMD_RX | CMD_TX));

    if (mac_is_zero(g_interface.hardware_address))
        g_interface.hardware_address = g_device.mac;
    g_device.started = true;
    const bool link_ready = poll_until(LINK_TIMEOUT_US, [] {
        return link_up_owned() ? Poll::Complete : Poll::Waiting;
    });
    return g_status = link_ready ? Status::Ok : Status::LinkDown;
}

} // namespace

Status initialize() {
    ControllerGuard ownership;
    if (!ownership.acquired()) return Status::Busy;
    /* After an unverified reset, keep the original BAR mapping and do not
     * overwrite ownership by retrying initialization on the same device. */
    if (g_device.registers != nullptr && !g_device.started)
        return g_status;
    if (g_device.started) {
        // A corrupted live ring is quarantined. Reinitializing here would
        // free or reuse DMA pages while the controller might still own them.
        if (__atomic_load_n(&g_device.faulted, __ATOMIC_ACQUIRE))
            return Status::DeviceFault;
        g_status = link_up_owned() ? Status::Ok : Status::LinkDown;
        return g_status;
    }
    const pci::Device* found = pci::find(UINT16_C(0x10EC), UINT16_C(0x8168));
    if (found == nullptr || classify_model(found->vendor_id, found->device_id) ==
            Model::Unknown) return g_status = Status::NotFound;
    g_device = {};
    g_mac_xid = 0U;
    g_device.pci_device = *found;

    bool io_space = false;
    g_device.bar_index = 2U;
    uint64_t bar = pci::bar_address(*found, g_device.bar_index, &io_space);
    if (bar == 0U || io_space) {
        g_device.bar_index = 0U;
        bar = pci::bar_address(*found, g_device.bar_index, &io_space);
        if (bar == 0U || io_space) return g_status = Status::InvalidBar;
    }
    g_device.bar_physical = bar;
    if (!map_registers(bar, &g_device))
        return g_status = g_device.mapped_pages == 0U
            ? Status::MappingFailed : Status::ResourceReleaseFailed;

    return start_mapped_controller();
}

Status shutdown() {
    ControllerGuard ownership;
    if (!ownership.acquired()) return Status::Busy;
    g_device.started = false;
    if (g_device.stop_stage != StopStage::Unpublished &&
        g_device.stop_stage != StopStage::Release && !prove_dma_quiescence()) {
        static_cast<void>(disable_bus_master());
        return g_status = Status::DmaQuiescenceUnverified;
    }
    return g_status = cleanup_before_start(&g_device)
        ? Status::Ok : Status::ResourceReleaseFailed;
}

namespace {
bool link_up_owned() {
    if (!g_device.started ||
        __atomic_load_n(&g_device.faulted, __ATOMIC_ACQUIRE) ||
        g_device.registers == nullptr) return false;
    if (!registers_available(true)) return false;
    const uint8_t command = read8(g_device, REG_CHIP_CMD);
    constexpr uint8_t mask = CMD_RESET | CMD_RX | CMD_TX;
    if (command == UINT8_MAX || (command & mask) != (CMD_RX | CMD_TX))
        return false;
    const uint8_t phy = read8(g_device, REG_PHY_STATUS);
    return phy != UINT8_MAX && (phy & PHY_LINK_UP) != 0U;
}
} // namespace

bool link_up() {
    ControllerGuard ownership;
    return ownership.acquired() && link_up_owned();
}
bool ready() { return link_up(); }
NetworkInterface* interface() { return ready() ? &g_interface : nullptr; }
Status last_status() {
    ControllerGuard ownership;
    if (!ownership.acquired()) return Status::Busy;
    return __atomic_load_n(&g_device.faulted, __ATOMIC_ACQUIRE)
        ? Status::DeviceFault : g_status;
}
uint16_t mac_xid() {
    ControllerGuard ownership;
    return ownership.acquired() ? g_mac_xid : 0U;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::NotFound: return "RTL8111/8168 not found";
        case Status::InvalidBar: return "RTL8111/8168 has no usable MMIO BAR";
        case Status::MappingFailed: return "RTL8111/8168 MMIO map failed";
        case Status::ResetTimedOut: return "RTL8111/8168 reset timed out";
        case Status::InvalidMac: return "RTL8111/8168 has an invalid MAC";
        case Status::DmaAllocationFailed: return "RTL8111/8168 DMA allocation failed";
        case Status::LinkDown: return "RTL8111/8168 link down";
        case Status::DeviceFault: return "RTL8111/8168 device fault";
        case Status::ResourceReleaseFailed: return "RTL8111/8168 resource release failed";
        case Status::Busy: return "RTL8111/8168 driver is busy";
        case Status::UnsupportedRevision: return "RTL8111/8168 MAC revision is unsupported";
        case Status::DmaQuiescenceUnverified: return "RTL8111/8168 DMA quiescence unverified";
    }
    return "unknown RTL8111/8168 error";
}
} // namespace net::rtl81xx
