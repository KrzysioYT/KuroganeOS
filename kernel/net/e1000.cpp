#include "e1000.hpp"

#include "../drivers/pci.hpp"
#include "../drivers/pci_msi.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/virtual_memory.hpp"
#include "../storage/dma.hpp"

namespace net::e1000 {
namespace {

constexpr size_t MMIO_BYTES = 128U * 1024U;
constexpr uint64_t MMIO_VIRTUAL_BASE = UINT64_C(0xFFFFB10000000000);
constexpr size_t DESCRIPTOR_COUNT = 8U;
constexpr uint32_t RESET_BUDGET = 1000000U;
constexpr uint32_t MASTER_DRAIN_BUDGET = 1000000U;
constexpr uint8_t PCI_COMMAND = 0x04U;
constexpr uint16_t PCI_MEMORY_ENABLE = UINT16_C(1) << 1U;
constexpr uint16_t PCI_BUS_MASTER_ENABLE = UINT16_C(1) << 2U;

constexpr size_t REG_CTRL = 0x0000U;
constexpr size_t REG_STATUS = 0x0008U;
constexpr size_t REG_ICR = 0x00C0U;
constexpr size_t REG_ICS = 0x00C8U;
constexpr size_t REG_IMS = 0x00D0U;
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

constexpr uint32_t CTRL_GIO_MASTER_DISABLE = UINT32_C(1) << 2U;
constexpr uint32_t CTRL_SLU = UINT32_C(1) << 6U;
constexpr uint32_t CTRL_RST = UINT32_C(1) << 26U;
constexpr uint32_t STATUS_LU = UINT32_C(1) << 1U;
constexpr uint32_t STATUS_GIO_MASTER_ENABLE = UINT32_C(1) << 19U;
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
constexpr uint32_t INTERRUPT_LINK_STATUS_CHANGE = UINT32_C(1) << 2U;

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
    Model model;
    volatile uint8_t* registers;
    uint64_t bar_physical;
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
    pci::msi::Route msi_route;
    uint64_t interrupt_count;
    uint32_t last_interrupt_causes;
    bool msi_active;
    bool initialized;
    bool quiescing;
    // A failed retirement retains the binding until a verified recovery path
    // exists. initialize() must not overwrite ownership or map a new endpoint.
    bool resources_retained;
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

void interrupt_handler(arch::x86_64::interrupts::InterruptFrame&) {
    if (__atomic_load_n(&g_device.quiescing, __ATOMIC_ACQUIRE) ||
        g_device.registers == nullptr) return;
    const uint32_t causes = read_register(g_device, REG_ICR);
    if (causes == 0U) return;
    __atomic_fetch_or(
        &g_device.last_interrupt_causes, causes, __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &g_device.interrupt_count, UINT64_C(1), __ATOMIC_RELEASE);
}

bool unmap_mmio(Device& device) {
    if (device.mapped_pages == 0U) return true;
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) return false;
    while (device.mapped_pages != 0U) {
        if (memory::virtual_memory::unmap_page(address_space,
                device.mapped_base + (device.mapped_pages - 1U) *
                    memory::virtual_memory::PAGE_SIZE) !=
                memory::virtual_memory::Status::Ok) return false;
        --device.mapped_pages;
    }
    device.registers = nullptr;
    return true;
}

bool map_mmio(uint64_t physical, Device* device) {
    if (device == nullptr || physical == 0U ||
        physical > UINT64_MAX - (MMIO_BYTES - 1U)) return false;
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
    device->mapped_base = MMIO_VIRTUAL_BASE;
    size_t& mapped = device->mapped_pages;
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
        device->resources_retained = !unmap_mmio(*device);
        return false;
    }
    device->bar_physical = physical;
    device->registers = reinterpret_cast<volatile uint8_t*>(
        MMIO_VIRTUAL_BASE + offset);
    return true;
}

