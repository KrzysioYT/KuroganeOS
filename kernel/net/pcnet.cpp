#include "pcnet.hpp"

#include "../arch/x86_64/io.hpp"
#include "../drivers/pci.hpp"
#include "../storage/dma.hpp"

namespace net::pcnet {
namespace {

constexpr uint16_t AMD_VENDOR = UINT16_C(0x1022);
constexpr uint16_t PCNET_DEVICE = UINT16_C(0x2000);
constexpr size_t DESCRIPTOR_COUNT = 8U;
constexpr uint16_t IO_RDP = UINT16_C(0x10);
constexpr uint16_t IO_RAP = UINT16_C(0x12);
constexpr uint16_t IO_RESET = UINT16_C(0x14);
constexpr uint16_t IO_BDP = UINT16_C(0x16);
constexpr uint16_t CSR0_INIT = UINT16_C(0x0001);
constexpr uint16_t CSR0_STRT = UINT16_C(0x0002);
constexpr uint16_t CSR0_STOP = UINT16_C(0x0004);
constexpr uint16_t CSR0_TDMD = UINT16_C(0x0008);
constexpr uint16_t CSR0_IDON = UINT16_C(0x0100);
constexpr uint16_t CSR0_ERR = UINT16_C(0x8000);
constexpr uint16_t DESC_OWN = UINT16_C(0x8000);
constexpr uint16_t DESC_ERR = UINT16_C(0x4000);
constexpr uint16_t DESC_STP = UINT16_C(0x0200);
constexpr uint16_t DESC_ENP = UINT16_C(0x0100);
constexpr uint32_t INIT_BUDGET = UINT32_C(1000000);
constexpr uint32_t TX_BUDGET = UINT32_C(1000000);

struct __attribute__((packed)) InitBlock {
    uint16_t mode;
    uint16_t ring_lengths;
    uint8_t mac[6];
    uint16_t reserved;
    uint32_t logical_filter_low;
    uint32_t logical_filter_high;
    uint32_t rx_ring;
    uint32_t tx_ring;
};

struct __attribute__((packed)) Descriptor {
    uint32_t address;
    int16_t buffer_length;
    uint16_t status;
    uint32_t message_length;
    uint32_t reserved;
};

static_assert(sizeof(InitBlock) == 28U, "PCnet init block ABI");
static_assert(sizeof(Descriptor) == 16U, "PCnet descriptor ABI");
static_assert(DESCRIPTOR_COUNT * sizeof(Descriptor) <= 4096U,
    "PCnet descriptor ring must fit one DMA page");

struct Device {
    pci::Device pci_device;
    uint16_t io_base;
    storage::dma::Page control_page;
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
    for (size_t index = 0U; index < count; ++index) bytes[index] = 0U;
}

void copy_bytes(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t index = 0U; index < count; ++index) output[index] = input[index];
}

void relax() { arch::pause(); }
void barrier() { __asm__ volatile("mfence" : : : "memory"); }

void select_register(Device& device, uint16_t index) {
    arch::out16(static_cast<uint16_t>(device.io_base + IO_RAP), index);
}

uint16_t read_csr(Device& device, uint16_t index) {
    select_register(device, index);
    return arch::in16(static_cast<uint16_t>(device.io_base + IO_RDP));
}

void write_csr(Device& device, uint16_t index, uint16_t value) {
    select_register(device, index);
    arch::out16(static_cast<uint16_t>(device.io_base + IO_RDP), value);
}

void write_bcr(Device& device, uint16_t index, uint16_t value) {
    select_register(device, index);
    arch::out16(static_cast<uint16_t>(device.io_base + IO_BDP), value);
}

InitBlock* init_block(Device& device) {
    return static_cast<InitBlock*>(device.control_page.virtual_address);
}

Descriptor* rx_ring(Device& device) {
    return reinterpret_cast<Descriptor*>(
        static_cast<uint8_t*>(device.control_page.virtual_address) + 64U);
}

Descriptor* tx_ring(Device& device) {
    return reinterpret_cast<Descriptor*>(
        static_cast<uint8_t*>(device.control_page.virtual_address) + 256U);
}

bool valid_mac(const MacAddress& mac) {
    return !mac_is_zero(mac) && !mac_is_broadcast(mac) && !mac_is_multicast(mac);
}

void release_resources(Device* device) {
    if (device == nullptr) return;
    for (size_t index = 0U; index < DESCRIPTOR_COUNT; ++index) {
        if (device->rx_buffers[index].allocated) {
            static_cast<void>(storage::dma::release_page(&device->rx_buffers[index]));
        }
        if (device->tx_buffers[index].allocated) {
            static_cast<void>(storage::dma::release_page(&device->tx_buffers[index]));
        }
    }
    if (device->control_page.allocated) {
        static_cast<void>(storage::dma::release_page(&device->control_page));
    }
    *device = {};
}

bool allocate_dma(Device* device) {
    if (device == nullptr) return false;
    if (storage::dma::allocate_page(false, &device->control_page) !=
        storage::dma::Status::Ok) return false;
    clear_bytes(device->control_page.virtual_address, 4096U);

    for (size_t index = 0U; index < DESCRIPTOR_COUNT; ++index) {
        if (storage::dma::allocate_page(false, &device->rx_buffers[index]) !=
                storage::dma::Status::Ok ||
            storage::dma::allocate_page(false, &device->tx_buffers[index]) !=
                storage::dma::Status::Ok) {
            return false;
        }
        if (device->rx_buffers[index].physical_address >= UINT64_C(0x100000000) ||
            device->tx_buffers[index].physical_address >= UINT64_C(0x100000000)) {
            return false;
        }
        Descriptor& rx = rx_ring(*device)[index];
        rx.address = static_cast<uint32_t>(device->rx_buffers[index].physical_address);
        rx.buffer_length = static_cast<int16_t>(-2048);
        rx.status = DESC_OWN;
        rx.message_length = 0U;
        rx.reserved = 0U;

        Descriptor& tx = tx_ring(*device)[index];
        tx.address = static_cast<uint32_t>(device->tx_buffers[index].physical_address);
        tx.buffer_length = 0;
        tx.status = 0U;
        tx.message_length = 0U;
        tx.reserved = 0U;
    }
    return true;
}

net::Status transmit_callback(
    void* context,
    const uint8_t* frame,
    size_t frame_length) {
    auto* device = static_cast<Device*>(context);
    if (device == nullptr || !device->initialized) return net::Status::NotInitialized;
    if (frame == nullptr || frame_length < ETHERNET_HEADER_SIZE ||
        frame_length > ETHERNET_MAX_FRAME_SIZE || frame_length > 2048U) {
        return frame_length > ETHERNET_MAX_FRAME_SIZE
            ? net::Status::FrameTooLarge : net::Status::InvalidArgument;
    }

    Descriptor& descriptor = tx_ring(*device)[device->next_tx];
    barrier();
    if ((descriptor.status & DESC_OWN) != 0U) {
        ++device->drops;
        return net::Status::WouldBlock;
    }
    copy_bytes(device->tx_buffers[device->next_tx].virtual_address, frame, frame_length);
    descriptor.buffer_length = static_cast<int16_t>(-static_cast<int32_t>(frame_length));
    descriptor.message_length = 0U;
    descriptor.reserved = 0U;
    descriptor.status = DESC_OWN | DESC_STP | DESC_ENP;
    barrier();
    write_csr(*device, 0U, static_cast<uint16_t>(CSR0_STRT | CSR0_TDMD));

    for (uint32_t attempt = 0U; attempt < TX_BUDGET; ++attempt) {
        barrier();
        if ((descriptor.status & DESC_OWN) == 0U) {
            if ((descriptor.status & DESC_ERR) != 0U) {
                ++device->drops;
                return net::Status::InterfaceError;
            }
            ++device->tx_frames;
            device->next_tx = (device->next_tx + 1U) % DESCRIPTOR_COUNT;
            return net::Status::Ok;
        }
        relax();
    }
    ++device->drops;
    return net::Status::WouldBlock;
}

net::Status receive_callback(
    void* context,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    auto* device = static_cast<Device*>(context);
    if (device == nullptr || !device->initialized) return net::Status::NotInitialized;
    if (output == nullptr || out_length == nullptr) return net::Status::InvalidArgument;

    Descriptor& descriptor = rx_ring(*device)[device->next_rx];
    barrier();
    if ((descriptor.status & DESC_OWN) != 0U) return net::Status::WouldBlock;

    const uint16_t descriptor_status = descriptor.status;
    size_t length = static_cast<size_t>(descriptor.message_length & UINT32_C(0x0FFF));
    if (length >= 4U) length -= 4U; /* PCnet includes Ethernet FCS. */
    if ((descriptor_status & (DESC_STP | DESC_ENP)) != (DESC_STP | DESC_ENP) ||
        (descriptor_status & DESC_ERR) != 0U ||
        length < ETHERNET_HEADER_SIZE || length > ETHERNET_MAX_FRAME_SIZE) {
        ++device->drops;
        descriptor.message_length = 0U;
        descriptor.status = DESC_OWN;
        barrier();
        device->next_rx = (device->next_rx + 1U) % DESCRIPTOR_COUNT;
        return net::Status::InterfaceError;
    }
    if (output_capacity < length) {
        descriptor.message_length = 0U;
        descriptor.status = DESC_OWN;
        barrier();
        device->next_rx = (device->next_rx + 1U) % DESCRIPTOR_COUNT;
        ++device->drops;
        return net::Status::BufferTooSmall;
    }
    copy_bytes(output, device->rx_buffers[device->next_rx].virtual_address, length);
    *out_length = length;
    descriptor.message_length = 0U;
    descriptor.status = DESC_OWN;
    barrier();
    device->next_rx = (device->next_rx + 1U) % DESCRIPTOR_COUNT;
    ++device->rx_frames;
    return net::Status::Ok;
}

} // namespace

