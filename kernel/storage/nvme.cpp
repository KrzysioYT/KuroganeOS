#include "nvme.hpp"

#include "dma.hpp"
#include "nvme_protocol.hpp"
#include "../drivers/pci.hpp"
#include "../drivers/pci_bar.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/virtual_memory.hpp"

namespace storage::nvme {
namespace {

constexpr uint8_t PCI_CLASS_MASS_STORAGE = UINT8_C(0x01);
constexpr uint8_t PCI_SUBCLASS_NVM = UINT8_C(0x08);
constexpr uint8_t PCI_PROG_IF_NVME = UINT8_C(0x02);
constexpr uint8_t PCI_COMMAND_OFFSET = UINT8_C(0x04);
constexpr uint16_t PCI_COMMAND_MEMORY_SPACE = UINT16_C(1) << 1U;
constexpr uint16_t PCI_COMMAND_BUS_MASTER = UINT16_C(1) << 2U;

constexpr uint64_t MMIO_VIRTUAL_BASE = UINT64_C(0xFFFFB50000000000);
constexpr size_t MMIO_PAGE_COUNT = 2U;
constexpr size_t MMIO_REQUIRED_BYTES =
    MMIO_PAGE_COUNT * memory::virtual_memory::PAGE_SIZE;

constexpr size_t REG_CAP = 0x00U;
constexpr size_t REG_VS = 0x08U;
constexpr size_t REG_CC = 0x14U;
constexpr size_t REG_CSTS = 0x1CU;
constexpr size_t REG_AQA = 0x24U;
constexpr size_t REG_ASQ = 0x28U;
constexpr size_t REG_ACQ = 0x30U;
constexpr size_t REG_DOORBELL_BASE = 0x1000U;

constexpr uint32_t CC_ENABLE = UINT32_C(1);
constexpr uint32_t CSTS_READY = UINT32_C(1);
constexpr uint32_t CSTS_FATAL = UINT32_C(1) << 1U;
constexpr uint32_t POLL_BUDGET = UINT32_C(20000000);
constexpr uint16_t DESIRED_ADMIN_QUEUE_ENTRIES = 16U;

struct Controller {
    pci::Device pci_device;
    volatile uint8_t* registers;
    pci::bar::Info bar;
    protocol::Capabilities capabilities;
    dma::Page admin_submission_queue;
    dma::Page admin_completion_queue;
    dma::Page identify_page;
    uint16_t original_pci_command;
    uint16_t queue_entries;
    uint16_t submission_tail;
    uint16_t completion_head;
    uint16_t next_command_id;
    bool completion_phase;
    size_t mapped_pages;
    bool pci_enabled;
    bool controller_enabled;
    bool ready;
    ControllerInfo info;
};

Controller g_controller{};

void clear_bytes(void* destination, size_t count) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < count; ++index) bytes[index] = 0U;
}

void copy_trimmed_ascii(
    char* destination,
    size_t destination_capacity,
    const uint8_t* source,
    size_t source_length) {
    if (destination == nullptr || destination_capacity == 0U) return;
    size_t end = source_length;
    while (end != 0U && (source[end - 1U] == ' ' || source[end - 1U] == 0U)) {
        --end;
    }
    size_t count = end;
    if (count + 1U > destination_capacity) count = destination_capacity - 1U;
    for (size_t index = 0U; index < count; ++index) {
        const uint8_t value = source[index];
        destination[index] =
            value >= UINT8_C(0x20) && value <= UINT8_C(0x7E)
            ? static_cast<char>(value)
            : '?';
    }
    destination[count] = '\0';
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

uint32_t read32(size_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(
        g_controller.registers + offset);
}

uint64_t read64(size_t offset) {
    const uint32_t low = read32(offset);
    const uint32_t high = read32(offset + 4U);
    return static_cast<uint64_t>(low) |
        static_cast<uint64_t>(high) << 32U;
}

void write32(size_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(
        g_controller.registers + offset) = value;
}

void write64(size_t offset, uint64_t value) {
    write32(offset, static_cast<uint32_t>(value));
    write32(offset + 4U, static_cast<uint32_t>(value >> 32U));
}

bool wait_ready(bool expected) {
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        const uint32_t status = read32(REG_CSTS);
        if ((status & CSTS_FATAL) != 0U) return false;
        if (((status & CSTS_READY) != 0U) == expected) return true;
        relax();
    }
    return false;
}