bool pci_binding_matches(const Device& device) {
    const uint32_t expected =
        (static_cast<uint32_t>(device.pci_device.device_id) << 16U) |
        device.pci_device.vendor_id;
    const auto& pci_device = device.pci_device;
    if (pci::read32(pci_device.address, 0x00U) != expected ||
        pci::read8(pci_device.address, 0x08U) != pci_device.revision) return false;
    bool is_io = false;
    return pci::bar_address(pci_device, 0U, &is_io) == device.bar_physical &&
        !is_io && device.bar_physical != 0U;
}

// I82574L datasheet 3.1.3.10 and 5.4.4.2.3 require the device-side
// master-disable handshake before clearing PCI Command.Bus Master Enable.
bool drain_e1000e_dma(Device& device) {
    if (device.registers == nullptr || !pci_binding_matches(device)) return false;
    const uint16_t command = pci::read16(device.pci_device.address, PCI_COMMAND);
    if (command == UINT16_MAX || (command & PCI_MEMORY_ENABLE) == 0U)
        return false;

    const uint32_t control = read_register(device, REG_CTRL);
    const uint32_t receive = read_register(device, REG_RCTL);
    const uint32_t transmit = read_register(device, REG_TCTL);
    if (control == UINT32_MAX || (control & CTRL_RST) != 0U ||
        receive == UINT32_MAX || transmit == UINT32_MAX) return false;

    write_register(device, REG_IMC, UINT32_MAX);
    static_cast<void>(read_register(device, REG_ICR));
    write_register(device, REG_RCTL, receive & ~RCTL_EN);
    write_register(device, REG_TCTL, transmit & ~TCTL_EN);
    if ((read_register(device, REG_RCTL) & RCTL_EN) != 0U ||
        (read_register(device, REG_TCTL) & TCTL_EN) != 0U) return false;
    write_register(device, REG_CTRL, control | CTRL_GIO_MASTER_DISABLE);
    const uint32_t disabled = read_register(device, REG_CTRL);
    if (disabled == UINT32_MAX || (disabled & CTRL_GIO_MASTER_DISABLE) == 0U)
        return false;

    for (uint32_t attempt = 0U; attempt < MASTER_DRAIN_BUDGET; ++attempt) {
        const uint32_t status = read_register(device, REG_STATUS);
        if (status == UINT32_MAX) return false;
        if ((status & STATUS_GIO_MASTER_ENABLE) == 0U)
            return pci_binding_matches(device);
        relax();
    }
    return false;
}

bool disable_pci_bus_master(const Device& device) {
    if (!pci_binding_matches(device)) return false;
    const auto& address = device.pci_device.address;
    const uint16_t command = pci::read16(address, PCI_COMMAND);
    if (command == UINT16_MAX || (command & PCI_MEMORY_ENABLE) == 0U)
        return false;
    pci::write16(address, PCI_COMMAND,
        static_cast<uint16_t>(command & ~PCI_BUS_MASTER_ENABLE));
    const uint16_t applied = pci::read16(address, PCI_COMMAND);
    return applied != UINT16_MAX && (applied & PCI_BUS_MASTER_ENABLE) == 0U &&
        pci_binding_matches(device);
}

bool release_page(storage::dma::Page& page) {
    return !page.allocated ||
        storage::dma::release_page(&page) == storage::dma::Status::Ok;
}

Status release_resources(Device& device) {
    // Called only during initialize(), before interface publication.
    // Block software tail updates and MMIO in the interrupt handler even
    // when retirement must retain DMA.
    device.initialized = false;
    __atomic_store_n(&device.quiescing, true, __ATOMIC_RELEASE);
    if (device.model == Model::I82574L) {
        if (!drain_e1000e_dma(device)) return Status::DmaQuiesceFailed;
    } else if (device.registers != nullptr) {
        write_register(device, REG_IMC, UINT32_MAX);
        static_cast<void>(read_register(device, REG_ICR));
    }
    if (device.msi_route.active) {
        if (pci::msi::disable(&device.msi_route) != pci::msi::Status::Ok)
            return Status::ResourceReleaseFailed;
        device.msi_active = false;
    }
    // MSI teardown restores its saved Command word. Drop Bus Master after it
    // finishes so that restoration cannot turn DMA back on before page release.
    if (device.model == Model::I82574L && !disable_pci_bus_master(device))
        return Status::DmaQuiesceFailed;
    for (size_t index = 0U; index < DESCRIPTOR_COUNT; ++index) {
        if (!release_page(device.rx_buffers[index]) ||
            !release_page(device.tx_buffers[index]))
            return Status::ResourceReleaseFailed;
    }
    if (!release_page(device.rx_ring_page) || !release_page(device.tx_ring_page) ||
        !unmap_mmio(device)) return Status::ResourceReleaseFailed;
    device = {};
    return Status::Ok;
}

