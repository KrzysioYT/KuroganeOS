#include "e1000.hpp"

#include "../drivers/pci.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/virtual_memory.hpp"
#include "../storage/dma.hpp"

namespace net::e1000 {
namespace {

constexpr uint16_t INTEL_VENDOR = 0x8086U;
constexpr uint16_t E1000_82540EM = 0x100EU;
constexpr size_t MMIO_BYTES = 128U * 1024U;
constexpr uint64_t MMIO_VIRTUAL_BASE = UINT64_C(0xFFFFB10000000000);
constexpr size_t DESCRIPTOR_COUNT = 8U;
constexpr uint32_t RESET_BUDGET = 1000000U;

constexpr size_t REG_CTRL = 0x0000U;
constexpr size_t REG_STATUS = 0x0008U;
constexpr size_t REG_ICR = 0x00C0U;
constexpr size_t REG_IMC = 0x00D8U;
constexpr size_t REG_RCTL = 0x0100U;
constexpr size_t REG_TCTL = 0x0400U;
constexpr size_t REG_TIPG = 0x0410U;
constexpr size_t REG_RDBAL = 0x2800U;
constexpr size_t REG_RDBAH = 0x2804U;
constexpr size_t REG_RDLEN = 0x2808U;
constexpr size_t REG_RDH = 0x2810U;
constexpr size_t REG_RDT = 0x2818U;
constexpr size_t REG_TDBAL = 0x3800U;
constexpr size_t REG_TDBAH = 0x3804U;
constexpr size_t REG_TDLEN = 0x3808U;
constexpr size_t REG_TDH = 0x3810U;
constexpr size_t REG_TDT = 0x3818U;
constexpr size_t REG_RAL = 0x5400U;
constexpr size_t REG_RAH = 0x5404U;

constexpr uint32_t CTRL_SLU = UINT32_C(1) << 6U;
constexpr uint32_t CTRL_RST = UINT32_C(1) << 26U;
constexpr uint32_t STATUS_LU = UINT32_C(1) << 1U;
constexpr uint32_t RCTL_EN = UINT32_C(1) << 1U;
constexpr uint32_t RCTL_BAM = UINT32_C(1) << 15U;
constexpr uint32_t RCTL_SECRC = UINT32_C(1) << 26U;
constexpr uint32_t TCTL_EN = UINT32_C(1) << 1U;
constexpr uint32_t TCTL_PSP = UINT32_C(1) << 3U;
constexpr uint8_t RX_DD = UINT8_C(1) << 0U;
constexpr uint8_t RX_EOP = UINT8_C(1) << 1U;
constexpr uint8_t TX_EOP = UINT8_C(1) << 0U;
constexpr uint8_t TX_IFCS = UINT8_C(1) << 1U;
constexpr uint8_t TX_RS = UINT8_C(1) << 3U;
constexpr uint8_t TX_DD = UINT8_C(1) << 0U;

struct __attribute__((packed)) ReceiveDescriptor {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
};

struct __attribute__((packed)) TransmitDescriptor {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
};

static_assert(sizeof(ReceiveDescriptor) == 16U, "E1000 RX descriptor ABI");
static_assert(sizeof(TransmitDescriptor) == 16U, "E1000 TX descriptor ABI");
static_assert(
    DESCRIPTOR_COUNT * sizeof(ReceiveDescriptor) <=
        memory::virtual_memory::PAGE_SIZE,
    "RX ring must fit one DMA page");

struct Device {
    pci::Device pci_device;
    volatile uint8_t* registers;
    uintptr_t mapped_base;
    size_t mapped_pages;
    storage::dma::Page rx_ring_page;
    storage::dma::Page tx_ring_page;
    storage::dma::Page rx_buffers[DESCRIPTOR_COUNT];
    storage::dma::Page tx_buffers[DESCRIPTOR_COUNT];
    NetworkInterface interface;
    MacAddress mac;
    size_t next_rx;
    size_t next_tx;
    uint64_t tx_frames;
    uint64_t rx_frames;
    uint64_t drops;
    bool initialized;
};

Device g_device{};
Status g_status = Status::NotInitialized;

void clear_bytes(void* destination, size_t count) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < count; ++index) {
        bytes[index] = 0U;
    }
}

void copy_bytes(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t index = 0U; index < count; ++index) {
        output[index] = input[index];
    }
}

void relax() {
    __asm__ volatile("pause" : : : "memory");
}

void write_barrier() {
    __asm__ volatile("sfence" : : : "memory");
}

void read_barrier() {
    __asm__ volatile("lfence" : : : "memory");
}

volatile uint32_t* register_address(Device& device, size_t offset) {
    return reinterpret_cast<volatile uint32_t*>(device.registers + offset);
}

