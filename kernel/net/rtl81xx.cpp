#include "rtl81xx.hpp"

#include "rtl81xx_detection.hpp"
#include "rtl81xx_ring.hpp"
#include "../drivers/pci.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/virtual_memory.hpp"
#include "../storage/dma.hpp"

namespace net::rtl81xx {
namespace {

constexpr size_t MMIO_BYTES = 4096U;
constexpr uint64_t MMIO_VIRTUAL_BASE = UINT64_C(0xFFFFB80000000000);
constexpr size_t DESCRIPTOR_COUNT = 8U;
constexpr uint32_t RESET_BUDGET = UINT32_C(250000);
constexpr uint32_t LINK_BUDGET = UINT32_C(250000);

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
constexpr size_t REG_RX_MAX_SIZE = 0xDAU;
constexpr size_t REG_RX_DESC = 0xE4U;

constexpr uint8_t CMD_RESET = UINT8_C(0x10);
constexpr uint8_t CMD_RX = UINT8_C(0x08);
constexpr uint8_t CMD_TX = UINT8_C(0x04);
constexpr uint8_t PHY_LINK_UP = UINT8_C(0x02);
constexpr uint8_t TX_POLL_NORMAL = UINT8_C(0x40);
constexpr uint32_t RX_ACCEPT_PHYSICAL_BROADCAST = UINT32_C(0x0000E70A);
constexpr uint32_t TX_CONFIG_DEFAULT = UINT32_C(0x03000700);

struct Device {
    pci::Device pci_device;
    volatile uint8_t* registers;
    size_t mapped_pages;
    storage::dma::Page rx_ring_page;
    storage::dma::Page tx_ring_page;
    storage::dma::Page rx_buffers[DESCRIPTOR_COUNT];
    storage::dma::Page tx_buffers[DESCRIPTOR_COUNT];
    NetworkInterface interface;
    MacAddress mac;
    size_t next_rx;
    size_t next_tx;
    uint64_t rx_frames;
    uint64_t tx_frames;
    uint64_t dropped;
    bool rx_busy;
    bool tx_busy;
    bool started;
};

Device g_device{};
Status g_status = Status::NotFound;

inline uint8_t read8(const Device& device, size_t offset) {
    return *(device.registers + offset);
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

bool map_registers(uint64_t physical, Device* device) {
    if (device == nullptr || physical == 0U) return false;
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
    if (mapped != pages) {
        while (mapped != 0U) {
            --mapped;
            (void)memory::virtual_memory::unmap_page(space,
                MMIO_VIRTUAL_BASE + mapped * page_size);
        }
        return false;
    }
    device->registers = reinterpret_cast<volatile uint8_t*>(MMIO_VIRTUAL_BASE + offset);
    device->mapped_pages = pages;
    return true;
}

/* This cleanup is only allowed before bus mastering has been enabled.
 * After hardware accepts DMA descriptors, a verified stop is mandatory. */
void cleanup_before_start(Device* device) {
    if (device == nullptr) return;
    for (size_t i = 0U; i < DESCRIPTOR_COUNT; ++i) {
        if (device->rx_buffers[i].allocated)
            (void)storage::dma::release_page(&device->rx_buffers[i]);
        if (device->tx_buffers[i].allocated)
            (void)storage::dma::release_page(&device->tx_buffers[i]);
    }
    if (device->rx_ring_page.allocated)
        (void)storage::dma::release_page(&device->rx_ring_page);
    if (device->tx_ring_page.allocated)
        (void)storage::dma::release_page(&device->tx_ring_page);
    auto* space = memory::kernel_virtual_memory::address_space();
    if (space != nullptr) {
        for (size_t i = 0U; i < device->mapped_pages; ++i)
            (void)memory::virtual_memory::unmap_page(space,
                MMIO_VIRTUAL_BASE + i * memory::virtual_memory::PAGE_SIZE);
    }
    *device = {};
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
    if (context != &g_device || !g_device.started)
        return net::Status::NotInitialized;
    if (data == nullptr || count < dma::MIN_ETHERNET_FRAME)
        return net::Status::InvalidArgument;
    if (count > dma::MAX_ETHERNET_FRAME) return net::Status::FrameTooLarge;
    if (__atomic_test_and_set(&g_device.tx_busy, __ATOMIC_ACQUIRE))
        return net::Status::QueueFull;
    const size_t index = g_device.next_tx;
    dma::Descriptor& slot = tx_ring()[index];
    const uint32_t flags = __atomic_load_n(&slot.flags, __ATOMIC_ACQUIRE);
    if ((flags & dma::OWN) != 0U) {
        __atomic_clear(&g_device.tx_busy, __ATOMIC_RELEASE);
        return net::Status::QueueFull;
    }
    auto* bytes = static_cast<uint8_t*>(g_device.tx_buffers[index].virtual_address);
    for (size_t i = 0U; i < count; ++i) bytes[i] = data[i];
    slot.vlan = 0U;
    __atomic_store_n(&slot.flags,
        dma::tx_submit(index + 1U == DESCRIPTOR_COUNT, count), __ATOMIC_RELEASE);
    g_device.next_tx = (index + 1U) % DESCRIPTOR_COUNT;
    write8(g_device, REG_TX_POLL, TX_POLL_NORMAL);
    ++g_device.tx_frames;
    __atomic_clear(&g_device.tx_busy, __ATOMIC_RELEASE);
    return net::Status::Ok;
}

net::Status receive(void* context, uint8_t* out, size_t capacity,
                    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    if (context != &g_device || !g_device.started)
        return net::Status::NotInitialized;
    if (out == nullptr || out_length == nullptr)
        return net::Status::InvalidArgument;
    if (__atomic_test_and_set(&g_device.rx_busy, __ATOMIC_ACQUIRE))
        return net::Status::WouldBlock;
    const size_t index = g_device.next_rx;
    dma::Descriptor& slot = rx_ring()[index];
    const dma::ReceiveInfo info = dma::inspect_rx(
        __atomic_load_n(&slot.flags, __ATOMIC_ACQUIRE), capacity);
    if (info.result == dma::Receive::NotReady) {
        __atomic_clear(&g_device.rx_busy, __ATOMIC_RELEASE);
        return net::Status::WouldBlock;
    }
    if (info.result == dma::Receive::OutputTooSmall) {
        *out_length = info.payload_length;
        __atomic_clear(&g_device.rx_busy, __ATOMIC_RELEASE);
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
    __atomic_clear(&g_device.rx_busy, __ATOMIC_RELEASE);
    return info.result == dma::Receive::Ready
        ? net::Status::Ok : net::Status::InterfaceError;
}

} // namespace

Status initialize() {
    if (g_device.started) {
        g_status = link_up() ? Status::Ok : Status::LinkDown;
        return g_status;
    }
    const pci::Device* found = pci::find(UINT16_C(0x10EC), UINT16_C(0x8168));
    if (found == nullptr || classify_model(found->vendor_id, found->device_id) ==
            Model::Unknown) return g_status = Status::NotFound;
    g_device = {};
    g_device.pci_device = *found;

    bool io_space = false;
    uint64_t bar = pci::bar_address(*found, 2U, &io_space);
    if (bar == 0U || io_space) {
        bar = pci::bar_address(*found, 0U, &io_space);
        if (bar == 0U || io_space) return g_status = Status::InvalidBar;
    }
    if (!map_registers(bar, &g_device))
        return g_status = Status::MappingFailed;

    /* Disable PCI DMA before reset and descriptor setup; use DMA32
     * for conservative RTL8168 revisions. */
    const uint16_t command = pci::read16(*found, 0x04U);
    pci::write16(*found, 0x04U, static_cast<uint16_t>(
        command & ~UINT16_C(0x0004)));
    write16(g_device, REG_INT_MASK, 0U);
    write16(g_device, REG_INT_STATUS, UINT16_MAX);
    write8(g_device, REG_CHIP_CMD, CMD_RESET);
    bool reset_done = false;
    for (uint32_t i = 0U; i < RESET_BUDGET; ++i) {
        if ((read8(g_device, REG_CHIP_CMD) & CMD_RESET) == 0U) {
            reset_done = true;
            break;
        }
        relax();
    }
    if (!reset_done) {
        /* Unverified stop: retain mapping, but no DMA pages have been
         * allocated and bus mastering remains disabled. */
        return g_status = Status::ResetTimedOut;
    }

    for (size_t i = 0U; i < MAC_ADDRESS_LENGTH; ++i)
        g_device.mac.bytes[i] = read8(g_device, REG_MAC + i);
    if (mac_is_zero(g_device.mac) || mac_is_broadcast(g_device.mac) ||
        mac_is_multicast(g_device.mac)) {
        cleanup_before_start(&g_device);
        return g_status = Status::InvalidMac;
    }
    if (!allocate_dma()) {
        cleanup_before_start(&g_device);
        return g_status = Status::DmaAllocationFailed;
    }

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
    pci::enable_bus_mastering(*found);
    write8(g_device, REG_CHIP_CMD, static_cast<uint8_t>(CMD_RX | CMD_TX));

    g_device.interface = {&g_device, transmit, receive, g_device.mac, ETHERNET_MTU};
    g_device.started = true;
    for (uint32_t i = 0U; i < LINK_BUDGET; ++i) {
        if ((read8(g_device, REG_PHY_STATUS) & PHY_LINK_UP) != 0U)
            return g_status = Status::Ok;
        relax();
    }
    return g_status = Status::LinkDown;
}

bool link_up() {
    return g_device.started && g_device.registers != nullptr &&
        (read8(g_device, REG_PHY_STATUS) & PHY_LINK_UP) != 0U;
}
bool ready() { return g_device.started && link_up(); }
NetworkInterface* interface() { return ready() ? &g_device.interface : nullptr; }
Status last_status() { return g_status; }

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
    }
    return "unknown RTL8111/8168 error";
}
} // namespace net::rtl81xx