bool map_mmio(uint64_t physical_address) {
    if ((physical_address & (memory::virtual_memory::PAGE_SIZE - 1U)) != 0U) {
        return false;
    }
    auto* space = memory::kernel_virtual_memory::address_space();
    if (space == nullptr) return false;

    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    for (size_t index = 0U; index < MMIO_PAGE_COUNT; ++index) {
        const uint64_t virtual_address =
            MMIO_VIRTUAL_BASE +
            static_cast<uint64_t>(index) * memory::virtual_memory::PAGE_SIZE;
        memory::virtual_memory::Mapping existing{};
        if (memory::virtual_memory::query_page(
                space, virtual_address, &existing) !=
                memory::virtual_memory::Status::NotMapped ||
            memory::virtual_memory::map_page(
                space,
                virtual_address,
                physical_address +
                    static_cast<uint64_t>(index) *
                        memory::virtual_memory::PAGE_SIZE,
                flags) != memory::virtual_memory::Status::Ok) {
            while (g_controller.mapped_pages != 0U) {
                --g_controller.mapped_pages;
                static_cast<void>(memory::virtual_memory::unmap_page(
                    space,
                    MMIO_VIRTUAL_BASE +
                        static_cast<uint64_t>(g_controller.mapped_pages) *
                            memory::virtual_memory::PAGE_SIZE));
            }
            return false;
        }
        ++g_controller.mapped_pages;
    }
    g_controller.registers =
        reinterpret_cast<volatile uint8_t*>(MMIO_VIRTUAL_BASE);
    return true;
}

bool unmap_mmio() {
    if (g_controller.mapped_pages == 0U) return true;
    auto* space = memory::kernel_virtual_memory::address_space();
    if (space == nullptr) return false;
    bool ok = true;
    while (g_controller.mapped_pages != 0U) {
        --g_controller.mapped_pages;
        if (memory::virtual_memory::unmap_page(
                space,
                MMIO_VIRTUAL_BASE +
                    static_cast<uint64_t>(g_controller.mapped_pages) *
                        memory::virtual_memory::PAGE_SIZE) !=
            memory::virtual_memory::Status::Ok) {
            ok = false;
        }
    }
    g_controller.registers = nullptr;
    return ok;
}

bool release_dma_page_owned(dma::Page* page) {
    return page == nullptr || !page->allocated ||
        dma::release_page(page) == dma::Status::Ok;
}

Status release_resources(bool restore_pci) {
    if (g_controller.registers != nullptr && g_controller.controller_enabled) {
        write32(REG_CC, read32(REG_CC) & ~CC_ENABLE);
        static_cast<void>(wait_ready(false));
        g_controller.controller_enabled = false;
    }

    bool ok = true;
    if (!release_dma_page_owned(&g_controller.identify_page)) ok = false;
    if (!release_dma_page_owned(&g_controller.admin_completion_queue)) ok = false;
    if (!release_dma_page_owned(&g_controller.admin_submission_queue)) ok = false;
    if (!unmap_mmio()) ok = false;

    if (restore_pci && g_controller.pci_enabled) {
        pci::write16(
            g_controller.pci_device,
            PCI_COMMAND_OFFSET,
            g_controller.original_pci_command);
        if (pci::read16(g_controller.pci_device, PCI_COMMAND_OFFSET) !=
            g_controller.original_pci_command) {
            ok = false;
        }
        g_controller.pci_enabled = false;
    }
    return ok ? Status::Ok : Status::ResourceReleaseFailed;
}

uint16_t next_command_id() {
    ++g_controller.next_command_id;
    if (g_controller.next_command_id == 0U) ++g_controller.next_command_id;
    return g_controller.next_command_id;
}

bool submit_admin(
    const protocol::Command& command,
    uint16_t command_id,
    protocol::Completion* output) {
    if (output == nullptr || g_controller.queue_entries < 2U) return false;

    auto* submissions = static_cast<protocol::Command*>(
        g_controller.admin_submission_queue.virtual_address);
    submissions[g_controller.submission_tail] = command;
    write_barrier();

    ++g_controller.submission_tail;
    if (g_controller.submission_tail == g_controller.queue_entries) {
        g_controller.submission_tail = 0U;
    }
    write32(
        REG_DOORBELL_BASE,
        static_cast<uint32_t>(g_controller.submission_tail));

    auto* completions = static_cast<volatile uint32_t*>(
        g_controller.admin_completion_queue.virtual_address);
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        const size_t base =
            static_cast<size_t>(g_controller.completion_head) *
            protocol::COMPLETION_DWORDS;
        const uint32_t status_word = completions[base + 3U];
        const bool phase =
            ((status_word >> 16U) & UINT32_C(1)) != 0U;
        if (phase != g_controller.completion_phase) {
            relax();
            continue;
        }

        read_barrier();
        uint32_t words[protocol::COMPLETION_DWORDS]{};
        for (size_t index = 0U;
             index < protocol::COMPLETION_DWORDS;
             ++index) {
            words[index] = completions[base + index];
        }
        protocol::Completion completion{};
        if (protocol::parse_completion(
                words,
                command_id,
                g_controller.completion_phase,
                &completion) != protocol::Status::Ok) {
            return false;
        }

        ++g_controller.completion_head;
        if (g_controller.completion_head == g_controller.queue_entries) {
            g_controller.completion_head = 0U;
            g_controller.completion_phase =
                !g_controller.completion_phase;
        }
        write32(
            REG_DOORBELL_BASE +
                g_controller.capabilities.doorbell_stride_bytes,
            static_cast<uint32_t>(g_controller.completion_head));
        *output = completion;
        return completion.success;
    }
    return false;
}

