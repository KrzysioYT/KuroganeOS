#include "hda.hpp"

#include "hda_protocol.hpp"
#include "../pci.hpp"
#include "../pci_bar.hpp"
#include "../../memory/kernel_virtual_memory.hpp"
#include "../../memory/virtual_memory.hpp"
#include "../../storage/dma.hpp"
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
constexpr size_t REG_STREAM_BASE = 0x80U;
constexpr size_t STREAM_STRIDE = 0x20U;
constexpr size_t SD_CTL0 = 0x00U;
constexpr size_t SD_CTL2 = 0x02U;
constexpr size_t SD_STS = 0x03U;
constexpr size_t SD_LPIB = 0x04U;
constexpr size_t SD_CBL = 0x08U;
constexpr size_t SD_LVI = 0x0CU;
constexpr size_t SD_FMT = 0x12U;
constexpr size_t SD_BDPL = 0x18U;
constexpr size_t SD_BDPU = 0x1CU;

constexpr uint32_t GCTL_CRST = UINT32_C(1);
constexpr uint16_t ICIS_ICB = UINT16_C(1);
constexpr uint16_t ICIS_IRV = UINT16_C(1) << 1U;
constexpr uint8_t SD_CTL_SRST = UINT8_C(1);
constexpr uint8_t SD_CTL_RUN = UINT8_C(1) << 1U;
constexpr uint8_t SD_STS_BCIS = UINT8_C(1) << 2U;
constexpr uint8_t SD_STS_FIFOE = UINT8_C(1) << 3U;
constexpr uint8_t SD_STS_DESE = UINT8_C(1) << 4U;
constexpr uint8_t SD_STS_W1C = SD_STS_BCIS | SD_STS_FIFOE | SD_STS_DESE;
constexpr uint32_t POLL_BUDGET = UINT32_C(8000000);

constexpr uint16_t VERB_GET_PARAMETER = UINT16_C(0xF00);
constexpr uint16_t VERB_SET_CONNECTION_SELECT = UINT16_C(0x701);
constexpr uint16_t VERB_SET_POWER_STATE = UINT16_C(0x705);
constexpr uint16_t VERB_SET_STREAM_CHANNEL = UINT16_C(0x706);
constexpr uint16_t VERB_SET_PIN_WIDGET_CONTROL = UINT16_C(0x707);
constexpr uint8_t VERB4_SET_CONVERTER_FORMAT = UINT8_C(0x2);
constexpr uint8_t PARAM_VENDOR_ID = UINT8_C(0x00);
constexpr uint8_t PARAM_SUBORDINATE_NODE_COUNT = UINT8_C(0x04);
constexpr uint8_t PARAM_FUNCTION_GROUP_TYPE = UINT8_C(0x05);
constexpr uint8_t PARAM_AUDIO_WIDGET_CAPS = UINT8_C(0x09);
constexpr uint8_t FUNCTION_GROUP_AUDIO = UINT8_C(0x01);
constexpr uint8_t WIDGET_AUDIO_OUTPUT = UINT8_C(0x00);
constexpr uint8_t WIDGET_PIN_COMPLEX = UINT8_C(0x04);
constexpr uint8_t PIN_WIDGET_OUT_ENABLE = UINT8_C(0x40);

constexpr uint32_t SAMPLE_RATE = 48000U;
constexpr uint8_t CHANNELS = 2U;
constexpr uint8_t BITS_PER_SAMPLE = 16U;
constexpr size_t BYTES_PER_FRAME = sizeof(int16_t) * CHANNELS;
constexpr size_t MAXIMUM_FRAMES =
    memory::virtual_memory::PAGE_SIZE / BYTES_PER_FRAME;
constexpr uint8_t STREAM_TAG = UINT8_C(1);

struct __attribute__((packed)) BufferDescriptor {
    uint64_t address;
    uint32_t length;
    uint32_t flags;
};

static_assert(sizeof(BufferDescriptor) == 16U, "HDA BDL entry ABI");

struct Controller {
    pci::Device pci_device;
    pci::bar::Info bar;
    volatile uint8_t* registers;
    protocol::ControllerCapabilities capabilities;
    storage::dma::Page bdl_page;
    storage::dma::Page pcm_page;
    uint16_t original_pci_command;
    uint16_t pcm_format;
    size_t mapped_pages;
    size_t output_stream_offset;
    uint32_t active_bytes;
    uint32_t volume_percent;
    bool muted;
    bool pci_enabled;
    bool busy;
    bool ready;
    ControllerInfo info;
};

