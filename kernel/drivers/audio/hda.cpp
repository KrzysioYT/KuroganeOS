#include "hda.hpp"

#include "hda_protocol.hpp"
#include "../pci.hpp"
#include "../pci_bar.hpp"
#include "../../memory/kernel_virtual_memory.hpp"
#include "../../memory/virtual_memory.hpp"
#include "../../terminal.hpp"

namespace drivers::audio::hda {
namespace {

constexpr uint8_t PCI_CLASS_MULTIMEDIA = UINT8_C(0x04);
constexpr uint8_t PCI_SUBCLASS_HDA = UINT8_C(0x03);
constexpr uint8_t PCI_COMMAND_OFFSET = UINT8_C(0x04);
constexpr uint16_t PCI_COMMAND_MEMORY_SPACE = UINT16_C(1) << 1U;
constexpr uint16_t PCI_COMMAND_BUS_MASTER = UINT16_C(1) << 2U;

constexpr uint64_t MMIO_VIRTUAL_BASE = UINT64_C(0xFFFFB60000000000);
constexpr size_t MMIO_PAGE_COUNT = 1U;
constexpr size_t MMIO_REQUIRED_BYTES =
    MMIO_PAGE_COUNT * memory::virtual_memory::PAGE_SIZE;

constexpr size_t REG_GCAP = 0x00U;
constexpr size_t REG_VMIN = 0x02U;
constexpr size_t REG_VMAJ = 0x03U;
constexpr size_t REG_GCTL = 0x08U;
constexpr size_t REG_STATESTS = 0x0EU;
constexpr size_t REG_ICOI = 0x60U;
constexpr size_t REG_ICII = 0x64U;
constexpr size_t REG_ICIS = 0x68U;

constexpr uint32_t GCTL_CRST = UINT32_C(1);
constexpr uint16_t ICIS_ICB = UINT16_C(1);
constexpr uint16_t ICIS_IRV = UINT16_C(1) << 1U;
constexpr uint32_t POLL_BUDGET = UINT32_C(4000000);

constexpr uint16_t VERB_GET_PARAMETER = UINT16_C(0xF00);
constexpr uint8_t PARAM_VENDOR_ID = UINT8_C(0x00);
constexpr uint8_t PARAM_SUBORDINATE_NODE_COUNT = UINT8_C(0x04);

struct Controller {
    pci::Device pci_device;
    pci::bar::Info bar;
    volatile uint8_t* registers;
    uint16_t original_pci_command;
    size_t mapped_pages;
    bool pci_enabled;
    bool ready;
    ControllerInfo info;
};

Controller g_controller{};

void relax() {
    __asm__ volatile("pause" : : : "memory");
}

uint8_t read8(size_t offset) {
    return *(g_controller.registers + offset);
}

uint16_t read16(size_t offset) {
    return *reinterpret_cast<const volatile uint16_t*>(
        g_controller.registers + offset);
}

uint32_t read32(size_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(
        g_controller.registers + offset);
}

void write16(size_t offset, uint16_t value) {
    *reinterpret_cast<volatile uint16_t*>(
        g_controller.registers + offset) = value;
}

void write32(size_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(
        g_controller.registers + offset) = value;
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

Status release_resources(bool restore_pci) {
    bool ok = unmap_mmio();
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

bool wait_reset_state(bool running) {
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        if (((read32(REG_GCTL) & GCTL_CRST) != 0U) == running) {
            return true;
        }
        relax();
    }
    return false;
}

Status reset_controller() {
    write32(REG_GCTL, read32(REG_GCTL) & ~GCTL_CRST);
    if (!wait_reset_state(false)) return Status::ControllerResetTimeout;

    write32(REG_GCTL, read32(REG_GCTL) | GCTL_CRST);
    if (!wait_reset_state(true)) return Status::ControllerResetTimeout;

    // The codec link becomes visible asynchronously after CRST. Do not assume
    // codec address zero and do not fabricate presence from PCI discovery.
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        if ((read16(REG_STATESTS) & UINT16_C(0x7FFF)) != 0U) {
            terminal::println("[TEST] hda_controller_reset: PASS");
            return Status::Ok;
        }
        relax();
    }
    return Status::NoCodec;
}

bool immediate_command(uint32_t command, uint32_t* output) {
    if (output == nullptr) return false;

    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        if ((read16(REG_ICIS) & ICIS_ICB) == 0U) break;
        if (attempt + 1U == POLL_BUDGET) return false;
        relax();
    }

    // IRV is write-one-to-clear. Clear a stale result before publishing a new
    // immediate command, then set ICB to hand ownership to the controller.
    write16(REG_ICIS, ICIS_IRV);
    write32(REG_ICOI, command);
    __asm__ volatile("sfence" : : : "memory");
    write16(REG_ICIS, ICIS_ICB);

    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        const uint16_t status = read16(REG_ICIS);
        if ((status & ICIS_ICB) == 0U && (status & ICIS_IRV) != 0U) {
            __asm__ volatile("lfence" : : : "memory");
            *output = read32(REG_ICII);
            write16(REG_ICIS, ICIS_IRV);
            return true;
        }
        relax();
    }
    return false;
}