Status enable_controller() {
    const uint64_t cap_raw = read64(REG_CAP);
    if (protocol::decode_capabilities(
            cap_raw, &g_controller.capabilities) != protocol::Status::Ok ||
        g_controller.bar.size < MMIO_REQUIRED_BYTES ||
        g_controller.capabilities.doorbell_stride_bytes >
            MMIO_REQUIRED_BYTES - REG_DOORBELL_BASE - sizeof(uint32_t)) {
        return Status::UnsupportedController;
    }

    if ((read32(REG_CC) & CC_ENABLE) != 0U) {
        write32(REG_CC, read32(REG_CC) & ~CC_ENABLE);
        if (!wait_ready(false)) return Status::ControllerResetTimeout;
    }

    const uint32_t maximum_entries =
        g_controller.capabilities.maximum_queue_entries;
    g_controller.queue_entries = static_cast<uint16_t>(
        maximum_entries < DESIRED_ADMIN_QUEUE_ENTRIES
        ? maximum_entries
        : DESIRED_ADMIN_QUEUE_ENTRIES);
    uint32_t aqa = 0U;
    uint32_t cc = 0U;
    if (protocol::build_admin_queue_attributes(
            g_controller.capabilities,
            g_controller.queue_entries,
            &aqa) != protocol::Status::Ok ||
        protocol::build_controller_configuration(
            g_controller.capabilities,
            static_cast<uint32_t>(memory::virtual_memory::PAGE_SIZE),
            &cc) != protocol::Status::Ok) {
        return Status::UnsupportedController;
    }

    if (dma::allocate_page(
            true, &g_controller.admin_submission_queue) != dma::Status::Ok ||
        dma::allocate_page(
            true, &g_controller.admin_completion_queue) != dma::Status::Ok ||
        dma::allocate_page(
            true, &g_controller.identify_page) != dma::Status::Ok) {
        return Status::DmaAllocationFailed;
    }
    clear_bytes(
        g_controller.admin_submission_queue.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    clear_bytes(
        g_controller.admin_completion_queue.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    clear_bytes(
        g_controller.identify_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);

    write32(REG_AQA, aqa);
    write64(
        REG_ASQ,
        g_controller.admin_submission_queue.physical_address);
    write64(
        REG_ACQ,
        g_controller.admin_completion_queue.physical_address);

    g_controller.submission_tail = 0U;
    g_controller.completion_head = 0U;
    g_controller.completion_phase = true;
    g_controller.next_command_id = 0U;

    write32(REG_CC, cc);
    g_controller.controller_enabled = true;
    if (!wait_ready(true)) return Status::ControllerStartTimeout;
    return Status::Ok;
}

Status identify_controller() {
    const uint16_t command_id = next_command_id();
    protocol::Command command{};
    if (protocol::build_identify_controller(
            command_id,
            g_controller.identify_page.physical_address,
            &command) != protocol::Status::Ok) {
        return Status::IdentifyInvalid;
    }

    protocol::Completion completion{};
    if (!submit_admin(command, command_id, &completion)) {
        return Status::AdminCommandFailed;
    }

    const auto* bytes = static_cast<const uint8_t*>(
        g_controller.identify_page.virtual_address);
    const uint16_t vendor =
        static_cast<uint16_t>(bytes[0U]) |
        static_cast<uint16_t>(bytes[1U]) << 8U;
    if (vendor == 0U || vendor == UINT16_MAX) {
        return Status::IdentifyInvalid;
    }

    g_controller.info.vendor_id = vendor;
    copy_trimmed_ascii(
        g_controller.info.serial,
        sizeof(g_controller.info.serial),
        bytes + 4U,
        20U);
    copy_trimmed_ascii(
        g_controller.info.model,
        sizeof(g_controller.info.model),
        bytes + 24U,
        40U);
    if (g_controller.info.model[0] == '\0') {
        return Status::IdentifyInvalid;
    }
    return Status::Ok;
}

} // namespace

Status initialize() {
    if (g_controller.ready) return Status::AlreadyInitialized;

    const pci::Device* source = nullptr;
    for (size_t index = 0U; index < pci::device_count(); ++index) {
        const pci::Device* candidate = pci::device_at(index);
        if (candidate != nullptr &&
            candidate->class_code == PCI_CLASS_MASS_STORAGE &&
            candidate->subclass == PCI_SUBCLASS_NVM &&
            candidate->programming_interface == PCI_PROG_IF_NVME) {
            source = candidate;
            break;
        }
    }
    if (source == nullptr) return Status::NoController;

    g_controller = {};
    g_controller.pci_device = *source;
    g_controller.original_pci_command =
        pci::read16(*source, PCI_COMMAND_OFFSET);

    pci::write16(
        *source,
        PCI_COMMAND_OFFSET,
        static_cast<uint16_t>(
            g_controller.original_pci_command &
            ~pci::bar::COMMAND_DECODE_MASK));
    if ((pci::read16(*source, PCI_COMMAND_OFFSET) &
         pci::bar::COMMAND_DECODE_MASK) != 0U) {
        pci::write16(
            *source, PCI_COMMAND_OFFSET,
            g_controller.original_pci_command);
        return Status::PciCommandRejected;
    }

    const auto bar_status =
        pci::bar::probe_disabled(*source, 0U, &g_controller.bar);
    if (bar_status != pci::bar::Status::Ok ||
        g_controller.bar.kind == pci::bar::Kind::Io ||
        g_controller.bar.size < MMIO_REQUIRED_BYTES) {
        pci::write16(
            *source, PCI_COMMAND_OFFSET,
            g_controller.original_pci_command);
        return Status::BarUnavailable;
    }

    const uint16_t enabled_command = static_cast<uint16_t>(
        g_controller.original_pci_command |
        PCI_COMMAND_MEMORY_SPACE |
        PCI_COMMAND_BUS_MASTER);
    pci::write16(*source, PCI_COMMAND_OFFSET, enabled_command);
    if ((pci::read16(*source, PCI_COMMAND_OFFSET) &
         (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) !=
        (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) {
        pci::write16(
            *source, PCI_COMMAND_OFFSET,
            g_controller.original_pci_command);
        return Status::PciCommandRejected;
    }
    g_controller.pci_enabled = true;

    if (!map_mmio(g_controller.bar.physical_address)) {
        static_cast<void>(release_resources(true));
        g_controller = {};
        return Status::MmioMappingFailed;
    }

    g_controller.info.pci_bus = source->address.bus;
    g_controller.info.pci_slot = source->address.slot;
    g_controller.info.pci_function = source->address.function;
    g_controller.info.device_id = source->device_id;
    g_controller.info.version = read32(REG_VS);

    Status status = enable_controller();
    if (status == Status::Ok) status = identify_controller();
    if (status != Status::Ok) {
        const Status cleanup = release_resources(true);
        g_controller = {};
        return cleanup == Status::Ok ? status : cleanup;
    }

    g_controller.info.admin_queue_entries = g_controller.queue_entries;
    g_controller.ready = true;
    return Status::Ok;
}

void shutdown() {
    static_cast<void>(release_resources(true));
    g_controller = {};
}

bool initialized() { return g_controller.ready; }

const ControllerInfo* controller_info() {
    return g_controller.ready ? &g_controller.info : nullptr;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "NVMe already initialized";
        case Status::NoController: return "no PCI NVMe controller";
        case Status::UnsupportedController:
            return "unsupported NVMe controller capabilities";
        case Status::PciCommandRejected:
            return "NVMe PCI command programming rejected";
        case Status::BarUnavailable: return "NVMe BAR0 unavailable";
        case Status::MmioMappingFailed: return "NVMe MMIO mapping failed";
        case Status::DmaAllocationFailed:
            return "NVMe admin DMA allocation failed";
        case Status::ControllerResetTimeout:
            return "NVMe controller reset timeout";
        case Status::ControllerStartTimeout:
            return "NVMe controller start timeout";
        case Status::AdminCommandTimeout:
            return "NVMe admin command timeout";
        case Status::AdminCommandFailed:
            return "NVMe admin command failed";
        case Status::IdentifyInvalid:
            return "NVMe Identify Controller data invalid";
        case Status::ResourceReleaseFailed:
            return "NVMe resource release failed";
    }
    return "unknown NVMe status";
}

} // namespace storage::nvme
