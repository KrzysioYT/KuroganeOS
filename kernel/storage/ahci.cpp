#include "ahci.hpp"

#include "ahci_protocol.hpp"
#include "dma.hpp"
#include "../drivers/pci.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/virtual_memory.hpp"

namespace storage::ahci {
namespace {

constexpr uint8_t PCI_CLASS_MASS_STORAGE = UINT8_C(0x01);
constexpr uint8_t PCI_SUBCLASS_SATA = UINT8_C(0x06);
constexpr uint8_t PCI_PROG_IF_AHCI = UINT8_C(0x01);
constexpr uint8_t PCI_COMMAND_OFFSET = UINT8_C(0x04);
constexpr uint8_t PCI_BAR5_OFFSET = UINT8_C(0x24);
constexpr uint16_t PCI_COMMAND_MEMORY_SPACE = UINT16_C(1) << 1U;
constexpr uint16_t PCI_COMMAND_BUS_MASTER = UINT16_C(1) << 2U;

constexpr uint64_t MMIO_WINDOW_BASE = UINT64_C(0xFFFFB00000000000);
constexpr size_t MMIO_WINDOW_PAGE_COUNT = 64U;
constexpr size_t MAXIMUM_PORT_SLOTS = MAXIMUM_DEVICES;
constexpr uint32_t POLL_ITERATIONS = UINT32_C(20000000);
constexpr uint32_t COMMAND_POLL_ITERATIONS = UINT32_C(50000000);
constexpr uint32_t LINK_POLL_ITERATIONS = UINT32_C(2000000);

constexpr size_t HBA_CAP = 0x00U;
constexpr size_t HBA_GHC = 0x04U;
constexpr size_t HBA_IS = 0x08U;
constexpr size_t HBA_PI = 0x0CU;
constexpr size_t HBA_CAP2 = 0x24U;
constexpr size_t HBA_BOHC = 0x28U;
constexpr size_t HBA_PORT_BASE = 0x100U;
constexpr size_t HBA_PORT_STRIDE = 0x80U;

constexpr uint32_t CAP_NUMBER_OF_PORTS_MASK = UINT32_C(0x1F);
constexpr uint32_t CAP_NUMBER_OF_SLOTS_MASK = UINT32_C(0x1F) << 8U;
constexpr uint32_t CAP_SUPPORTS_CLO = UINT32_C(1) << 24U;
constexpr uint32_t CAP_SUPPORTS_64_BIT = UINT32_C(1) << 31U;
constexpr uint32_t CAP2_BIOS_HANDOFF = UINT32_C(1);
constexpr uint32_t GHC_HBA_RESET = UINT32_C(1);
constexpr uint32_t GHC_INTERRUPT_ENABLE = UINT32_C(1) << 1U;
constexpr uint32_t GHC_AHCI_ENABLE = UINT32_C(1) << 31U;
constexpr uint32_t BOHC_BIOS_OWNED = UINT32_C(1);
constexpr uint32_t BOHC_OS_OWNED = UINT32_C(1) << 1U;
constexpr uint32_t BOHC_BIOS_BUSY = UINT32_C(1) << 4U;

constexpr size_t PORT_CLB = 0x00U;
constexpr size_t PORT_CLBU = 0x04U;
constexpr size_t PORT_FB = 0x08U;
constexpr size_t PORT_FBU = 0x0CU;
constexpr size_t PORT_IS = 0x10U;
constexpr size_t PORT_IE = 0x14U;
constexpr size_t PORT_CMD = 0x18U;
constexpr size_t PORT_TFD = 0x20U;
constexpr size_t PORT_SIG = 0x24U;
constexpr size_t PORT_SSTS = 0x28U;
constexpr size_t PORT_SERR = 0x30U;
constexpr size_t PORT_SACT = 0x34U;
constexpr size_t PORT_CI = 0x38U;

constexpr uint32_t PORT_CMD_START = UINT32_C(1);
constexpr uint32_t PORT_CMD_SPIN_UP = UINT32_C(1) << 1U;
constexpr uint32_t PORT_CMD_POWER_ON = UINT32_C(1) << 2U;
constexpr uint32_t PORT_CMD_CLO = UINT32_C(1) << 3U;
constexpr uint32_t PORT_CMD_FIS_RECEIVE_ENABLE = UINT32_C(1) << 4U;
constexpr uint32_t PORT_CMD_FIS_RECEIVE_RUNNING = UINT32_C(1) << 14U;
constexpr uint32_t PORT_CMD_COMMAND_LIST_RUNNING = UINT32_C(1) << 15U;
constexpr uint32_t PORT_CMD_INTERFACE_CONTROL_MASK = UINT32_C(0x0F) << 28U;
constexpr uint32_t PORT_CMD_INTERFACE_ACTIVE = UINT32_C(1) << 28U;
constexpr uint32_t PORT_TFD_ERROR = UINT32_C(1);
constexpr uint32_t PORT_TFD_DATA_REQUEST = UINT32_C(1) << 3U;
constexpr uint32_t PORT_TFD_DEVICE_FAULT = UINT32_C(1) << 5U;
constexpr uint32_t PORT_TFD_BUSY = UINT32_C(1) << 7U;
constexpr uint32_t PORT_INTERRUPT_OVERFLOW = UINT32_C(1) << 24U;
constexpr uint32_t PORT_INTERRUPT_NONFATAL_INTERFACE = UINT32_C(1) << 26U;
constexpr uint32_t PORT_INTERRUPT_INTERFACE_FATAL = UINT32_C(1) << 27U;
constexpr uint32_t PORT_INTERRUPT_HOST_BUS_DATA = UINT32_C(1) << 28U;
constexpr uint32_t PORT_INTERRUPT_HOST_BUS_FATAL = UINT32_C(1) << 29U;
constexpr uint32_t PORT_INTERRUPT_TASK_FILE_ERROR = UINT32_C(1) << 30U;
constexpr uint32_t PORT_INTERFACE_ERROR_MASK =
    PORT_INTERRUPT_OVERFLOW | PORT_INTERRUPT_NONFATAL_INTERFACE |
    PORT_INTERRUPT_INTERFACE_FATAL | PORT_INTERRUPT_HOST_BUS_DATA |
    PORT_INTERRUPT_HOST_BUS_FATAL;
constexpr uint32_t SATA_SIGNATURE_ATA = UINT32_C(0x00000101);
constexpr uint32_t SATA_STATUS_DET_MASK = UINT32_C(0x0F);
constexpr uint32_t SATA_STATUS_IPM_MASK = UINT32_C(0x0F) << 8U;
constexpr uint32_t SATA_STATUS_DEVICE_PRESENT = UINT32_C(0x03);
constexpr uint32_t SATA_STATUS_INTERFACE_ACTIVE = UINT32_C(0x01) << 8U;

constexpr size_t COMMAND_LIST_OFFSET = 0U;
constexpr size_t RECEIVED_FIS_OFFSET = 1024U;
constexpr size_t COMMAND_TABLE_OFFSET = 1280U;
constexpr size_t COMMAND_FIS_LENGTH_DWORDS = 5U;
constexpr uint16_t COMMAND_HEADER_WRITE = UINT16_C(1) << 6U;

struct CommandHeader {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t transferred_bytes;
    uint32_t command_table_base;
    uint32_t command_table_base_upper;
    uint32_t reserved[4];
};

struct PrdtEntry {
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t byte_count_and_interrupt;
};

struct CommandTable {
    uint8_t command_fis[64];
    uint8_t atapi_command[16];
    uint8_t reserved[48];
    PrdtEntry prdt[1];
};

static_assert(sizeof(CommandHeader) == 32U, "invalid AHCI command header");
static_assert(sizeof(PrdtEntry) == 16U, "invalid AHCI PRDT entry");
static_assert(sizeof(CommandTable) == 144U, "invalid AHCI command table");
static_assert(
    COMMAND_TABLE_OFFSET % 128U == 0U,
    "AHCI command table is not 128-byte aligned");

struct MmioMapping {
    uint64_t virtual_address;
    uint64_t aligned_virtual_address;
    uint64_t aligned_physical_address;
    size_t first_window_page;
    size_t page_count;
    bool active;
};

struct Controller {
    pci::Device pci_device;
    MmioMapping mapping;
    volatile uint8_t* registers;
    uint32_t capabilities;
    uint32_t ports_implemented;
    size_t mapped_register_bytes;
    uint16_t original_pci_command;
    uint8_t port_count;
    uint8_t slot_count;
    bool supports_64_bit_dma;
    bool supports_clo;
    bool active;
};

struct Port {
    Controller* controller;
    dma::Page command_memory;
    dma::Page bounce_buffer;
    block::Device device;
    DeviceInfo info;
    uint8_t port_number;
    bool engine_running;
    bool command_active;
    bool dma_may_be_active;
    bool registered;
    bool offline;
};

bool g_mmio_window_pages[MMIO_WINDOW_PAGE_COUNT]{};
Controller g_controllers[MAXIMUM_CONTROLLERS]{};
Port g_ports[MAXIMUM_PORT_SLOTS]{};
Port* g_devices[MAXIMUM_DEVICES]{};
size_t g_detected_controller_count = 0U;
size_t g_active_controller_count = 0U;
size_t g_failed_controller_count = 0U;
size_t g_port_slot_count = 0U;
size_t g_device_count = 0U;
Status g_initialize_status = Status::NotInitialized;
bool g_initialization_attempted = false;

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

void copy_text(char* destination, const char* source, size_t capacity) {
    if (capacity == 0U) {
        return;
    }
    size_t index = 0U;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

void cpu_relax() {
    __asm__ volatile("pause" : : : "memory");
}

void dma_write_barrier() {
    __asm__ volatile("sfence" : : : "memory");
}

void dma_read_barrier() {
    __asm__ volatile("lfence" : : : "memory");
}

bool range_is_free(size_t first_page, size_t page_count) {
    if (page_count == 0U || first_page >= MMIO_WINDOW_PAGE_COUNT ||
        page_count > MMIO_WINDOW_PAGE_COUNT - first_page) {
        return false;
    }
    for (size_t index = 0U; index < page_count; ++index) {
        if (g_mmio_window_pages[first_page + index]) {
            return false;
        }
    }
    return true;
}

void mark_range(size_t first_page, size_t page_count, bool used) {
    for (size_t index = 0U; index < page_count; ++index) {
        g_mmio_window_pages[first_page + index] = used;
    }
}

Status map_mmio(uint64_t physical_address, size_t byte_count,
                MmioMapping* output) {
    if (output == nullptr || byte_count == 0U) {
        return Status::InvalidArgument;
    }
    *output = {};
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) {
        return Status::MmioMappingFailed;
    }

    constexpr uint64_t page_size = memory::virtual_memory::PAGE_SIZE;
    constexpr uint64_t page_mask = page_size - UINT64_C(1);
    const uint64_t aligned_physical = physical_address & ~page_mask;
    const uint64_t page_offset = physical_address & page_mask;
    if (static_cast<uint64_t>(byte_count) > UINT64_MAX - page_offset) {
        return Status::MmioMappingFailed;
    }
    const uint64_t span = page_offset + static_cast<uint64_t>(byte_count);
    if (span > UINT64_MAX - page_mask) {
        return Status::MmioMappingFailed;
    }
    const uint64_t page_count_u64 = (span + page_mask) / page_size;
    if (page_count_u64 == 0U ||
        page_count_u64 > static_cast<uint64_t>(MMIO_WINDOW_PAGE_COUNT)) {
        return Status::MmioWindowUnavailable;
    }
    const size_t page_count = static_cast<size_t>(page_count_u64);

    for (size_t first_page = 0U;
         first_page <= MMIO_WINDOW_PAGE_COUNT - page_count;
         ++first_page) {
        if (!range_is_free(first_page, page_count)) {
            continue;
        }

        const uint64_t candidate = MMIO_WINDOW_BASE +
            static_cast<uint64_t>(first_page) * page_size;
        bool conflict = false;
        for (size_t index = 0U; index < page_count; ++index) {
            memory::virtual_memory::Mapping existing{};
            const auto query_status = memory::virtual_memory::query_page(
                address_space,
                candidate + static_cast<uint64_t>(index) * page_size,
                &existing);
            if (query_status == memory::virtual_memory::Status::NotMapped) {
                continue;
            }
            if (query_status != memory::virtual_memory::Status::Ok) {
                return Status::MmioMappingFailed;
            }
            conflict = true;
            break;
        }
        if (conflict) {
            continue;
        }

        size_t mapped_count = 0U;
        const auto flags = memory::virtual_memory::MapFlags::Writable |
            memory::virtual_memory::MapFlags::WriteThrough |
            memory::virtual_memory::MapFlags::CacheDisable |
            memory::virtual_memory::MapFlags::NoExecute;
        for (; mapped_count < page_count; ++mapped_count) {
            const auto map_status = memory::virtual_memory::map_page(
                address_space,
                candidate + static_cast<uint64_t>(mapped_count) * page_size,
                aligned_physical +
                    static_cast<uint64_t>(mapped_count) * page_size,
                flags);
            if (map_status != memory::virtual_memory::Status::Ok) {
                break;
            }
        }
        if (mapped_count != page_count) {
            while (mapped_count != 0U) {
                --mapped_count;
                static_cast<void>(memory::virtual_memory::unmap_page(
                    address_space,
                    candidate +
                        static_cast<uint64_t>(mapped_count) * page_size));
            }
            return Status::MmioMappingFailed;
        }

        mark_range(first_page, page_count, true);
        output->virtual_address = candidate + page_offset;
        output->aligned_virtual_address = candidate;
        output->aligned_physical_address = aligned_physical;
        output->first_window_page = first_page;
        output->page_count = page_count;
        output->active = true;
        return Status::Ok;
    }
    return Status::MmioWindowUnavailable;
}

void unmap_mmio(MmioMapping* mapping) {
    if (mapping == nullptr || !mapping->active) {
        return;
    }
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space != nullptr) {
        for (size_t index = 0U; index < mapping->page_count; ++index) {
            static_cast<void>(memory::virtual_memory::unmap_page(
                address_space,
                mapping->aligned_virtual_address +
                    static_cast<uint64_t>(index) *
                        memory::virtual_memory::PAGE_SIZE));
        }
    }
    mark_range(mapping->first_window_page, mapping->page_count, false);
    *mapping = {};
}

volatile uint32_t* register_address(Controller& controller, size_t offset) {
    return reinterpret_cast<volatile uint32_t*>(
        controller.registers + offset);
}

uint32_t read_register(Controller& controller, size_t offset) {
    return *register_address(controller, offset);
}

void write_register(Controller& controller, size_t offset, uint32_t value) {
    *register_address(controller, offset) = value;
}

size_t port_register_offset(const Port& port, size_t register_offset) {
    return HBA_PORT_BASE +
        static_cast<size_t>(port.port_number) * HBA_PORT_STRIDE +
        register_offset;
}

uint32_t read_port_register(Port& port, size_t register_offset) {
    return read_register(
        *port.controller,
        port_register_offset(port, register_offset));
}

void write_port_register(
    Port& port, size_t register_offset, uint32_t value) {
    write_register(
        *port.controller,
        port_register_offset(port, register_offset),
        value);
}

bool wait_register_clear(
    Controller& controller,
    size_t offset,
    uint32_t mask,
    uint32_t iterations) {
    for (uint32_t attempt = 0U; attempt < iterations; ++attempt) {
        if ((read_register(controller, offset) & mask) == 0U) {
            return true;
        }
        cpu_relax();
    }
    return false;
}

bool wait_port_register_clear(
    Port& port,
    size_t offset,
    uint32_t mask,
    uint32_t iterations) {
    for (uint32_t attempt = 0U; attempt < iterations; ++attempt) {
        if ((read_port_register(port, offset) & mask) == 0U) {
            return true;
        }
        cpu_relax();
    }
    return false;
}

bool wait_port_register_set(
    Port& port,
    size_t offset,
    uint32_t mask,
    uint32_t iterations) {
    for (uint32_t attempt = 0U; attempt < iterations; ++attempt) {
        if ((read_port_register(port, offset) & mask) == mask) {
            return true;
        }
        cpu_relax();
    }
    return false;
}

Status probe_bar5(const pci::Device& pci_device,
                  protocol::PciBar* output,
                  uint16_t* original_command) {
    if (output == nullptr || original_command == nullptr) {
        return Status::InvalidArgument;
    }
    *output = {};
    *original_command = pci::read16(
        pci_device.address, PCI_COMMAND_OFFSET);
    const uint32_t original_bar = pci::read32(
        pci_device.address, PCI_BAR5_OFFSET);

    pci::write16(
        pci_device.address,
        PCI_COMMAND_OFFSET,
        static_cast<uint16_t>(*original_command &
            static_cast<uint16_t>(~UINT16_C(0x0007))));
    pci::write32(pci_device.address, PCI_BAR5_OFFSET, UINT32_MAX);
    const uint32_t size_probe = pci::read32(
        pci_device.address, PCI_BAR5_OFFSET);
    pci::write32(pci_device.address, PCI_BAR5_OFFSET, original_bar);
    pci::write16(
        pci_device.address, PCI_COMMAND_OFFSET, *original_command);

    return protocol::decode_bar5(original_bar, size_probe, output) ==
            protocol::Status::Ok
        ? Status::Ok
        : Status::InvalidPciBar;
}

Status claim_controller(Controller& controller) {
    if ((read_register(controller, HBA_CAP2) & CAP2_BIOS_HANDOFF) != 0U) {
        write_register(
            controller,
            HBA_BOHC,
            read_register(controller, HBA_BOHC) | BOHC_OS_OWNED);
        bool ownership_acquired = false;
        for (uint32_t attempt = 0U; attempt < POLL_ITERATIONS; ++attempt) {
            const uint32_t ownership =
                read_register(controller, HBA_BOHC);
            if ((ownership & (BOHC_BIOS_OWNED | BOHC_BIOS_BUSY)) == 0U) {
                ownership_acquired = true;
                break;
            }
            cpu_relax();
        }
        if (!ownership_acquired) {
            return Status::BiosHandoffTimeout;
        }
    }

    uint32_t control = read_register(controller, HBA_GHC);
    control |= GHC_AHCI_ENABLE;
    control &= ~GHC_INTERRUPT_ENABLE;
    write_register(controller, HBA_GHC, control);
    write_register(
        controller,
        HBA_GHC,
        control | GHC_HBA_RESET);
    if (!wait_register_clear(
            controller,
            HBA_GHC,
            GHC_HBA_RESET,
            POLL_ITERATIONS)) {
        return Status::ControllerResetTimeout;
    }

    control = read_register(controller, HBA_GHC);
    control |= GHC_AHCI_ENABLE;
    control &= ~GHC_INTERRUPT_ENABLE;
    write_register(controller, HBA_GHC, control);
    write_register(controller, HBA_IS, UINT32_MAX);

    controller.capabilities = read_register(controller, HBA_CAP);
    controller.ports_implemented = read_register(controller, HBA_PI);
    controller.port_count = static_cast<uint8_t>(
        (controller.capabilities & CAP_NUMBER_OF_PORTS_MASK) + UINT32_C(1));
    controller.slot_count = static_cast<uint8_t>(
        ((controller.capabilities & CAP_NUMBER_OF_SLOTS_MASK) >> 8U) +
        UINT32_C(1));
    controller.supports_64_bit_dma =
        (controller.capabilities & CAP_SUPPORTS_64_BIT) != 0U;
    controller.supports_clo =
        (controller.capabilities & CAP_SUPPORTS_CLO) != 0U;
    const size_t required_register_bytes = HBA_PORT_BASE +
        static_cast<size_t>(controller.port_count) * HBA_PORT_STRIDE;
    if (required_register_bytes > controller.mapped_register_bytes) {
        return Status::InvalidPciBar;
    }
    return Status::Ok;
}

Status stop_engine(Port& port) {
    uint32_t command = read_port_register(port, PORT_CMD);
    command &= ~PORT_CMD_START;
    write_port_register(port, PORT_CMD, command);
    if (!wait_port_register_clear(
            port,
            PORT_CMD,
            PORT_CMD_COMMAND_LIST_RUNNING,
            POLL_ITERATIONS)) {
        return Status::PortStopTimeout;
    }
    command = read_port_register(port, PORT_CMD);
    command &= ~PORT_CMD_FIS_RECEIVE_ENABLE;
    write_port_register(port, PORT_CMD, command);
    if (!wait_port_register_clear(
            port,
            PORT_CMD,
            PORT_CMD_FIS_RECEIVE_RUNNING,
            POLL_ITERATIONS)) {
        return Status::PortStopTimeout;
    }
    port.engine_running = false;
    return Status::Ok;
}

Status start_engine(Port& port) {
    uint32_t command = read_port_register(port, PORT_CMD);
    if ((command & (PORT_CMD_COMMAND_LIST_RUNNING |
                    PORT_CMD_FIS_RECEIVE_RUNNING)) != 0U) {
        return Status::PortStartTimeout;
    }
    command |= PORT_CMD_POWER_ON | PORT_CMD_SPIN_UP |
        PORT_CMD_FIS_RECEIVE_ENABLE;
    write_port_register(port, PORT_CMD, command);
    if (!wait_port_register_set(
            port,
            PORT_CMD,
            PORT_CMD_FIS_RECEIVE_RUNNING,
            POLL_ITERATIONS)) {
        return Status::PortStartTimeout;
    }
    command = read_port_register(port, PORT_CMD) | PORT_CMD_START;
    write_port_register(port, PORT_CMD, command);
    if (!wait_port_register_set(
            port,
            PORT_CMD,
            PORT_CMD_COMMAND_LIST_RUNNING,
            POLL_ITERATIONS)) {
        return Status::PortStartTimeout;
    }
    port.engine_running = true;
    return Status::Ok;
}

Status wait_device_ready(Port& port) {
    constexpr uint32_t busy_mask =
        PORT_TFD_BUSY | PORT_TFD_DATA_REQUEST;
    if (wait_port_register_clear(
            port, PORT_TFD, busy_mask, POLL_ITERATIONS)) {
        return Status::Ok;
    }
    if (!port.controller->supports_clo) {
        return Status::DeviceBusyTimeout;
    }

    write_port_register(
        port,
        PORT_CMD,
        read_port_register(port, PORT_CMD) | PORT_CMD_CLO);
    if (!wait_port_register_clear(
            port, PORT_CMD, PORT_CMD_CLO, POLL_ITERATIONS) ||
        !wait_port_register_clear(
            port, PORT_TFD, busy_mask, POLL_ITERATIONS)) {
        return Status::DeviceBusyTimeout;
    }
    return Status::Ok;
}

Status wait_for_sata_signature(Port& port) {
    for (uint32_t attempt = 0U;
         attempt < LINK_POLL_ITERATIONS;
         ++attempt) {
        const uint32_t signature = read_port_register(port, PORT_SIG);
        if (signature == SATA_SIGNATURE_ATA) {
            return Status::Ok;
        }
        // All-ones and zero are transitional/reset values. Any other stable
        // signature describes a non-ATA device (for example ATAPI or a port
        // multiplier), which this deliberately SATA-only backend rejects.
        if (signature != UINT32_MAX && signature != 0U) {
            return Status::NoSataDevice;
        }
        cpu_relax();
    }
    return Status::NoSataDevice;
}

CommandHeader* command_header(Port& port) {
    return reinterpret_cast<CommandHeader*>(
        static_cast<uint8_t*>(port.command_memory.virtual_address) +
        COMMAND_LIST_OFFSET);
}

CommandTable* command_table(Port& port) {
    return reinterpret_cast<CommandTable*>(
        static_cast<uint8_t*>(port.command_memory.virtual_address) +
        COMMAND_TABLE_OFFSET);
}

void snapshot_port_error(Port& port, Status status) {
    port.info.last_status = status;
    port.info.last_interrupt_status = read_port_register(port, PORT_IS);
    port.info.last_task_file_data = read_port_register(port, PORT_TFD);
    port.info.last_sata_error = read_port_register(port, PORT_SERR);
}

Status issue_command(
    Port& port,
    protocol::AtaCommand command,
    uint64_t first_lba,
    uint16_t sector_count,
    size_t transfer_bytes,
    bool device_write) {
    if (port.offline || !port.engine_running) {
        return Status::PortOffline;
    }
    if (port.command_active) {
        return Status::CommandSlotBusy;
    }
    port.command_active = true;

    if ((read_port_register(port, PORT_CI) & UINT32_C(1)) != 0U ||
        (read_port_register(port, PORT_SACT) & UINT32_C(1)) != 0U) {
        port.command_active = false;
        snapshot_port_error(port, Status::CommandSlotBusy);
        return Status::CommandSlotBusy;
    }
    const Status ready_status = wait_device_ready(port);
    if (ready_status != Status::Ok) {
        port.command_active = false;
        snapshot_port_error(port, ready_status);
        return ready_status;
    }
    if (transfer_bytes > memory::virtual_memory::PAGE_SIZE) {
        port.command_active = false;
        snapshot_port_error(port, Status::InvalidArgument);
        return Status::InvalidArgument;
    }

    CommandHeader* const header = command_header(port);
    CommandTable* const table = command_table(port);
    clear_bytes(header, sizeof(CommandHeader));
    clear_bytes(table, sizeof(CommandTable));
    const auto fis_status = protocol::build_register_fis(
        command, first_lba, sector_count, table->command_fis);
    if (fis_status != protocol::Status::Ok) {
        port.command_active = false;
        snapshot_port_error(port, Status::InvalidArgument);
        return Status::InvalidArgument;
    }

    header->flags = static_cast<uint16_t>(COMMAND_FIS_LENGTH_DWORDS);
    if (device_write) {
        header->flags = static_cast<uint16_t>(
            header->flags | COMMAND_HEADER_WRITE);
    }
    header->prdt_length = transfer_bytes == 0U ? UINT16_C(0) : UINT16_C(1);
    const uint64_t table_physical =
        port.command_memory.physical_address + COMMAND_TABLE_OFFSET;
    header->command_table_base = static_cast<uint32_t>(table_physical);
    header->command_table_base_upper =
        static_cast<uint32_t>(table_physical >> 32U);
    if (transfer_bytes != 0U) {
        table->prdt[0].data_base = static_cast<uint32_t>(
            port.bounce_buffer.physical_address);
        table->prdt[0].data_base_upper = static_cast<uint32_t>(
            port.bounce_buffer.physical_address >> 32U);
        table->prdt[0].byte_count_and_interrupt =
            static_cast<uint32_t>(transfer_bytes - 1U);
    }

    write_port_register(port, PORT_IS, UINT32_MAX);
    write_port_register(port, PORT_SERR, UINT32_MAX);
    dma_write_barrier();
    write_port_register(port, PORT_CI, UINT32_C(1));

    Status completion_status = Status::CommandTimeout;
    for (uint32_t attempt = 0U;
         attempt < COMMAND_POLL_ITERATIONS;
         ++attempt) {
        const uint32_t interrupt_status = read_port_register(port, PORT_IS);
        if ((interrupt_status & PORT_INTERFACE_ERROR_MASK) != 0U) {
            completion_status = Status::InterfaceError;
            break;
        }
        if ((interrupt_status & PORT_INTERRUPT_TASK_FILE_ERROR) != 0U) {
            completion_status = Status::TaskFileError;
            break;
        }
        if ((read_port_register(port, PORT_CI) & UINT32_C(1)) == 0U) {
            completion_status = Status::Ok;
            break;
        }
        cpu_relax();
    }

    if (completion_status != Status::CommandTimeout &&
        !wait_port_register_clear(
            port, PORT_CI, UINT32_C(1), POLL_ITERATIONS)) {
        completion_status = Status::CommandTimeout;
    }
    if (completion_status == Status::CommandTimeout) {
        port.dma_may_be_active = true;
        port.offline = true;
    } else if (completion_status == Status::InterfaceError) {
        // This polling-only backend has no link-reset/error-recovery state
        // machine. Never reuse a port after an AHCI interface/host-bus fault.
        port.offline = true;
    }

    dma_read_barrier();
    const uint32_t task_file_data = read_port_register(port, PORT_TFD);
    if (completion_status == Status::Ok &&
        (task_file_data &
         (PORT_TFD_ERROR | PORT_TFD_DEVICE_FAULT)) != 0U) {
        completion_status = Status::TaskFileError;
    }
    const auto* transferred = reinterpret_cast<volatile const uint32_t*>(
        reinterpret_cast<const uint8_t*>(header) + 4U);
    if (completion_status == Status::Ok && transfer_bytes != 0U &&
        *transferred != static_cast<uint32_t>(transfer_bytes)) {
        completion_status = Status::ShortTransfer;
    }

    port.command_active = false;
    snapshot_port_error(port, completion_status);
    write_port_register(port, PORT_IS, UINT32_MAX);
    return completion_status;
}

block::Status block_status(Status status) {
    switch (status) {
        case Status::Ok:
            return block::Status::Ok;
        case Status::InvalidArgument:
            return block::Status::InvalidArgument;
        case Status::OutOfRange:
            return block::Status::OutOfRange;
        case Status::PortOffline:
        case Status::NoSataDevice:
            return block::Status::NoDevice;
        case Status::CommandSlotBusy:
            return block::Status::DeviceBusy;
        case Status::DeviceBusyTimeout:
        case Status::CommandTimeout:
        case Status::PortStartTimeout:
        case Status::PortStopTimeout:
            return block::Status::TimedOut;
        case Status::DmaAddressNotSupported:
            return block::Status::AddressNotSupported;
        case Status::TaskFileError:
            return block::Status::CommandFailed;
        case Status::InterfaceError:
            return block::Status::ControllerFault;
        case Status::ShortTransfer:
            return block::Status::IoError;
        default:
            return block::Status::BackendFailure;
    }
}

Status validate_io_request(
    Port& port,
    uint64_t first_block,
    uint64_t block_count,
    const void* buffer) {
    if (buffer == nullptr || block_count == 0U) {
        return Status::InvalidArgument;
    }
    if (port.offline || !port.registered) {
        return Status::PortOffline;
    }
    if (first_block >= port.info.sector_count ||
        block_count > port.info.sector_count - first_block) {
        return Status::OutOfRange;
    }
    return Status::Ok;
}

block::Status read_blocks_callback(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    void* destination) {
    auto* port = static_cast<Port*>(context);
    if (port == nullptr) {
        return block::Status::InvalidArgument;
    }
    const Status validation = validate_io_request(
        *port, first_block, block_count, destination);
    if (validation != Status::Ok) {
        port->info.last_status = validation;
        return block_status(validation);
    }

    auto* output = static_cast<uint8_t*>(destination);
    const uint64_t maximum_sectors =
        memory::virtual_memory::PAGE_SIZE / port->info.sector_size;
    uint64_t completed = 0U;
    while (completed < block_count) {
        const uint64_t remaining = block_count - completed;
        const uint64_t chunk =
            remaining < maximum_sectors ? remaining : maximum_sectors;
        const size_t byte_count = static_cast<size_t>(
            chunk * static_cast<uint64_t>(port->info.sector_size));
        const Status status = issue_command(
            *port,
            protocol::AtaCommand::ReadDmaExt,
            first_block + completed,
            static_cast<uint16_t>(chunk),
            byte_count,
            false);
        if (status != Status::Ok) {
            return block_status(status);
        }
        copy_bytes(
            output + static_cast<size_t>(
                completed *
                static_cast<uint64_t>(port->info.sector_size)),
            port->bounce_buffer.virtual_address,
            byte_count);
        completed += chunk;
    }
    port->info.last_status = Status::Ok;
    return block::Status::Ok;
}

block::Status write_blocks_callback(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    const void* source) {
    auto* port = static_cast<Port*>(context);
    if (port == nullptr) {
        return block::Status::InvalidArgument;
    }
    const Status validation = validate_io_request(
        *port, first_block, block_count, source);
    if (validation != Status::Ok) {
        port->info.last_status = validation;
        return block_status(validation);
    }

    const auto* input = static_cast<const uint8_t*>(source);
    const uint64_t maximum_sectors =
        memory::virtual_memory::PAGE_SIZE / port->info.sector_size;
    uint64_t completed = 0U;
    while (completed < block_count) {
        const uint64_t remaining = block_count - completed;
        const uint64_t chunk =
            remaining < maximum_sectors ? remaining : maximum_sectors;
        const size_t byte_count = static_cast<size_t>(
            chunk * static_cast<uint64_t>(port->info.sector_size));
        copy_bytes(
            port->bounce_buffer.virtual_address,
            input + static_cast<size_t>(
                completed *
                static_cast<uint64_t>(port->info.sector_size)),
            byte_count);
        const Status status = issue_command(
            *port,
            protocol::AtaCommand::WriteDmaExt,
            first_block + completed,
            static_cast<uint16_t>(chunk),
            byte_count,
            true);
        if (status != Status::Ok) {
            return block_status(status);
        }
        completed += chunk;
    }
    port->info.last_status = Status::Ok;
    return block::Status::Ok;
}

block::Status flush_callback(void* context) {
    auto* port = static_cast<Port*>(context);
    if (port == nullptr) {
        return block::Status::InvalidArgument;
    }
    if (port->offline || !port->registered) {
        port->info.last_status = Status::PortOffline;
        return block::Status::NoDevice;
    }
    return block_status(issue_command(
        *port,
        protocol::AtaCommand::FlushCacheExt,
        0U,
        0U,
        0U,
        false));
}

void release_port_memory(Port& port) {
    if (port.dma_may_be_active) {
        return;
    }
    if (port.engine_running && stop_engine(port) != Status::Ok) {
        port.dma_may_be_active = true;
        return;
    }
    write_port_register(port, PORT_CLB, 0U);
    write_port_register(port, PORT_CLBU, 0U);
    write_port_register(port, PORT_FB, 0U);
    write_port_register(port, PORT_FBU, 0U);
    if (port.bounce_buffer.allocated) {
        static_cast<void>(dma::release_page(&port.bounce_buffer));
    }
    if (port.command_memory.allocated) {
        static_cast<void>(dma::release_page(&port.command_memory));
    }
}

Status configure_port(Controller& controller, uint8_t port_number) {
    if (g_port_slot_count >= MAXIMUM_PORT_SLOTS) {
        return Status::DeviceLimitReached;
    }
    Port& port = g_ports[g_port_slot_count++];
    port = {};
    port.controller = &controller;
    port.port_number = port_number;
    port.info.pci_bus = controller.pci_device.address.bus;
    port.info.pci_slot = controller.pci_device.address.slot;
    port.info.pci_function = controller.pci_device.address.function;
    port.info.port = port_number;
    port.info.controller_supports_64_bit_dma =
        controller.supports_64_bit_dma;

    Status status = stop_engine(port);
    if (status != Status::Ok) {
        snapshot_port_error(port, status);
        return status;
    }

    dma::Status dma_status = dma::allocate_page(
        controller.supports_64_bit_dma, &port.command_memory);
    if (dma_status != dma::Status::Ok) {
        status = dma_status == dma::Status::AddressNotSupported
            ? Status::DmaAddressNotSupported
            : Status::DmaAllocationFailed;
        snapshot_port_error(port, status);
        return status;
    }
    dma_status = dma::allocate_page(
        controller.supports_64_bit_dma, &port.bounce_buffer);
    if (dma_status != dma::Status::Ok) {
        static_cast<void>(dma::release_page(&port.command_memory));
        status = dma_status == dma::Status::AddressNotSupported
            ? Status::DmaAddressNotSupported
            : Status::DmaAllocationFailed;
        snapshot_port_error(port, status);
        return status;
    }

    clear_bytes(
        port.command_memory.virtual_address,
        static_cast<size_t>(memory::virtual_memory::PAGE_SIZE));
    clear_bytes(
        port.bounce_buffer.virtual_address,
        static_cast<size_t>(memory::virtual_memory::PAGE_SIZE));

    const uint64_t command_list_physical =
        port.command_memory.physical_address + COMMAND_LIST_OFFSET;
    const uint64_t received_fis_physical =
        port.command_memory.physical_address + RECEIVED_FIS_OFFSET;
    write_port_register(
        port, PORT_CLB, static_cast<uint32_t>(command_list_physical));
    write_port_register(
        port, PORT_CLBU,
        static_cast<uint32_t>(command_list_physical >> 32U));
    write_port_register(
        port, PORT_FB, static_cast<uint32_t>(received_fis_physical));
    write_port_register(
        port, PORT_FBU,
        static_cast<uint32_t>(received_fis_physical >> 32U));
    write_port_register(port, PORT_IE, 0U);
    write_port_register(port, PORT_IS, UINT32_MAX);
    write_port_register(port, PORT_SERR, UINT32_MAX);

    status = start_engine(port);
    if (status != Status::Ok) {
        snapshot_port_error(port, status);
        release_port_memory(port);
        return status;
    }
    status = wait_for_sata_signature(port);
    if (status != Status::Ok) {
        snapshot_port_error(port, status);
        release_port_memory(port);
        return status;
    }
    status = wait_device_ready(port);
    if (status != Status::Ok) {
        snapshot_port_error(port, status);
        release_port_memory(port);
        return status;
    }

    status = issue_command(
        port,
        protocol::AtaCommand::IdentifyDevice,
        0U,
        0U,
        512U,
        false);
    if (status != Status::Ok) {
        release_port_memory(port);
        return status;
    }

    protocol::IdentifyInfo identify{};
    const auto identify_status = protocol::parse_identify(
        static_cast<const uint16_t*>(port.bounce_buffer.virtual_address),
        &identify);
    if (identify_status != protocol::Status::Ok) {
        if (identify_status == protocol::Status::Lba48Unsupported) {
            status = Status::Lba48Unsupported;
        } else if (
            identify_status == protocol::Status::UnsupportedSectorSize) {
            status = Status::UnsupportedSectorSize;
        } else {
            status = Status::InvalidIdentifyData;
        }
        snapshot_port_error(port, status);
        release_port_memory(port);
        return status;
    }

    port.info.sector_size = identify.logical_sector_size;
    port.info.sector_count = identify.sector_count;
    copy_text(port.info.model, identify.model, sizeof(port.info.model));
    port.info.last_status = Status::Ok;
    port.device = {
        &port,
        identify.logical_sector_size,
        identify.sector_count,
        read_blocks_callback,
        write_blocks_callback,
        flush_callback};
    port.registered = true;
    g_devices[g_device_count++] = &port;
    return Status::Ok;
}

bool sata_device_present(Controller& controller, uint8_t port_number) {
    Port probe{};
    probe.controller = &controller;
    probe.port_number = port_number;

    // A global HBA reset may leave staggered-spin-up ports with SUD clear and
    // the link in a transitional state. Request power/spin-up and active IPM,
    // then poll a bounded interval before deciding that an implemented port
    // is empty. No command structures or media accesses are involved here.
    uint32_t command = read_port_register(probe, PORT_CMD);
    command |= PORT_CMD_POWER_ON | PORT_CMD_SPIN_UP;
    command &= ~PORT_CMD_INTERFACE_CONTROL_MASK;
    command |= PORT_CMD_INTERFACE_ACTIVE;
    write_port_register(probe, PORT_CMD, command);

    for (uint32_t attempt = 0U;
         attempt < LINK_POLL_ITERATIONS;
         ++attempt) {
        const uint32_t sata_status = read_port_register(probe, PORT_SSTS);
        if ((sata_status & SATA_STATUS_DET_MASK) ==
                SATA_STATUS_DEVICE_PRESENT &&
            (sata_status & SATA_STATUS_IPM_MASK) ==
                SATA_STATUS_INTERFACE_ACTIVE) {
            return true;
        }
        cpu_relax();
    }
    return false;
}

Status initialize_controller(
    Controller& controller, const pci::Device& pci_device) {
    controller = {};
    controller.pci_device = pci_device;

    protocol::PciBar bar{};
    Status status = probe_bar5(
        pci_device, &bar, &controller.original_pci_command);
    if (status != Status::Ok) {
        return status;
    }
    controller.mapped_register_bytes =
        bar.size < protocol::AHCI_REGISTER_SPAN
        ? static_cast<size_t>(bar.size)
        : static_cast<size_t>(protocol::AHCI_REGISTER_SPAN);
    status = map_mmio(
        bar.physical_address,
        controller.mapped_register_bytes,
        &controller.mapping);
    if (status != Status::Ok) {
        return status;
    }
    controller.registers = reinterpret_cast<volatile uint8_t*>(
        static_cast<uintptr_t>(controller.mapping.virtual_address));

    const uint16_t enabled_command = static_cast<uint16_t>(
        controller.original_pci_command | PCI_COMMAND_MEMORY_SPACE |
        PCI_COMMAND_BUS_MASTER);
    pci::write16(pci_device.address, PCI_COMMAND_OFFSET, enabled_command);
    if ((pci::read16(pci_device.address, PCI_COMMAND_OFFSET) &
         (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) !=
        (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) {
        pci::write16(
            pci_device.address,
            PCI_COMMAND_OFFSET,
            controller.original_pci_command);
        unmap_mmio(&controller.mapping);
        controller.registers = nullptr;
        return Status::PciCommandRejected;
    }

    status = claim_controller(controller);
    if (status != Status::Ok) {
        pci::write16(
            pci_device.address,
            PCI_COMMAND_OFFSET,
            controller.original_pci_command);
        unmap_mmio(&controller.mapping);
        controller.registers = nullptr;
        return status;
    }
    controller.active = true;

    Status last_port_failure = Status::NoSataDevice;
    size_t controller_devices = 0U;
    for (uint8_t port_number = 0U;
         port_number < controller.port_count;
         ++port_number) {
        const uint32_t port_bit = UINT32_C(1) << port_number;
        if ((controller.ports_implemented & port_bit) == 0U ||
            !sata_device_present(controller, port_number)) {
            continue;
        }
        const Status port_status = configure_port(controller, port_number);
        if (port_status == Status::Ok) {
            ++controller_devices;
        } else {
            last_port_failure = port_status;
        }
    }
    return controller_devices != 0U ? Status::Ok : last_port_failure;
}

} // namespace

Status initialize() {
    if (g_initialization_attempted) {
        return g_initialize_status == Status::Ok
            ? Status::AlreadyInitialized
            : g_initialize_status;
    }
    g_initialization_attempted = true;

    Status last_failure = Status::NoController;
    for (size_t index = 0U; index < pci::device_count(); ++index) {
        const pci::Device* const pci_device = pci::device_at(index);
        if (pci_device == nullptr ||
            pci_device->class_code != PCI_CLASS_MASS_STORAGE ||
            pci_device->subclass != PCI_SUBCLASS_SATA ||
            pci_device->programming_interface != PCI_PROG_IF_AHCI) {
            continue;
        }
        ++g_detected_controller_count;
        if (g_active_controller_count + g_failed_controller_count >=
            MAXIMUM_CONTROLLERS) {
            ++g_failed_controller_count;
            last_failure = Status::ControllerLimitReached;
            continue;
        }

        Controller& controller = g_controllers[
            g_active_controller_count + g_failed_controller_count];
        const Status status = initialize_controller(controller, *pci_device);
        if (controller.active) {
            ++g_active_controller_count;
        } else {
            ++g_failed_controller_count;
        }
        if (status != Status::Ok) {
            last_failure = status;
        }
    }

    if (g_device_count != 0U) {
        g_initialize_status = Status::Ok;
    } else if (g_detected_controller_count == 0U) {
        g_initialize_status = Status::NoController;
    } else if (last_failure == Status::NoController) {
        g_initialize_status = Status::NoSataDevice;
    } else {
        g_initialize_status = last_failure;
    }
    return g_initialize_status;
}

bool initialized() {
    return g_initialize_status == Status::Ok;
}

bool initialization_attempted() {
    return g_initialization_attempted;
}

Status initialization_status() {
    return g_initialize_status;
}

size_t detected_controller_count() {
    return g_detected_controller_count;
}

size_t active_controller_count() {
    return g_active_controller_count;
}

size_t failed_controller_count() {
    return g_failed_controller_count;
}

size_t device_count() {
    return g_device_count;
}

const block::Device* device_at(size_t index) {
    return index < g_device_count ? &g_devices[index]->device : nullptr;
}

const DeviceInfo* device_info_at(size_t index) {
    return index < g_device_count ? &g_devices[index]->info : nullptr;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok:
            return "ok";
        case Status::AlreadyInitialized:
            return "already initialized";
        case Status::NotInitialized:
            return "not initialized";
        case Status::NoController:
            return "no PCI AHCI controller";
        case Status::ControllerLimitReached:
            return "AHCI controller limit reached";
        case Status::InvalidPciBar:
            return "invalid AHCI BAR5";
        case Status::PciCommandRejected:
            return "PCI memory or bus-master enable was rejected";
        case Status::MmioWindowUnavailable:
            return "dedicated AHCI MMIO window is unavailable";
        case Status::MmioMappingFailed:
            return "AHCI MMIO mapping failed";
        case Status::BiosHandoffTimeout:
            return "AHCI BIOS ownership handoff timed out";
        case Status::ControllerResetTimeout:
            return "AHCI controller reset timed out";
        case Status::NoSataDevice:
            return "no supported SATA disk";
        case Status::DeviceLimitReached:
            return "AHCI device limit reached";
        case Status::DmaAllocationFailed:
            return "AHCI DMA page allocation failed";
        case Status::DmaAddressNotSupported:
            return "no DMA page in the controller address range";
        case Status::PortStopTimeout:
            return "AHCI port stop timed out";
        case Status::PortStartTimeout:
            return "AHCI port start timed out";
        case Status::DeviceBusyTimeout:
            return "ATA device remained busy";
        case Status::CommandSlotBusy:
            return "AHCI command slot zero is busy";
        case Status::CommandTimeout:
            return "AHCI command timed out";
        case Status::TaskFileError:
            return "ATA task-file error";
        case Status::InterfaceError:
            return "AHCI interface or host-bus error";
        case Status::ShortTransfer:
            return "AHCI command transferred fewer bytes than requested";
        case Status::InvalidIdentifyData:
            return "invalid ATA IDENTIFY response";
        case Status::Lba48Unsupported:
            return "ATA disk does not support LBA48";
        case Status::UnsupportedSectorSize:
            return "ATA logical-sector size is unsupported";
        case Status::InvalidArgument:
            return "invalid AHCI argument";
        case Status::OutOfRange:
            return "AHCI block range is outside the disk";
        case Status::PortOffline:
            return "AHCI port is offline";
    }
    return "unknown AHCI status";
}

} // namespace storage::ahci