Status get_parameter(
    uint8_t codec_address,
    uint8_t node_id,
    uint8_t parameter,
    uint32_t* output) {
    uint32_t command = 0U;
    if (protocol::build_verb_12(
            codec_address,
            node_id,
            VERB_GET_PARAMETER,
            parameter,
            &command) != protocol::Status::Ok ||
        !immediate_command(command, output)) {
        return Status::ImmediateCommandTimeout;
    }
    return Status::Ok;
}

Status identify_codec() {
    const uint16_t present =
        static_cast<uint16_t>(read16(REG_STATESTS) & UINT16_C(0x7FFF));
    if (present == 0U) return Status::NoCodec;

    uint8_t codec_address = 0U;
    while (codec_address < 15U &&
           (present & (UINT16_C(1) << codec_address)) == 0U) {
        ++codec_address;
    }
    if (codec_address >= 15U) return Status::NoCodec;

    uint32_t vendor = 0U;
    Status status = get_parameter(
        codec_address, 0U, PARAM_VENDOR_ID, &vendor);
    if (status != Status::Ok) return status;
    if (vendor == 0U || vendor == UINT32_MAX) {
        return Status::InvalidCodecResponse;
    }

    uint32_t nodes = 0U;
    status = get_parameter(
        codec_address, 0U, PARAM_SUBORDINATE_NODE_COUNT, &nodes);
    if (status != Status::Ok) return status;

    const uint8_t start_node =
        static_cast<uint8_t>((nodes >> 16U) & UINT32_C(0xFF));
    const uint8_t node_count =
        static_cast<uint8_t>(nodes & UINT32_C(0xFF));
    if (node_count == 0U ||
        static_cast<uint16_t>(start_node) + node_count > 256U) {
        return Status::InvalidCodecResponse;
    }

    g_controller.info.codec_address = codec_address;
    g_controller.info.codec_vendor_id = vendor;
    g_controller.info.root_start_node = start_node;
    g_controller.info.root_node_count = node_count;
    terminal::println("[TEST] hda_codec_identify: PASS");
    return Status::Ok;
}

} // namespace

Status initialize() {
    if (g_controller.ready) return Status::AlreadyInitialized;

    const pci::Device* source = nullptr;
    for (size_t index = 0U; index < pci::device_count(); ++index) {
        const pci::Device* candidate = pci::device_at(index);
        if (candidate != nullptr &&
            candidate->class_code == PCI_CLASS_MULTIMEDIA &&
            candidate->subclass == PCI_SUBCLASS_HDA) {
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

    protocol::ControllerCapabilities capabilities{};
    if (protocol::decode_global_capabilities(
            read16(REG_GCAP), &capabilities) != protocol::Status::Ok ||
        capabilities.output_stream_count == 0U) {
        static_cast<void>(release_resources(true));
        g_controller = {};
        return Status::UnsupportedController;
    }

    g_controller.info.pci_bus = source->address.bus;
    g_controller.info.pci_slot = source->address.slot;
    g_controller.info.pci_function = source->address.function;
    g_controller.info.vendor_id = source->vendor_id;
    g_controller.info.device_id = source->device_id;
    g_controller.info.major_version = read8(REG_VMAJ);
    g_controller.info.minor_version = read8(REG_VMIN);
    g_controller.info.output_stream_count = capabilities.output_stream_count;
    g_controller.info.input_stream_count = capabilities.input_stream_count;
    g_controller.info.bidirectional_stream_count =
        capabilities.bidirectional_stream_count;

    Status status = reset_controller();
    if (status == Status::Ok) status = identify_codec();
    if (status != Status::Ok) {
        const Status cleanup = release_resources(true);
        g_controller = {};
        return cleanup == Status::Ok ? status : cleanup;
    }

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
        case Status::AlreadyInitialized: return "Intel HDA already initialized";
        case Status::NoController: return "no PCI Intel HDA-class controller";
        case Status::UnsupportedController:
            return "unsupported Intel HDA controller capabilities";
        case Status::PciCommandRejected:
            return "Intel HDA PCI command programming rejected";
        case Status::BarUnavailable: return "Intel HDA BAR0 unavailable";
        case Status::MmioMappingFailed: return "Intel HDA MMIO mapping failed";
        case Status::ControllerResetTimeout:
            return "Intel HDA controller reset timeout";
        case Status::NoCodec: return "Intel HDA codec not present";
        case Status::ImmediateCommandTimeout:
            return "Intel HDA immediate command timeout";
        case Status::InvalidCodecResponse:
            return "Intel HDA codec returned invalid parameters";
        case Status::ResourceReleaseFailed:
            return "Intel HDA resource release failed";
    }
    return "unknown Intel HDA status";
}

} // namespace drivers::audio::hda