uint32_t read_register(Device& device, size_t offset) {
    return *register_address(device, offset);
}

void write_register(Device& device, size_t offset, uint32_t value) {
    *register_address(device, offset) = value;
}

bool map_mmio(uint64_t physical, Device* device) {
    if (device == nullptr) return false;
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) return false;
    constexpr uint64_t page_size = memory::virtual_memory::PAGE_SIZE;
    constexpr uint64_t page_mask = page_size - 1U;
    const uint64_t aligned = physical & ~page_mask;
    const uint64_t offset = physical & page_mask;
    const uint64_t span = offset + MMIO_BYTES;
    const size_t page_count = static_cast<size_t>(
        (span + page_mask) / page_size);
    if (page_count == 0U || page_count > 64U) return false;

    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    size_t mapped = 0U;
    for (; mapped < page_count; ++mapped) {
        memory::virtual_memory::Mapping existing{};
        if (memory::virtual_memory::query_page(
                address_space,
                MMIO_VIRTUAL_BASE + mapped * page_size,
                &existing) != memory::virtual_memory::Status::NotMapped ||
            memory::virtual_memory::map_page(
                address_space,
                MMIO_VIRTUAL_BASE + mapped * page_size,
                aligned + mapped * page_size,
                flags) != memory::virtual_memory::Status::Ok) {
            break;
        }
    }
    if (mapped != page_count) {
        while (mapped != 0U) {
            --mapped;
            static_cast<void>(memory::virtual_memory::unmap_page(
                address_space, MMIO_VIRTUAL_BASE + mapped * page_size));
        }
        return false;
    }
    device->mapped_base = MMIO_VIRTUAL_BASE;
    device->mapped_pages = page_count;
    device->registers = reinterpret_cast<volatile uint8_t*>(
        MMIO_VIRTUAL_BASE + offset);
    return true;
}

void release_resources(Device* device) {
    if (device == nullptr) return;
    for (size_t index = 0U; index < DESCRIPTOR_COUNT; ++index) {
        if (device->rx_buffers[index].allocated) {
            static_cast<void>(storage::dma::release_page(
                &device->rx_buffers[index]));
        }
        if (device->tx_buffers[index].allocated) {
            static_cast<void>(storage::dma::release_page(
                &device->tx_buffers[index]));
        }
    }
    if (device->rx_ring_page.allocated) {
        static_cast<void>(storage::dma::release_page(&device->rx_ring_page));
    }
    if (device->tx_ring_page.allocated) {
        static_cast<void>(storage::dma::release_page(&device->tx_ring_page));
    }
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space != nullptr) {
        for (size_t index = 0U; index < device->mapped_pages; ++index) {
            static_cast<void>(memory::virtual_memory::unmap_page(
                address_space,
                device->mapped_base +
                    index * memory::virtual_memory::PAGE_SIZE));
        }
    }
    *device = {};
}

ReceiveDescriptor* rx_ring(Device& device) {
    return static_cast<ReceiveDescriptor*>(
        device.rx_ring_page.virtual_address);
}

TransmitDescriptor* tx_ring(Device& device) {
    return static_cast<TransmitDescriptor*>(
        device.tx_ring_page.virtual_address);
}

net::Status transmit_callback(
    void* context,
    const uint8_t* frame,
    size_t frame_length) {
    auto* device = static_cast<Device*>(context);
    if (device == nullptr || !device->initialized) {
        return net::Status::NotInitialized;
    }
    if (frame == nullptr || frame_length < ETHERNET_HEADER_SIZE ||
        frame_length > ETHERNET_MAX_FRAME_SIZE) {
        return frame_length > ETHERNET_MAX_FRAME_SIZE
            ? net::Status::FrameTooLarge
            : net::Status::InvalidArgument;
    }

    TransmitDescriptor& descriptor = tx_ring(*device)[device->next_tx];
    read_barrier();
    if ((descriptor.status & TX_DD) == 0U) {
        // All DMA pages are owned per descriptor. If the producer catches the
        // hardware, report normal ring backpressure instead of converting a
        // busy descriptor into a fatal interface failure. The TCP layer can
        // then retry the exact same sequence number after making progress.
        return net::Status::QueueFull;
    }

    copy_bytes(
        device->tx_buffers[device->next_tx].virtual_address,
        frame,
        frame_length);
    descriptor.length = static_cast<uint16_t>(frame_length);
    descriptor.checksum_offset = 0U;
    descriptor.command = TX_EOP | TX_IFCS | TX_RS;
    descriptor.status = 0U;
    descriptor.checksum_start = 0U;
    descriptor.special = 0U;
    write_barrier();

    device->next_tx = (device->next_tx + 1U) % DESCRIPTOR_COUNT;
    write_register(*device, REG_TDT, static_cast<uint32_t>(device->next_tx));

    // Do not synchronously spin for TX_DD here. The descriptor owns a dedicated
    // DMA page until hardware sets DD, and reuse is already guarded above.
    // Returning immediately lets TLS enqueue a normal burst without stalling
    // the whole browser process on VirtualBox device scheduling latency.
    ++device->tx_frames;
    return net::Status::Ok;
}