Controller g_controller{};

void relax() {
    __asm__ volatile("pause" : : : "memory");
}

void clear_bytes(void* destination, size_t count) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < count; ++index) bytes[index] = 0U;
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

void write8(size_t offset, uint8_t value) {
    *(g_controller.registers + offset) = value;
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

bool release_dma_page(storage::dma::Page* page) {
    return !page->allocated ||
        storage::dma::release_page(page) == storage::dma::Status::Ok;
}

bool wait_stream_bit(uint8_t mask, bool set) {
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        if (((read8(g_controller.output_stream_offset + SD_CTL0) & mask) != 0U) ==
            set) {
            return true;
        }
        relax();
    }
    return false;
}

bool reset_output_stream() {
    const size_t base = g_controller.output_stream_offset;
    write8(base + SD_CTL0, static_cast<uint8_t>(
        read8(base + SD_CTL0) & ~SD_CTL_RUN));
    if (!wait_stream_bit(SD_CTL_RUN, false)) return false;

    write8(base + SD_CTL0, static_cast<uint8_t>(
        read8(base + SD_CTL0) | SD_CTL_SRST));
    if (!wait_stream_bit(SD_CTL_SRST, true)) return false;
    write8(base + SD_CTL0, static_cast<uint8_t>(
        read8(base + SD_CTL0) & ~SD_CTL_SRST));
    if (!wait_stream_bit(SD_CTL_SRST, false)) return false;
    write8(base + SD_STS, SD_STS_W1C);
    return true;
}

void stop_output_stream_raw() {
    if (g_controller.registers == nullptr ||
        g_controller.output_stream_offset == 0U) {
        g_controller.busy = false;
        return;
    }
    const size_t base = g_controller.output_stream_offset;
    write8(base + SD_CTL0, static_cast<uint8_t>(
        read8(base + SD_CTL0) & ~SD_CTL_RUN));
    static_cast<void>(wait_stream_bit(SD_CTL_RUN, false));
    write8(base + SD_STS, SD_STS_W1C);
    g_controller.busy = false;
    g_controller.active_bytes = 0U;
}

Status release_resources(bool restore_pci) {
    if (g_controller.registers != nullptr) stop_output_stream_raw();
    bool ok = release_dma_page(&g_controller.pcm_page);
    if (!release_dma_page(&g_controller.bdl_page)) ok = false;
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

bool wait_reset_state(bool running) {
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        if (((read32(REG_GCTL) & GCTL_CRST) != 0U) == running) return true;
        relax();
    }
    return false;
}