Status initialize() {
    if (g_device.initialized) return Status::AlreadyInitialized;
    const pci::Device* pci_device = pci::find(AMD_VENDOR, PCNET_DEVICE);
    if (pci_device == nullptr) {
        g_status = Status::NotFound;
        return g_status;
    }

    g_device = {};
    g_device.pci_device = *pci_device;
    bool io_space = false;
    const uint64_t bar = pci::bar_address(*pci_device, 0U, &io_space);
    if (bar == 0U || !io_space || bar > UINT16_MAX - UINT16_C(0x20)) {
        g_status = Status::InvalidBar;
        return g_status;
    }
    g_device.io_base = static_cast<uint16_t>(bar);
    pci::enable_bus_mastering(*pci_device);

    /* Reset, then select 32-bit software style through BCR20. */
    static_cast<void>(arch::in16(static_cast<uint16_t>(g_device.io_base + IO_RESET)));
    arch::io_wait();
    write_csr(g_device, 0U, CSR0_STOP);
    write_bcr(g_device, 20U, 2U);

    for (size_t index = 0U; index < MAC_ADDRESS_LENGTH; ++index) {
        g_device.mac.bytes[index] = arch::in8(
            static_cast<uint16_t>(g_device.io_base + static_cast<uint16_t>(index)));
    }
    if (!valid_mac(g_device.mac)) {
        g_status = Status::InvalidMac;
        return g_status;
    }
    if (!allocate_dma(&g_device)) {
        release_resources(&g_device);
        g_status = Status::DmaAllocationFailed;
        return g_status;
    }

    InitBlock* block = init_block(g_device);
    block->mode = 0U;
    /* 2^3 = 8 RX and 8 TX descriptors. RLEN bits 4..7, TLEN bits 12..15. */
    block->ring_lengths = static_cast<uint16_t>((3U << 4U) | (3U << 12U));
    for (size_t index = 0U; index < MAC_ADDRESS_LENGTH; ++index) {
        block->mac[index] = g_device.mac.bytes[index];
    }
    block->reserved = 0U;
    block->logical_filter_low = 0U;
    block->logical_filter_high = 0U;
    block->rx_ring = static_cast<uint32_t>(g_device.control_page.physical_address + 64U);
    block->tx_ring = static_cast<uint32_t>(g_device.control_page.physical_address + 256U);
    barrier();

    const uint32_t init_address = static_cast<uint32_t>(g_device.control_page.physical_address);
    write_csr(g_device, 1U, static_cast<uint16_t>(init_address & UINT32_C(0xFFFF)));
    write_csr(g_device, 2U, static_cast<uint16_t>(init_address >> 16U));
    write_csr(g_device, 3U, UINT16_C(0xFFFF)); /* polling driver: mask interrupts */
    write_csr(g_device, 0U, CSR0_INIT);

    bool initialized = false;
    for (uint32_t attempt = 0U; attempt < INIT_BUDGET; ++attempt) {
        const uint16_t csr0 = read_csr(g_device, 0U);
        if ((csr0 & CSR0_ERR) != 0U) break;
        if ((csr0 & CSR0_IDON) != 0U) {
            initialized = true;
            break;
        }
        relax();
    }
    if (!initialized) {
        release_resources(&g_device);
        g_status = Status::InitializationTimedOut;
        return g_status;
    }

    write_csr(g_device, 0U, static_cast<uint16_t>(CSR0_IDON | CSR0_STRT));
    g_device.interface = {
        &g_device,
        transmit_callback,
        receive_callback,
        g_device.mac,
        ETHERNET_MTU,
    };
    g_device.initialized = true;
    g_status = Status::Ok;
    return g_status;
}

bool ready() { return g_device.initialized && g_status == Status::Ok; }
NetworkInterface* interface() { return ready() ? &g_device.interface : nullptr; }
const MacAddress* hardware_address() { return ready() ? &g_device.mac : nullptr; }
uint64_t transmitted_frames() { return g_device.tx_frames; }
uint64_t received_frames() { return g_device.rx_frames; }
uint64_t dropped_frames() { return g_device.drops; }

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::NotInitialized: return "not initialized";
        case Status::NotFound: return "AMD PCnet was not found";
        case Status::InvalidBar: return "invalid PCnet I/O BAR";
        case Status::ResetTimedOut: return "PCnet reset timed out";
        case Status::InvalidMac: return "PCnet has no valid unicast MAC";
        case Status::DmaAllocationFailed: return "PCnet DMA32 allocation failed";
        case Status::InitializationTimedOut: return "PCnet initialization timed out";
        case Status::LinkUnavailable: return "PCnet link unavailable";
        case Status::DeviceError: return "PCnet device error";
    }
    return "unknown PCnet status";
}

} // namespace net::pcnet