net::Status receive_callback(
    void* context,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    auto* device = static_cast<Device*>(context);
    if (device == nullptr || !device->initialized) {
        return net::Status::NotInitialized;
    }
    if (output == nullptr || out_length == nullptr) {
        return net::Status::InvalidArgument;
    }
    ReceiveDescriptor& descriptor = rx_ring(*device)[device->next_rx];
    read_barrier();
    if ((descriptor.status & RX_DD) == 0U) {
        return net::Status::WouldBlock;
    }
    const size_t length = descriptor.length;
    if ((descriptor.status & RX_EOP) == 0U || descriptor.errors != 0U ||
        length < ETHERNET_HEADER_SIZE || length > ETHERNET_MAX_FRAME_SIZE) {
        ++device->drops;
        descriptor.status = 0U;
        write_barrier();
        write_register(
            *device, REG_RDT, static_cast<uint32_t>(device->next_rx));
        device->next_rx = (device->next_rx + 1U) % DESCRIPTOR_COUNT;
        return net::Status::InterfaceError;
    }
    *out_length = length;
    if (output_capacity < length) {
        return net::Status::BufferTooSmall;
    }
    copy_bytes(
        output,
        device->rx_buffers[device->next_rx].virtual_address,
        length);
    descriptor.status = 0U;
    write_barrier();
    write_register(*device, REG_RDT, static_cast<uint32_t>(device->next_rx));
    device->next_rx = (device->next_rx + 1U) % DESCRIPTOR_COUNT;
    ++device->rx_frames;
    return net::Status::Ok;
}

bool valid_mac(const MacAddress& mac) {
    return !mac_is_zero(mac) && !mac_is_broadcast(mac) &&
        !mac_is_multicast(mac);
}