Status reset_controller() {
    write32(REG_GCTL, read32(REG_GCTL) & ~GCTL_CRST);
    if (!wait_reset_state(false)) return Status::ControllerResetTimeout;
    write32(REG_GCTL, read32(REG_GCTL) | GCTL_CRST);
    if (!wait_reset_state(true)) return Status::ControllerResetTimeout;

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

Status verb12(
    uint8_t node_id,
    uint16_t verb,
    uint8_t payload,
    uint32_t* output) {
    uint32_t command = 0U;
    if (protocol::build_verb_12(
            g_controller.info.codec_address, node_id, verb, payload,
            &command) != protocol::Status::Ok ||
        !immediate_command(command, output)) {
        return Status::ImmediateCommandTimeout;
    }
    return Status::Ok;
}

Status verb4(
    uint8_t node_id,
    uint8_t verb,
    uint16_t payload,
    uint32_t* output) {
    uint32_t command = 0U;
    if (protocol::build_verb_4(
            g_controller.info.codec_address, node_id, verb, payload,
            &command) != protocol::Status::Ok ||
        !immediate_command(command, output)) {
        return Status::ImmediateCommandTimeout;
    }
    return Status::Ok;
}

Status get_parameter(uint8_t node_id, uint8_t parameter, uint32_t* output) {
    return verb12(node_id, VERB_GET_PARAMETER, parameter, output);
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
    g_controller.info.codec_address = codec_address;

    uint32_t vendor = 0U;
    Status status = get_parameter(0U, PARAM_VENDOR_ID, &vendor);
    if (status != Status::Ok) return status;
    if (vendor == 0U || vendor == UINT32_MAX) {
        return Status::InvalidCodecResponse;
    }

    uint32_t nodes = 0U;
    status = get_parameter(0U, PARAM_SUBORDINATE_NODE_COUNT, &nodes);
    if (status != Status::Ok) return status;
    const uint8_t start_node =
        static_cast<uint8_t>((nodes >> 16U) & UINT32_C(0xFF));
    const uint8_t node_count =
        static_cast<uint8_t>(nodes & UINT32_C(0xFF));
    if (node_count == 0U ||
        static_cast<uint16_t>(start_node) + node_count > 256U) {
        return Status::InvalidCodecResponse;
    }

    g_controller.info.codec_vendor_id = vendor;
    g_controller.info.root_start_node = start_node;
    g_controller.info.root_node_count = node_count;
    terminal::println("[TEST] hda_codec_identify: PASS");
    return Status::Ok;
}

Status configure_codec_path() {
    uint8_t function_group = 0U;
    for (uint16_t node = g_controller.info.root_start_node;
         node < static_cast<uint16_t>(g_controller.info.root_start_node) +
                    g_controller.info.root_node_count;
         ++node) {
        uint32_t type = 0U;
        const Status status = get_parameter(
            static_cast<uint8_t>(node), PARAM_FUNCTION_GROUP_TYPE, &type);
        if (status != Status::Ok) return status;
        if ((type & UINT32_C(0xFF)) == FUNCTION_GROUP_AUDIO) {
            function_group = static_cast<uint8_t>(node);
            break;
        }
    }
    if (function_group == 0U) return Status::UnsupportedPcmPath;

    uint32_t response = 0U;
    Status status = verb12(
        function_group, VERB_SET_POWER_STATE, 0U, &response);
    if (status != Status::Ok) return status;

    uint32_t nodes = 0U;
    status = get_parameter(
        function_group, PARAM_SUBORDINATE_NODE_COUNT, &nodes);
    if (status != Status::Ok) return status;
    const uint8_t start_node =
        static_cast<uint8_t>((nodes >> 16U) & UINT32_C(0xFF));
    const uint8_t node_count =
        static_cast<uint8_t>(nodes & UINT32_C(0xFF));
    if (node_count == 0U ||
        static_cast<uint16_t>(start_node) + node_count > 256U) {
        return Status::InvalidCodecResponse;
    }

    uint8_t converter = 0U;
    uint8_t pin = 0U;
    for (uint16_t node = start_node;
         node < static_cast<uint16_t>(start_node) + node_count;
         ++node) {
        uint32_t caps = 0U;
        status = get_parameter(
            static_cast<uint8_t>(node), PARAM_AUDIO_WIDGET_CAPS, &caps);
        if (status != Status::Ok) return status;
        const uint8_t type =
            static_cast<uint8_t>((caps >> 20U) & UINT32_C(0x0F));
        if (converter == 0U && type == WIDGET_AUDIO_OUTPUT) {
            converter = static_cast<uint8_t>(node);
        } else if (pin == 0U && type == WIDGET_PIN_COMPLEX) {
            pin = static_cast<uint8_t>(node);
        }
    }
    if (converter == 0U || pin == 0U) return Status::UnsupportedPcmPath;

    status = verb12(converter, VERB_SET_POWER_STATE, 0U, &response);
    if (status != Status::Ok) return status;
    status = verb12(pin, VERB_SET_POWER_STATE, 0U, &response);
    if (status != Status::Ok) return status;

    uint16_t format = 0U;
    if (protocol::build_pcm_format(
            SAMPLE_RATE, BITS_PER_SAMPLE, CHANNELS, &format) !=
        protocol::Status::Ok) {
        return Status::UnsupportedPcmPath;
    }
    status = verb4(
        converter, VERB4_SET_CONVERTER_FORMAT, format, &response);
    if (status != Status::Ok) return status;
    status = verb12(
        converter,
        VERB_SET_STREAM_CHANNEL,
        static_cast<uint8_t>(STREAM_TAG << 4U),
        &response);
    if (status != Status::Ok) return status;

    // Steel's first HDA target is a simple output path. Select the first
    // advertised pin connection and enable output; physical codec routing is
    // deliberately re-qualified per machine during the 5.0 manual matrix.
    status = verb12(pin, VERB_SET_CONNECTION_SELECT, 0U, &response);
    if (status != Status::Ok) return status;
    status = verb12(
        pin, VERB_SET_PIN_WIDGET_CONTROL, PIN_WIDGET_OUT_ENABLE, &response);
    if (status != Status::Ok) return status;

    g_controller.pcm_format = format;
    g_controller.info.audio_function_group_node = function_group;
    g_controller.info.output_converter_node = converter;
    g_controller.info.output_pin_node = pin;
    g_controller.info.stream_tag = STREAM_TAG;
    terminal::println("[TEST] hda_pcm_path: PASS");
    return Status::Ok;
}

Status allocate_pcm_dma() {
    if (storage::dma::allocate_page(
            g_controller.capabilities.supports_64_bit_addressing,
            &g_controller.bdl_page) != storage::dma::Status::Ok ||
        storage::dma::allocate_page(
            g_controller.capabilities.supports_64_bit_addressing,
            &g_controller.pcm_page) != storage::dma::Status::Ok) {
        return Status::DmaAllocationFailed;
    }
    clear_bytes(
        g_controller.bdl_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    clear_bytes(
        g_controller.pcm_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);

    g_controller.output_stream_offset =
        REG_STREAM_BASE +
        static_cast<size_t>(g_controller.capabilities.input_stream_count) *
            STREAM_STRIDE;
    if (g_controller.output_stream_offset + STREAM_STRIDE >
        MMIO_REQUIRED_BYTES) {
        return Status::UnsupportedController;
    }
    if (!reset_output_stream()) return Status::DeviceFault;
    return Status::Ok;
}

Status program_stream(uint32_t bytes) {
    if (bytes == 0U || bytes > memory::virtual_memory::PAGE_SIZE ||
        (bytes & 1U) != 0U) {
        return Status::InvalidArgument;
    }
    protocol::BufferDescriptor descriptor{};
    if (protocol::build_buffer_descriptor(
            g_controller.pcm_page.physical_address,
            bytes,
            true,
            &descriptor) != protocol::Status::Ok) {
        return Status::DmaAllocationFailed;
    }

    auto* bdl =
        static_cast<BufferDescriptor*>(g_controller.bdl_page.virtual_address);
    clear_bytes(
        g_controller.bdl_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    bdl[0] = {descriptor.address, descriptor.length, descriptor.flags};
    __asm__ volatile("sfence" : : : "memory");

    const size_t base = g_controller.output_stream_offset;
    if (!reset_output_stream()) return Status::DeviceFault;
    write8(base + SD_CTL2, static_cast<uint8_t>(
        (read8(base + SD_CTL2) & UINT8_C(0x0F)) |
        static_cast<uint8_t>(STREAM_TAG << 4U)));
    write32(base + SD_CBL, bytes);
    write16(base + SD_LVI, 0U);
    write16(base + SD_FMT, g_controller.pcm_format);
    write32(
        base + SD_BDPL,
        static_cast<uint32_t>(g_controller.bdl_page.physical_address));
    write32(
        base + SD_BDPU,
        static_cast<uint32_t>(g_controller.bdl_page.physical_address >> 32U));
    write8(base + SD_STS, SD_STS_W1C);
    __asm__ volatile("sfence" : : : "memory");
    write8(base + SD_CTL0, static_cast<uint8_t>(
        read8(base + SD_CTL0) | SD_CTL_RUN));
    if (!wait_stream_bit(SD_CTL_RUN, true)) return Status::DeviceFault;
    g_controller.active_bytes = bytes;
    g_controller.busy = true;
    return Status::Ok;
}

Status poll_stream() {
    if (!g_controller.busy) return Status::Ok;
    const size_t base = g_controller.output_stream_offset;
    const uint8_t status = read8(base + SD_STS);
    if ((status & (SD_STS_DESE | SD_STS_FIFOE)) != 0U) {
        write8(base + SD_STS, SD_STS_W1C);
        stop_output_stream_raw();
        return Status::DeviceFault;
    }
    if ((status & SD_STS_BCIS) != 0U ||
        (g_controller.active_bytes != 0U &&
         read32(base + SD_LPIB) >= g_controller.active_bytes)) {
        write8(base + SD_STS, SD_STS_W1C);
        stop_output_stream_raw();
    }
    return Status::Ok;
}

Status qualify_pcm_dma() {
    clear_bytes(
        g_controller.pcm_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    constexpr uint32_t self_test_bytes = 1024U;
    Status status = program_stream(self_test_bytes);
    if (status != Status::Ok) {
        terminal::println("[TEST] hda_pcm_dma: FAIL");
        return status;
    }

    bool progressed = false;
    const size_t base = g_controller.output_stream_offset;
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        const uint8_t stream_status = read8(base + SD_STS);
        if ((stream_status & (SD_STS_DESE | SD_STS_FIFOE)) != 0U) {
            stop_output_stream_raw();
            terminal::println("[TEST] hda_pcm_dma: FAIL");
            return Status::DeviceFault;
        }
        const uint32_t position = read32(base + SD_LPIB);
        if (position != 0U || (stream_status & SD_STS_BCIS) != 0U) {
            progressed = true;
            break;
        }
        relax();
    }
    stop_output_stream_raw();
    if (!reset_output_stream() || !progressed) {
        terminal::println("[TEST] hda_pcm_dma: FAIL");
        return Status::DeviceFault;
    }
    terminal::println("[TEST] hda_pcm_dma: PASS");
    return Status::Ok;
}

Status copy_pcm_samples(const int16_t* samples, size_t frame_count) {
    if (samples == nullptr || frame_count == 0U) return Status::InvalidArgument;
    if (frame_count > MAXIMUM_FRAMES) return Status::BufferTooLarge;

    auto* output = static_cast<int16_t*>(g_controller.pcm_page.virtual_address);
    const size_t sample_count = frame_count * CHANNELS;
    for (size_t index = 0U; index < sample_count; ++index) {
        if (g_controller.muted || g_controller.volume_percent == 0U) {
            output[index] = 0;
        } else {
            const int32_t scaled =
                static_cast<int32_t>(samples[index]) *
                static_cast<int32_t>(g_controller.volume_percent) /
                100;
            output[index] = static_cast<int16_t>(scaled);
        }
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
            candidate->class_code == PCI_CLASS_MULTIMEDIA &&
            candidate->subclass == PCI_SUBCLASS_HDA) {
            source = candidate;
            break;
        }
    }
    if (source == nullptr) return Status::NoController;

    g_controller = {};
    g_controller.volume_percent = 100U;
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

    if (protocol::decode_global_capabilities(
            read16(REG_GCAP), &g_controller.capabilities) !=
            protocol::Status::Ok ||
        g_controller.capabilities.output_stream_count == 0U) {
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
    g_controller.info.output_stream_count =
        g_controller.capabilities.output_stream_count;
    g_controller.info.input_stream_count =
        g_controller.capabilities.input_stream_count;
    g_controller.info.bidirectional_stream_count =
        g_controller.capabilities.bidirectional_stream_count;

    Status status = reset_controller();
    if (status == Status::Ok) status = identify_codec();
    if (status == Status::Ok) status = configure_codec_path();
    if (status == Status::Ok) status = allocate_pcm_dma();
    if (status == Status::Ok) status = qualify_pcm_dma();
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

Capabilities capabilities() {
    return {
        SAMPLE_RATE,
        CHANNELS,
        BITS_PER_SAMPLE,
        MAXIMUM_FRAMES,
    };
}

Status set_master_volume(uint32_t percent, bool muted_value) {
    if (!g_controller.ready) return Status::NotInitialized;
    if (percent > 100U) return Status::InvalidArgument;
    g_controller.volume_percent = percent;
    g_controller.muted = muted_value;
    return Status::Ok;
}

uint32_t master_volume_percent() { return g_controller.volume_percent; }
bool muted() { return g_controller.muted; }

Status play_pcm16_stereo(const int16_t* samples, size_t frame_count) {
    if (!g_controller.ready) return Status::NotInitialized;
    if (g_controller.busy) {
        const Status status = poll_stream();
        if (status != Status::Ok) return status;
        if (g_controller.busy) return Status::DeviceBusy;
    }
    Status status = copy_pcm_samples(samples, frame_count);
    if (status != Status::Ok) return status;
    return program_stream(static_cast<uint32_t>(frame_count * BYTES_PER_FRAME));
}

Status poll() {
    if (!g_controller.ready) return Status::NotInitialized;
    return poll_stream();
}

bool busy() { return g_controller.busy; }

Status stop() {
    if (!g_controller.ready) return Status::NotInitialized;
    stop_output_stream_raw();
    if (!reset_output_stream()) return Status::DeviceFault;
    return Status::Ok;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "Intel HDA already initialized";
        case Status::NotInitialized: return "Intel HDA not initialized";
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
        case Status::UnsupportedPcmPath:
            return "Intel HDA codec has no supported PCM output path";
        case Status::DmaAllocationFailed:
            return "Intel HDA PCM DMA allocation failed";
        case Status::InvalidArgument: return "invalid Intel HDA argument";
        case Status::BufferTooLarge: return "Intel HDA PCM buffer too large";
        case Status::DeviceBusy: return "Intel HDA PCM engine is busy";
        case Status::DeviceFault: return "Intel HDA PCM device fault";
        case Status::ResourceReleaseFailed:
            return "Intel HDA resource release failed";
    }
    return "unknown Intel HDA status";
}

} // namespace drivers::audio::hda