Status fail_initialize(Status cause) {
    const Status retirement = release_resources(g_device);
    g_device.resources_retained = retirement != Status::Ok;
    g_status = g_device.resources_retained ? retirement : cause;
    return g_status;
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

const pci::Device* find_supported_device(Model* out_model) {
    if (out_model != nullptr) *out_model = Model::Unknown;
    for (size_t index = 0U; index < pci::device_count(); ++index) {
        const pci::Device* device = pci::device_at(index);
        if (device == nullptr) continue;
        const Model candidate =
            classify_model(device->vendor_id, device->device_id);
        if (candidate == Model::Unknown) continue;
        if (out_model != nullptr) *out_model = candidate;
        return device;
    }
    return nullptr;
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
    if (g_device.resources_retained) return g_status;
    if (g_device.initialized) {
        return Status::AlreadyInitialized;
    }
    Model detected_model = Model::Unknown;
    const pci::Device* pci_device = find_supported_device(&detected_model);
    if (pci_device == nullptr || detected_model == Model::Unknown) {
        g_status = Status::NotFound;
        return g_status;
    }
    g_device = {};
    g_device.pci_device = *pci_device;
    g_device.model = detected_model;
    bool io_space = false;
    const uint64_t bar = pci::bar_address(*pci_device, 0U, &io_space);
    if (bar == 0U || io_space) {
        g_status = Status::InvalidBar;
        return g_status;
    }
    if (!map_mmio(bar, &g_device)) {
        g_status = g_device.resources_retained
            ? Status::ResourceReleaseFailed : Status::MmioMapFailed;
        return g_status;
    }
    if (detected_model == Model::I82574L && !pci_binding_matches(g_device)) {
        g_device.resources_retained = !unmap_mmio(g_device);
        g_status = g_device.resources_retained
            ? Status::ResourceReleaseFailed : Status::InvalidBar;
        return g_status;
    }
    pci::enable_bus_mastering(*pci_device);
    if (detected_model == Model::I82574L) {
        const uint16_t command = pci::read16(pci_device->address, PCI_COMMAND);
        constexpr uint16_t required = PCI_MEMORY_ENABLE | PCI_BUS_MASTER_ENABLE;
        if (command == UINT16_MAX || (command & required) != required ||
            !pci_binding_matches(g_device)) return fail_initialize(Status::DeviceError);
    }
    const uint32_t control = read_register(g_device, REG_CTRL);
    if (detected_model == Model::I82574L && control == UINT32_MAX)
        return fail_initialize(Status::DeviceError);
    write_register(g_device, REG_IMC, UINT32_MAX);
    static_cast<void>(read_register(g_device, REG_ICR));
    write_register(g_device, REG_CTRL, control | CTRL_RST);
    bool reset_complete = false;
    for (uint32_t attempt = 0U; attempt < RESET_BUDGET; ++attempt) {
        if ((read_register(g_device, REG_CTRL) & CTRL_RST) == 0U) {
            reset_complete = true;
            break;
        }
        relax();
    }
    if (!reset_complete) return fail_initialize(Status::ResetTimedOut);
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
    if (!valid_mac(g_device.mac)) return fail_initialize(Status::InvalidMac);
    if (!allocate_dma(&g_device)) return fail_initialize(Status::DmaAllocationFailed);

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

    const pci::msi::Status msi_status = pci::msi::enable(
        g_device.pci_device, interrupt_handler, &g_device.msi_route);
    if (msi_status == pci::msi::Status::Ok) {
        g_device.msi_active = true;
        write_register(g_device, REG_IMC, UINT32_MAX);
        static_cast<void>(read_register(g_device, REG_ICR));
    }

    g_device.interface = {
        &g_device,
        transmit_callback,
        receive_callback,
        g_device.mac,
        ETHERNET_MTU,
    };
    bool link_up = false;
    for (uint32_t attempt = 0U; attempt < RESET_BUDGET; ++attempt) {
        const uint32_t status = read_register(g_device, REG_STATUS);
        if (detected_model == Model::I82574L && status == UINT32_MAX)
            return fail_initialize(Status::DeviceError);
        if ((status & STATUS_LU) != 0U) {
            link_up = true;
            break;
        }
        relax();
    }
    g_device.initialized = true;
    g_status = link_up ? Status::Ok : Status::LinkDown;
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

bool msi_configured() {
    return g_device.initialized && g_device.msi_active;
}

Model model() {
    return g_device.model;
}

uint16_t pci_device_id() {
    return g_device.pci_device.device_id;
}

const char* model_name(Model model) {
    switch (model) {
        case Model::I82540EM: return "Intel 82540EM E1000";
        case Model::I82574L: return "Intel 82574L E1000e";
        case Model::Unknown: return "unsupported Intel GbE";
    }
    return "unsupported Intel GbE";
}

bool qualify_msi_delivery(uint32_t spin_budget) {
    if (!msi_configured() || spin_budget == 0U) return false;

    write_register(g_device, REG_IMC, UINT32_MAX);
    static_cast<void>(read_register(g_device, REG_ICR));
    __atomic_store_n(
        &g_device.last_interrupt_causes, UINT32_C(0), __ATOMIC_RELEASE);
    const uint64_t before = __atomic_load_n(
        &g_device.interrupt_count, __ATOMIC_ACQUIRE);
    write_register(g_device, REG_IMS, INTERRUPT_LINK_STATUS_CHANGE);
    write_register(g_device, REG_ICS, INTERRUPT_LINK_STATUS_CHANGE);

    bool delivered = false;
    for (uint32_t attempt = 0U; attempt < spin_budget; ++attempt) {
        if (__atomic_load_n(
                &g_device.interrupt_count, __ATOMIC_ACQUIRE) != before) {
            delivered =
                (__atomic_load_n(
                    &g_device.last_interrupt_causes, __ATOMIC_ACQUIRE) &
                 INTERRUPT_LINK_STATUS_CHANGE) != 0U;
            break;
        }
        relax();
    }
    write_register(g_device, REG_IMC, UINT32_MAX);
    static_cast<void>(read_register(g_device, REG_ICR));
    if (!delivered) {
        static_cast<void>(pci::msi::disable(&g_device.msi_route));
        g_device.msi_active = false;
    }
    return delivered;
}

uint64_t delivered_interrupts() {
    return __atomic_load_n(&g_device.interrupt_count, __ATOMIC_ACQUIRE);
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::NotInitialized: return "not initialized";
        case Status::NotFound: return "supported Intel E1000/E1000e NIC was not found";
        case Status::UnsupportedDevice: return "unsupported Intel GbE device";
        case Status::InvalidBar: return "invalid Intel GbE MMIO BAR";
        case Status::MmioMapFailed: return "Intel GbE MMIO mapping failed";
        case Status::ResetTimedOut: return "Intel GbE reset timed out";
        case Status::InvalidMac: return "Intel GbE has no valid unicast MAC";
        case Status::DmaAllocationFailed: return "Intel GbE DMA allocation failed";
        case Status::LinkDown: return "Intel GbE link is down";
        case Status::TransmitTimedOut: return "Intel GbE transmit timed out";
        case Status::DeviceError: return "Intel GbE device error";
        case Status::DmaQuiesceFailed: return "E1000e DMA did not quiesce";
        case Status::ResourceReleaseFailed: return "Intel GbE resources retained";
    }
    return "unknown E1000 status";
}

} // namespace net::e1000