bool allocate_dma(Device* device) {
    if (storage::dma::allocate_page(true, &device->rx_ring_page) !=
            storage::dma::Status::Ok ||
        storage::dma::allocate_page(true, &device->tx_ring_page) !=
            storage::dma::Status::Ok) {
        return false;
    }
    clear_bytes(
        device->rx_ring_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    clear_bytes(
        device->tx_ring_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    for (size_t index = 0U; index < DESCRIPTOR_COUNT; ++index) {
        if (storage::dma::allocate_page(true, &device->rx_buffers[index]) !=
                storage::dma::Status::Ok ||
            storage::dma::allocate_page(true, &device->tx_buffers[index]) !=
                storage::dma::Status::Ok) {
            return false;
        }
        rx_ring(*device)[index].address =
            device->rx_buffers[index].physical_address;
        tx_ring(*device)[index].address =
            device->tx_buffers[index].physical_address;
        tx_ring(*device)[index].status = TX_DD;
    }
    return true;
}

} // namespace

Status initialize() {
    if (g_device.initialized) {
        return Status::AlreadyInitialized;
    }
    const pci::Device* pci_device = pci::find(INTEL_VENDOR, E1000_82540EM);
    if (pci_device == nullptr) {
        g_status = Status::NotFound;
        return g_status;
    }
    g_device = {};
    g_device.pci_device = *pci_device;
    bool io_space = false;
    const uint64_t bar = pci::bar_address(*pci_device, 0U, &io_space);
    if (bar == 0U || io_space) {
        g_status = Status::InvalidBar;
        return g_status;
    }
    if (!map_mmio(bar, &g_device)) {
        g_status = Status::MmioMapFailed;
        return g_status;
    }
    pci::enable_bus_mastering(*pci_device);
    write_register(g_device, REG_IMC, UINT32_MAX);
    static_cast<void>(read_register(g_device, REG_ICR));
    write_register(
        g_device,
        REG_CTRL,
        read_register(g_device, REG_CTRL) | CTRL_RST);
    bool reset_complete = false;
    for (uint32_t attempt = 0U; attempt < RESET_BUDGET; ++attempt) {
        if ((read_register(g_device, REG_CTRL) & CTRL_RST) == 0U) {
            reset_complete = true;
            break;
        }
        relax();
    }
    if (!reset_complete) {
        release_resources(&g_device);
        g_status = Status::ResetTimedOut;
        return g_status;
    }
    write_register(g_device, REG_IMC, UINT32_MAX);
    static_cast<void>(read_register(g_device, REG_ICR));

    const uint32_t ral = read_register(g_device, REG_RAL);
    const uint32_t rah = read_register(g_device, REG_RAH);
    g_device.mac.bytes[0] = static_cast<uint8_t>(ral);
    g_device.mac.bytes[1] = static_cast<uint8_t>(ral >> 8U);
    g_device.mac.bytes[2] = static_cast<uint8_t>(ral >> 16U);
    g_device.mac.bytes[3] = static_cast<uint8_t>(ral >> 24U);
    g_device.mac.bytes[4] = static_cast<uint8_t>(rah);
    g_device.mac.bytes[5] = static_cast<uint8_t>(rah >> 8U);
    if (!valid_mac(g_device.mac)) {
        release_resources(&g_device);
        g_status = Status::InvalidMac;
        return g_status;
    }
    if (!allocate_dma(&g_device)) {
        release_resources(&g_device);
        g_status = Status::DmaAllocationFailed;
        return g_status;
    }

    write_register(g_device, REG_RDBAL,
        static_cast<uint32_t>(g_device.rx_ring_page.physical_address));
    write_register(g_device, REG_RDBAH,
        static_cast<uint32_t>(g_device.rx_ring_page.physical_address >> 32U));
    write_register(g_device, REG_RDLEN,
        static_cast<uint32_t>(DESCRIPTOR_COUNT * sizeof(ReceiveDescriptor)));
    write_register(g_device, REG_RDH, 0U);
    write_register(g_device, REG_RDT,
        static_cast<uint32_t>(DESCRIPTOR_COUNT - 1U));

    write_register(g_device, REG_TDBAL,
        static_cast<uint32_t>(g_device.tx_ring_page.physical_address));
    write_register(g_device, REG_TDBAH,
        static_cast<uint32_t>(g_device.tx_ring_page.physical_address >> 32U));
    write_register(g_device, REG_TDLEN,
        static_cast<uint32_t>(DESCRIPTOR_COUNT * sizeof(TransmitDescriptor)));
    write_register(g_device, REG_TDH, 0U);
    write_register(g_device, REG_TDT, 0U);
    write_register(g_device, REG_TIPG,
        10U | (8U << 10U) | (6U << 20U));
    write_register(g_device, REG_TCTL,
        TCTL_EN | TCTL_PSP | (0x10U << 4U) | (0x40U << 12U));
    write_register(g_device, REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);
    write_register(
        g_device,
        REG_CTRL,
        read_register(g_device, REG_CTRL) | CTRL_SLU);

    g_device.interface = {
        &g_device,
        transmit_callback,
        receive_callback,
        g_device.mac,
        ETHERNET_MTU,
    };
    g_device.initialized = true;
    bool link_up = false;
    for (uint32_t attempt = 0U; attempt < RESET_BUDGET; ++attempt) {
        if ((read_register(g_device, REG_STATUS) & STATUS_LU) != 0U) {
            link_up = true;
            break;
        }
        relax();
    }
    if (!link_up) {
        g_status = Status::LinkDown;
        return g_status;
    }
    g_status = Status::Ok;
    return g_status;
}

bool ready() {
    return g_device.initialized && g_status == Status::Ok;
}

bool link_up() {
    return ready() && (read_register(g_device, REG_STATUS) & STATUS_LU) != 0U;
}

NetworkInterface* interface() {
    return ready() ? &g_device.interface : nullptr;
}

const MacAddress* hardware_address() {
    return ready() ? &g_device.mac : nullptr;
}

uint64_t transmitted_frames() { return g_device.tx_frames; }
uint64_t received_frames() { return g_device.rx_frames; }
uint64_t dropped_frames() { return g_device.drops; }

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::NotInitialized: return "not initialized";
        case Status::NotFound: return "Intel E1000 was not found";
        case Status::UnsupportedDevice: return "unsupported E1000 device";
        case Status::InvalidBar: return "invalid E1000 MMIO BAR";
        case Status::MmioMapFailed: return "E1000 MMIO mapping failed";
        case Status::ResetTimedOut: return "E1000 reset timed out";
        case Status::InvalidMac: return "E1000 has no valid unicast MAC";
        case Status::DmaAllocationFailed: return "E1000 DMA allocation failed";
        case Status::LinkDown: return "E1000 link is down";
        case Status::TransmitTimedOut: return "E1000 transmit timed out";
        case Status::DeviceError: return "E1000 device error";
    }
    return "unknown E1000 status";
}

} // namespace net::e1000
