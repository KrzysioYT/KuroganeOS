#include "xhci.hpp"

#include "protocol.hpp"
#include "../../core/log.hpp"
#include "../../input/input.hpp"
#include "../../memory/kernel_virtual_memory.hpp"
#include "../../memory/virtual_memory.hpp"
#include "../../storage/dma.hpp"
#include "../../terminal.hpp"

namespace drivers::usb::xhci {
namespace {

constexpr uint64_t MMIO_VIRTUAL_BASE = UINT64_C(0xFFFFB20000000000);
constexpr size_t MMIO_BYTES = 64U * 1024U;
constexpr size_t RING_TRB_COUNT = 256U;
constexpr size_t USABLE_RING_TRBS = RING_TRB_COUNT - 1U;
constexpr size_t MAXIMUM_SCRATCHPADS = 32U;
constexpr uint32_t POLL_BUDGET = 2000000U;

constexpr size_t CAP_HCSPARAMS1 = 0x04U;
constexpr size_t CAP_HCSPARAMS2 = 0x08U;
constexpr size_t CAP_HCCPARAMS1 = 0x10U;
constexpr size_t CAP_DBOFF = 0x14U;
constexpr size_t CAP_RTSOFF = 0x18U;

constexpr size_t OP_USBCMD = 0x00U;
constexpr size_t OP_USBSTS = 0x04U;
constexpr size_t OP_PAGESIZE = 0x08U;
constexpr size_t OP_CRCR = 0x18U;
constexpr size_t OP_DCBAAP = 0x30U;
constexpr size_t OP_CONFIG = 0x38U;
constexpr size_t OP_PORTS = 0x400U;
constexpr size_t PORT_STRIDE = 0x10U;

constexpr uint32_t CMD_RUN = UINT32_C(1) << 0U;
constexpr uint32_t CMD_RESET = UINT32_C(1) << 1U;
constexpr uint32_t STS_HALTED = UINT32_C(1) << 0U;
constexpr uint32_t STS_NOT_READY = UINT32_C(1) << 11U;
constexpr uint32_t PORT_CONNECTED = UINT32_C(1) << 0U;
constexpr uint32_t PORT_ENABLED = UINT32_C(1) << 1U;
constexpr uint32_t PORT_RESET = UINT32_C(1) << 4U;
constexpr uint32_t PORT_POWER = UINT32_C(1) << 9U;

constexpr uint8_t TRB_NORMAL = 1U;
constexpr uint8_t TRB_SETUP_STAGE = 2U;
constexpr uint8_t TRB_DATA_STAGE = 3U;
constexpr uint8_t TRB_STATUS_STAGE = 4U;
constexpr uint8_t TRB_LINK = 6U;
constexpr uint8_t TRB_ENABLE_SLOT = 9U;
constexpr uint8_t TRB_ADDRESS_DEVICE = 11U;
constexpr uint8_t TRB_CONFIGURE_ENDPOINT = 12U;
constexpr uint8_t TRB_EVALUATE_CONTEXT = 13U;
constexpr uint8_t TRB_TRANSFER_EVENT = 32U;
constexpr uint8_t TRB_COMMAND_COMPLETION = 33U;

constexpr uint8_t COMPLETION_SUCCESS = 1U;
constexpr uint8_t COMPLETION_SHORT_PACKET = 13U;

struct alignas(16) Trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};

struct alignas(16) ErstEntry {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t reserved;
};

static_assert(sizeof(Trb) == 16U, "xHCI TRB ABI");
static_assert(sizeof(ErstEntry) == 16U, "xHCI ERST ABI");

struct ProducerRing {
    storage::dma::Page page;
    size_t enqueue;
    bool cycle;
};

struct Controller {
    pci::Device pci_device;
    volatile uint8_t* registers;
    volatile uint8_t* operational;
    volatile uint8_t* runtime;
    volatile uint32_t* doorbells;
    uintptr_t mapped_base;
    size_t mapped_pages;
    size_t context_size;
    uint8_t maximum_slots;
    uint8_t maximum_ports;
    uint8_t slot_id;
    uint8_t port_id;
    uint8_t port_speed;
    uint8_t interrupt_dci;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t ep0_packet_size;
    uint16_t interrupt_packet_size;
    device::DeviceId parent_device;
    device::DriverId owner_driver;
    device::DeviceId keyboard_device;
    storage::dma::Page dcbaa_page;
    storage::dma::Page event_ring_page;
    storage::dma::Page erst_page;
    storage::dma::Page input_context_page;
    storage::dma::Page device_context_page;
    storage::dma::Page data_page;
    storage::dma::Page scratchpad_array_page;
    storage::dma::Page scratchpads[MAXIMUM_SCRATCHPADS];
    size_t scratchpad_count;
    ProducerRing command_ring;
    ProducerRing ep0_ring;
    ProducerRing interrupt_ring;
    size_t event_dequeue;
    bool event_cycle;
    HidBootKeyboardInterface keyboard_interface;
    KeyboardDecoder keyboard_decoder;
    bool report_queued;
    bool input_proven;
    bool initialized;
    uint64_t reports;
};

Controller g_controller{};

void clear_bytes(void* destination, size_t count) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < count; ++index) bytes[index] = 0U;
}

void copy_bytes(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t index = 0U; index < count; ++index) output[index] = input[index];
}

void relax() { __asm__ volatile("pause" : : : "memory"); }
void barrier() { __asm__ volatile("mfence" : : : "memory"); }

uint8_t read8(const volatile uint8_t* base, size_t offset) {
    return base[offset];
}

uint32_t read32(const volatile uint8_t* base, size_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(base + offset);
}

void write32(volatile uint8_t* base, size_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(base + offset) = value;
}

void write64(volatile uint8_t* base, size_t offset, uint64_t value) {
    // xHCI's split 64-bit MMIO registers latch the pair when the high dword
    // is written. Program the low dword first while the controller is stopped.
    write32(base, offset, static_cast<uint32_t>(value));
    write32(base, offset + 4U, static_cast<uint32_t>(value >> 32U));
}

Trb* trbs(ProducerRing& ring) {
    return static_cast<Trb*>(ring.page.virtual_address);
}

uint8_t trb_type(const Trb& trb) {
    return static_cast<uint8_t>((trb.control >> 10U) & 0x3FU);
}

uint8_t completion_code(const Trb& trb) {
    return static_cast<uint8_t>(trb.status >> 24U);
}

bool completion_ok(const Trb& trb) {
    const uint8_t code = completion_code(trb);
    return code == COMPLETION_SUCCESS || code == COMPLETION_SHORT_PACKET;
}

bool allocate_page(storage::dma::Page* page) {
    if (page == nullptr ||
        storage::dma::allocate_page(true, page) != storage::dma::Status::Ok) {
        return false;
    }
    clear_bytes(page->virtual_address, memory::virtual_memory::PAGE_SIZE);
    return true;
}

bool initialize_ring(ProducerRing* ring) {
    if (ring == nullptr || !allocate_page(&ring->page)) return false;
    ring->enqueue = 0U;
    ring->cycle = true;
    Trb& link = trbs(*ring)[USABLE_RING_TRBS];
    link.parameter = ring->page.physical_address;
    link.status = 0U;
    link.control = static_cast<uint32_t>(TRB_LINK) << 10U |
        UINT32_C(1) << 1U | UINT32_C(1);
    return true;
}

uint64_t enqueue_trb(
    ProducerRing& ring,
    uint64_t parameter,
    uint32_t status,
    uint32_t control) {
    if (ring.enqueue >= USABLE_RING_TRBS) {
        Trb& link = trbs(ring)[USABLE_RING_TRBS];
        link.control = static_cast<uint32_t>(TRB_LINK) << 10U |
            UINT32_C(1) << 1U | (ring.cycle ? 1U : 0U);
        barrier();
        ring.enqueue = 0U;
        ring.cycle = !ring.cycle;
    }
    const size_t index = ring.enqueue++;
    Trb& trb = trbs(ring)[index];
    trb.parameter = parameter;
    trb.status = status;
    trb.control = control | (ring.cycle ? 1U : 0U);
    barrier();
    return ring.page.physical_address + index * sizeof(Trb);
}

bool map_mmio(uint64_t physical, Controller* controller) {
    if (controller == nullptr) return false;
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) return false;
    constexpr uint64_t page_size = memory::virtual_memory::PAGE_SIZE;
    constexpr uint64_t page_mask = page_size - 1U;
    const uint64_t aligned = physical & ~page_mask;
    const uint64_t offset = physical & page_mask;
    const size_t page_count = static_cast<size_t>(
        (offset + MMIO_BYTES + page_mask) / page_size);
    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    size_t mapped = 0U;
    for (; mapped < page_count; ++mapped) {
        memory::virtual_memory::Mapping existing{};
        if (memory::virtual_memory::query_page(
                address_space, MMIO_VIRTUAL_BASE + mapped * page_size,
                &existing) != memory::virtual_memory::Status::NotMapped ||
            memory::virtual_memory::map_page(
                address_space, MMIO_VIRTUAL_BASE + mapped * page_size,
                aligned + mapped * page_size, flags) !=
                memory::virtual_memory::Status::Ok) {
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
    controller->mapped_base = MMIO_VIRTUAL_BASE;
    controller->mapped_pages = page_count;
    controller->registers = reinterpret_cast<volatile uint8_t*>(
        MMIO_VIRTUAL_BASE + offset);
    return true;
}

void release_dma_page(storage::dma::Page* page) {
    if (page != nullptr && page->allocated) {
        static_cast<void>(storage::dma::release_page(page));
    }
}

void release_resources(Controller* controller) {
    if (controller == nullptr) return;
    if (controller->operational != nullptr) {
        write32(controller->operational, OP_USBCMD,
                read32(controller->operational, OP_USBCMD) & ~CMD_RUN);
    }
    release_dma_page(&controller->command_ring.page);
    release_dma_page(&controller->ep0_ring.page);
    release_dma_page(&controller->interrupt_ring.page);
    release_dma_page(&controller->dcbaa_page);
    release_dma_page(&controller->event_ring_page);
    release_dma_page(&controller->erst_page);
    release_dma_page(&controller->input_context_page);
    release_dma_page(&controller->device_context_page);
    release_dma_page(&controller->data_page);
    release_dma_page(&controller->scratchpad_array_page);
    for (size_t index = 0U; index < MAXIMUM_SCRATCHPADS; ++index) {
        release_dma_page(&controller->scratchpads[index]);
    }
    auto* space = memory::kernel_virtual_memory::address_space();
    if (space != nullptr) {
        for (size_t index = 0U; index < controller->mapped_pages; ++index) {
            static_cast<void>(memory::virtual_memory::unmap_page(
                space, controller->mapped_base +
                    index * memory::virtual_memory::PAGE_SIZE));
        }
    }
    *controller = {};
    controller->keyboard_device = device::INVALID_DEVICE_ID;
}

bool take_ownership(Controller& controller, uint32_t hccparams) {
    uint32_t offset = ((hccparams >> 16U) & 0xFFFFU) * 4U;
    for (size_t count = 0U; offset != 0U && count < 64U; ++count) {
        if (offset + 4U > MMIO_BYTES) return false;
        const uint32_t header = read32(controller.registers, offset);
        const uint8_t id = static_cast<uint8_t>(header & 0xFFU);
        const uint8_t next = static_cast<uint8_t>((header >> 8U) & 0xFFU);
        if (id == 1U) {
            write32(controller.registers, offset, header | (UINT32_C(1) << 24U));
            for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
                if ((read32(controller.registers, offset) &
                     (UINT32_C(1) << 16U)) == 0U) {
                    return true;
                }
                relax();
            }
            return false;
        }
        if (next == 0U) break;
        offset += static_cast<uint32_t>(next) * 4U;
    }
    return true;
}

bool reset_controller(Controller& controller) {
    uint32_t command = read32(controller.operational, OP_USBCMD);
    write32(controller.operational, OP_USBCMD, command & ~CMD_RUN);
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        if ((read32(controller.operational, OP_USBSTS) & STS_HALTED) != 0U) {
            break;
        }
        if (attempt + 1U == POLL_BUDGET) return false;
        relax();
    }
    command = read32(controller.operational, OP_USBCMD);
    write32(controller.operational, OP_USBCMD, command | CMD_RESET);
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        const uint32_t current = read32(controller.operational, OP_USBCMD);
        const uint32_t status = read32(controller.operational, OP_USBSTS);
        if ((current & CMD_RESET) == 0U && (status & STS_NOT_READY) == 0U) {
            return true;
        }
        relax();
    }
    return false;
}

bool allocate_controller_memory(Controller& controller, uint32_t hcsparams2) {
    if (!allocate_page(&controller.dcbaa_page) ||
        !initialize_ring(&controller.command_ring) ||
        !allocate_page(&controller.event_ring_page) ||
        !allocate_page(&controller.erst_page) ||
        !allocate_page(&controller.input_context_page) ||
        !allocate_page(&controller.device_context_page) ||
        !initialize_ring(&controller.ep0_ring) ||
        !initialize_ring(&controller.interrupt_ring) ||
        !allocate_page(&controller.data_page)) {
        return false;
    }
    controller.scratchpad_count =
        static_cast<size_t>((hcsparams2 >> 27U) & 0x1FU) |
        static_cast<size_t>((hcsparams2 >> 21U) & 0x1FU) << 5U;
    if (controller.scratchpad_count > MAXIMUM_SCRATCHPADS) return false;
    if (controller.scratchpad_count != 0U) {
        if (!allocate_page(&controller.scratchpad_array_page)) return false;
        auto* pointers = static_cast<uint64_t*>(
            controller.scratchpad_array_page.virtual_address);
        for (size_t index = 0U; index < controller.scratchpad_count; ++index) {
            if (!allocate_page(&controller.scratchpads[index])) return false;
            pointers[index] = controller.scratchpads[index].physical_address;
        }
        static_cast<uint64_t*>(controller.dcbaa_page.virtual_address)[0] =
            controller.scratchpad_array_page.physical_address;
    }
    return true;
}

bool configure_controller(Controller& controller) {
    if ((read32(controller.operational, OP_PAGESIZE) & 1U) == 0U) return false;
    auto* erst = static_cast<ErstEntry*>(controller.erst_page.virtual_address);
    erst[0] = {
        controller.event_ring_page.physical_address,
        static_cast<uint32_t>(RING_TRB_COUNT),
        0U,
    };
    controller.event_dequeue = 0U;
    controller.event_cycle = true;
    volatile uint8_t* interrupter = controller.runtime + 0x20U;
    write32(interrupter, 0x00U, 0U);
    write32(interrupter, 0x08U, 1U);
    write64(interrupter, 0x10U, controller.erst_page.physical_address);
    write64(interrupter, 0x18U, controller.event_ring_page.physical_address);
    write64(controller.operational, OP_DCBAAP,
            controller.dcbaa_page.physical_address);
    write64(controller.operational, OP_CRCR,
            controller.command_ring.page.physical_address | 1U);
    write32(controller.operational, OP_CONFIG, controller.maximum_slots);
    write32(controller.operational, OP_USBCMD,
            read32(controller.operational, OP_USBCMD) | CMD_RUN);
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        if ((read32(controller.operational, OP_USBSTS) & STS_HALTED) == 0U) {
            return true;
        }
        relax();
    }
    return false;
}

bool next_event(Controller& controller, Trb* output) {
    auto* events = static_cast<volatile Trb*>(
        controller.event_ring_page.virtual_address);
    const volatile Trb& source = events[controller.event_dequeue];
    if ((source.control & 1U) != (controller.event_cycle ? 1U : 0U)) {
        return false;
    }
    output->parameter = source.parameter;
    output->status = source.status;
    output->control = source.control;
    ++controller.event_dequeue;
    if (controller.event_dequeue == RING_TRB_COUNT) {
        controller.event_dequeue = 0U;
        controller.event_cycle = !controller.event_cycle;
    }
    const uint64_t dequeue = controller.event_ring_page.physical_address +
        controller.event_dequeue * sizeof(Trb);
    write64(controller.runtime + 0x20U, 0x18U, dequeue | UINT64_C(8));
    return true;
}

bool wait_event(
    Controller& controller,
    uint8_t expected_type,
    uint64_t expected_parameter,
    Trb* output) {
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        Trb event{};
        if (!next_event(controller, &event)) {
            relax();
            continue;
        }
        if (trb_type(event) == expected_type &&
            (expected_parameter == 0U ||
             (event.parameter & ~UINT64_C(0xF)) ==
                (expected_parameter & ~UINT64_C(0xF)))) {
            if (output != nullptr) *output = event;
            return true;
        }
    }
    return false;
}

bool submit_command(
    Controller& controller,
    uint64_t parameter,
    uint32_t status,
    uint32_t control,
    Trb* completion) {
    const uint64_t address = enqueue_trb(
        controller.command_ring, parameter, status, control);
    controller.doorbells[0] = 0U;
    if (!wait_event(controller, TRB_COMMAND_COMPLETION, address, completion)) {
        log::write(log::Level::Warn, "XHCI", "command completion timeout");
        return false;
    }
    if (!completion_ok(*completion)) {
        log::write_u64(
            log::Level::Warn, "XHCI", "command completion code=",
            completion_code(*completion));
    }
    return completion_ok(*completion);
}

bool reset_connected_port(Controller& controller) {
    for (uint8_t port = 1U; port <= controller.maximum_ports; ++port) {
        const size_t offset = OP_PORTS +
            static_cast<size_t>(port - 1U) * PORT_STRIDE;
        uint32_t status = read32(controller.operational, offset);
        if ((status & PORT_CONNECTED) == 0U) continue;
        write32(controller.operational, offset, PORT_POWER | PORT_RESET);
        for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
            status = read32(controller.operational, offset);
            if ((status & PORT_CONNECTED) != 0U &&
                (status & PORT_RESET) == 0U &&
                (status & PORT_ENABLED) != 0U) {
                controller.port_id = port;
                controller.port_speed = static_cast<uint8_t>(
                    (status >> 10U) & 0x0FU);
                return controller.port_speed != 0U;
            }
            relax();
        }
        return false;
    }
    return false;
}

uint32_t* input_context(Controller& controller, size_t index) {
    return reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(controller.input_context_page.virtual_address) +
        index * controller.context_size);
}

uint32_t* output_context(Controller& controller, size_t index) {
    return reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(controller.device_context_page.virtual_address) +
        index * controller.context_size);
}

uint16_t initial_packet_size(uint8_t speed) {
    if (speed == 4U) return 512U;
    if (speed == 3U) return 64U;
    return 8U;
}

bool address_device(Controller& controller) {
    Trb completion{};
    if (!submit_command(
            controller, 0U, 0U,
            static_cast<uint32_t>(TRB_ENABLE_SLOT) << 10U,
            &completion)) {
        return false;
    }
    log::write(log::Level::Info, "XHCI", "Enable Slot completed");
    controller.slot_id = static_cast<uint8_t>(completion.control >> 24U);
    if (controller.slot_id == 0U ||
        controller.slot_id > controller.maximum_slots) return false;
    static_cast<uint64_t*>(controller.dcbaa_page.virtual_address)
        [controller.slot_id] = controller.device_context_page.physical_address;

    clear_bytes(controller.input_context_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
    input_context(controller, 0U)[1U] = 3U;
    uint32_t* slot = input_context(controller, 1U);
    slot[0U] = static_cast<uint32_t>(controller.port_speed) << 20U |
        UINT32_C(1) << 27U;
    slot[1U] = static_cast<uint32_t>(controller.port_id) << 16U;
    controller.ep0_packet_size = initial_packet_size(controller.port_speed);
    uint32_t* ep0 = input_context(controller, 2U);
    ep0[1U] = UINT32_C(3) << 1U | UINT32_C(4) << 3U |
        static_cast<uint32_t>(controller.ep0_packet_size) << 16U;
    ep0[2U] = static_cast<uint32_t>(
        controller.ep0_ring.page.physical_address) | 1U;
    ep0[3U] = static_cast<uint32_t>(
        controller.ep0_ring.page.physical_address >> 32U);
    ep0[4U] = 8U;
    const bool addressed = submit_command(
        controller,
        controller.input_context_page.physical_address,
        0U,
        static_cast<uint32_t>(TRB_ADDRESS_DEVICE) << 10U |
            static_cast<uint32_t>(controller.slot_id) << 24U,
        &completion);
    if (addressed) {
        log::write(log::Level::Info, "XHCI", "Address Device completed");
    }
    return addressed;
}

uint64_t setup_packet(
    uint8_t request_type,
    uint8_t request,
    uint16_t value,
    uint16_t index,
    uint16_t length) {
    return static_cast<uint64_t>(request_type) |
        static_cast<uint64_t>(request) << 8U |
        static_cast<uint64_t>(value) << 16U |
        static_cast<uint64_t>(index) << 32U |
        static_cast<uint64_t>(length) << 48U;
}

bool control_transfer(
    Controller& controller,
    uint8_t request_type,
    uint8_t request,
    uint16_t value,
    uint16_t index,
    uint16_t length,
    bool direction_in) {
    const uint32_t transfer_type = length == 0U
        ? 0U
        : (direction_in ? 3U : 2U);
    enqueue_trb(
        controller.ep0_ring,
        setup_packet(request_type, request, value, index, length),
        8U,
        static_cast<uint32_t>(TRB_SETUP_STAGE) << 10U |
            UINT32_C(1) << 6U | transfer_type << 16U);
    if (length != 0U) {
        enqueue_trb(
            controller.ep0_ring,
            controller.data_page.physical_address,
            length,
            static_cast<uint32_t>(TRB_DATA_STAGE) << 10U |
                (direction_in ? UINT32_C(1) << 16U : 0U));
    }
    const bool status_in = length == 0U || !direction_in;
    const uint64_t status_trb = enqueue_trb(
        controller.ep0_ring,
        0U,
        0U,
        static_cast<uint32_t>(TRB_STATUS_STAGE) << 10U |
            UINT32_C(1) << 5U |
            (status_in ? UINT32_C(1) << 16U : 0U));
    controller.doorbells[controller.slot_id] = 1U;
    Trb completion{};
    return wait_event(
               controller, TRB_TRANSFER_EVENT, status_trb, &completion) &&
        completion_ok(completion) &&
        static_cast<uint8_t>(completion.control >> 24U) == controller.slot_id;
}

bool update_ep0_packet_size(Controller& controller, uint16_t packet_size) {
    if (packet_size == controller.ep0_packet_size) return true;
    clear_bytes(controller.input_context_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
    input_context(controller, 0U)[1U] = UINT32_C(1) << 1U;
    copy_bytes(input_context(controller, 2U), output_context(controller, 1U),
               controller.context_size);
    input_context(controller, 2U)[1U] &= UINT32_C(0x0000FFFF);
    input_context(controller, 2U)[1U] |=
        static_cast<uint32_t>(packet_size) << 16U;
    Trb completion{};
    if (!submit_command(
            controller,
            controller.input_context_page.physical_address,
            0U,
            static_cast<uint32_t>(TRB_EVALUATE_CONTEXT) << 10U |
                static_cast<uint32_t>(controller.slot_id) << 24U,
            &completion)) {
        return false;
    }
    controller.ep0_packet_size = packet_size;
    return true;
}

bool read_descriptors(Controller& controller) {
    clear_bytes(controller.data_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
    if (!control_transfer(controller, 0x80U, 6U, 0x0100U, 0U, 8U, true)) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(
        controller.data_page.virtual_address);
    if (bytes[0U] < 18U || bytes[1U] != 1U) return false;
    uint16_t packet_size = bytes[7U];
    if (controller.port_speed == 4U) {
        if (packet_size > 9U) return false;
        packet_size = static_cast<uint16_t>(UINT16_C(1) << packet_size);
    }
    if (packet_size < 8U || packet_size > 512U ||
        !update_ep0_packet_size(controller, packet_size)) return false;
    clear_bytes(controller.data_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
    if (!control_transfer(controller, 0x80U, 6U, 0x0100U, 0U, 18U, true)) {
        return false;
    }
    bytes = static_cast<const uint8_t*>(controller.data_page.virtual_address);
    if (bytes[0U] < 18U || bytes[1U] != 1U) return false;
    controller.vendor_id = static_cast<uint16_t>(bytes[8U]) |
        static_cast<uint16_t>(bytes[9U]) << 8U;
    controller.product_id = static_cast<uint16_t>(bytes[10U]) |
        static_cast<uint16_t>(bytes[11U]) << 8U;

    clear_bytes(controller.data_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
    if (!control_transfer(controller, 0x80U, 6U, 0x0200U, 0U, 9U, true)) {
        return false;
    }
    bytes = static_cast<const uint8_t*>(controller.data_page.virtual_address);
    const uint16_t total = static_cast<uint16_t>(bytes[2U]) |
        static_cast<uint16_t>(bytes[3U]) << 8U;
    if (bytes[0U] < 9U || bytes[1U] != 2U || total < 9U || total > 512U) {
        return false;
    }
    clear_bytes(controller.data_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
    if (!control_transfer(controller, 0x80U, 6U, 0x0200U, 0U, total, true)) {
        return false;
    }
    return find_boot_keyboard_interface(
        static_cast<const uint8_t*>(controller.data_page.virtual_address),
        total,
        &controller.keyboard_interface);
}

uint8_t endpoint_interval(uint8_t speed, uint8_t requested) {
    if (requested == 0U) return 0U;
    if (speed == 1U || speed == 2U) {
        uint8_t exponent = 0U;
        uint8_t value = requested;
        while (value > 1U) {
            value >>= 1U;
            ++exponent;
        }
        return static_cast<uint8_t>(exponent + 3U);
    }
    return requested > 0U ? static_cast<uint8_t>(requested - 1U) : 0U;
}

bool configure_keyboard_endpoint(Controller& controller) {
    if (!control_transfer(
            controller, 0x00U, 9U,
            controller.keyboard_interface.configuration_value,
            0U, 0U, false)) {
        return false;
    }
    static_cast<void>(control_transfer(
        controller, 0x21U, 0x0BU, 0U,
        controller.keyboard_interface.interface_number, 0U, false));
    static_cast<void>(control_transfer(
        controller, 0x21U, 0x0AU, 0U,
        controller.keyboard_interface.interface_number, 0U, false));

    const uint8_t endpoint_number =
        controller.keyboard_interface.endpoint_address & 0x0FU;
    controller.interrupt_dci = static_cast<uint8_t>(
        endpoint_number * 2U + 1U);
    if (endpoint_number == 0U || controller.interrupt_dci >= 32U) return false;
    controller.interrupt_packet_size = 8U;
    clear_bytes(controller.input_context_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
    input_context(controller, 0U)[1U] = UINT32_C(1) |
        (UINT32_C(1) << controller.interrupt_dci);
    copy_bytes(input_context(controller, 1U), output_context(controller, 0U),
               controller.context_size);
    uint32_t* slot = input_context(controller, 1U);
    slot[0U] &= ~(UINT32_C(0x1F) << 27U);
    slot[0U] |= static_cast<uint32_t>(controller.interrupt_dci) << 27U;
    uint32_t* endpoint = input_context(
        controller, static_cast<size_t>(controller.interrupt_dci) + 1U);
    endpoint[0U] = static_cast<uint32_t>(endpoint_interval(
        controller.port_speed, controller.keyboard_interface.interval)) << 16U;
    endpoint[1U] = UINT32_C(3) << 1U | UINT32_C(7) << 3U |
        static_cast<uint32_t>(
            controller.keyboard_interface.maximum_packet_size) << 16U;
    endpoint[2U] = static_cast<uint32_t>(
        controller.interrupt_ring.page.physical_address) | 1U;
    endpoint[3U] = static_cast<uint32_t>(
        controller.interrupt_ring.page.physical_address >> 32U);
    endpoint[4U] = controller.interrupt_packet_size;
    Trb completion{};
    return submit_command(
        controller,
        controller.input_context_page.physical_address,
        0U,
        static_cast<uint32_t>(TRB_CONFIGURE_ENDPOINT) << 10U |
            static_cast<uint32_t>(controller.slot_id) << 24U,
        &completion);
}

bool queue_keyboard_report(Controller& controller) {
    if (controller.report_queued) return true;
    clear_bytes(controller.data_page.virtual_address, 8U);
    enqueue_trb(
        controller.interrupt_ring,
        controller.data_page.physical_address,
        controller.interrupt_packet_size,
        static_cast<uint32_t>(TRB_NORMAL) << 10U | UINT32_C(1) << 5U);
    controller.report_queued = true;
    controller.doorbells[controller.slot_id] = controller.interrupt_dci;
    return true;
}

bool register_keyboard(Controller& controller) {
    const device::Descriptor descriptor{
        device::Type::Input,
        device::Bus::Usb,
        "USB HID boot keyboard",
        controller.vendor_id,
        controller.product_id,
        3U, 1U, 1U,
        {0U, 0U, controller.port_id, 0U},
        controller.parent_device,
        nullptr,
        0U,
    };
    device::DeviceId id = device::INVALID_DEVICE_ID;
    if (device::register_device(descriptor, &id) != KStatus::Ok ||
        device::claim(id, controller.owner_driver, "usb-hid-boot") !=
            KStatus::Ok ||
        device::set_status(id, device::Status::Ready) != KStatus::Ok) {
        return false;
    }
    controller.keyboard_device = id;
    return true;
}

void handle_keyboard_report(Controller& controller, const Trb& event) {
    controller.report_queued = false;
    const uint32_t remaining = event.status & 0x00FFFFFFU;
    const size_t actual = remaining <= controller.interrupt_packet_size
        ? controller.interrupt_packet_size - remaining
        : 0U;
    if (completion_ok(event) && actual >= 8U) {
        keyboard::KeyEvent events[MAXIMUM_KEYBOARD_EVENTS_PER_REPORT]{};
        size_t count = 0U;
        if (decode_boot_keyboard_report(
                &controller.keyboard_decoder,
                static_cast<const uint8_t*>(
                    controller.data_page.virtual_address),
                actual,
                events,
                MAXIMUM_KEYBOARD_EVENTS_PER_REPORT,
                &count)) {
            ++controller.reports;
            for (size_t index = 0U; index < count; ++index) {
                static_cast<void>(input::submit_key(events[index]));
                if (!controller.input_proven && events[index].pressed) {
                    controller.input_proven = true;
                    log::write(
                        log::Level::Info,
                        "USB",
                        "hardware xHCI HID keyboard report received");
                    terminal::println("[TEST] usb_hid_keyboard_input: PASS");
                }
            }
        }
    }
    static_cast<void>(queue_keyboard_report(controller));
}

} // namespace

Status initialize(
    const pci::Device& pci_device,
    device::DeviceId parent_device,
    device::DriverId owner_driver) {
    if (g_controller.initialized) return Status::AlreadyInitialized;
    if (pci_device.class_code != 0x0CU || pci_device.subclass != 0x03U ||
        pci_device.programming_interface != 0x30U ||
        parent_device == device::INVALID_DEVICE_ID ||
        owner_driver == device::INVALID_DRIVER_ID) {
        return Status::InvalidArgument;
    }
    g_controller = {};
    g_controller.keyboard_device = device::INVALID_DEVICE_ID;
    g_controller.pci_device = pci_device;
    g_controller.parent_device = parent_device;
    g_controller.owner_driver = owner_driver;
    bool is_io = false;
    const uint64_t bar = pci::bar_address(pci_device, 0U, &is_io);
    if (bar == 0U || is_io || !map_mmio(bar, &g_controller)) {
        release_resources(&g_controller);
        return Status::MmioUnavailable;
    }
    const uint8_t cap_length = read8(g_controller.registers, 0U);
    const uint32_t hcsparams1 = read32(
        g_controller.registers, CAP_HCSPARAMS1);
    const uint32_t hcsparams2 = read32(
        g_controller.registers, CAP_HCSPARAMS2);
    const uint32_t hccparams1 = read32(
        g_controller.registers, CAP_HCCPARAMS1);
    const uint32_t dboff = read32(g_controller.registers, CAP_DBOFF) & ~3U;
    const uint32_t rtsoff = read32(g_controller.registers, CAP_RTSOFF) & ~0x1FU;
    g_controller.maximum_slots = static_cast<uint8_t>(hcsparams1 & 0xFFU);
    g_controller.maximum_ports = static_cast<uint8_t>(hcsparams1 >> 24U);
    g_controller.context_size = (hccparams1 & (UINT32_C(1) << 2U)) != 0U
        ? 64U : 32U;
    if (cap_length < 0x20U || g_controller.maximum_slots == 0U ||
        g_controller.maximum_ports == 0U || dboff >= MMIO_BYTES ||
        rtsoff + 0x40U > MMIO_BYTES) {
        release_resources(&g_controller);
        return Status::UnsupportedController;
    }
    g_controller.operational = g_controller.registers + cap_length;
    g_controller.runtime = g_controller.registers + rtsoff;
    g_controller.doorbells = reinterpret_cast<volatile uint32_t*>(
        g_controller.registers + dboff);
    if (!take_ownership(g_controller, hccparams1)) {
        release_resources(&g_controller);
        return Status::BiosHandoffTimeout;
    }
    if (!reset_controller(g_controller)) {
        release_resources(&g_controller);
        return Status::ControllerResetTimeout;
    }
    if (!allocate_controller_memory(g_controller, hcsparams2)) {
        release_resources(&g_controller);
        return Status::DmaAllocationFailed;
    }
    pci::enable_bus_mastering(pci_device);
    if (!configure_controller(g_controller)) {
        release_resources(&g_controller);
        return Status::ControllerStartTimeout;
    }
    if (!reset_connected_port(g_controller)) {
        const bool any_connected = [&]() {
            for (uint8_t port = 1U; port <= g_controller.maximum_ports; ++port) {
                if ((read32(g_controller.operational, OP_PORTS +
                     static_cast<size_t>(port - 1U) * PORT_STRIDE) &
                     PORT_CONNECTED) != 0U) return true;
            }
            return false;
        }();
        release_resources(&g_controller);
        return any_connected ? Status::PortResetTimeout : Status::NoDevice;
    }
    log::write(log::Level::Info, "XHCI", "connected USB port reset completed");
    if (!address_device(g_controller)) {
        release_resources(&g_controller);
        return Status::CommandFailed;
    }
    if (!read_descriptors(g_controller)) {
        release_resources(&g_controller);
        return Status::HidKeyboardNotFound;
    }
    log::write(log::Level::Info, "XHCI", "USB HID descriptors accepted");
    if (!configure_keyboard_endpoint(g_controller)) {
        release_resources(&g_controller);
        return Status::CommandFailed;
    }
    reset_keyboard_decoder(&g_controller.keyboard_decoder);
    if (!register_keyboard(g_controller)) {
        release_resources(&g_controller);
        return Status::DeviceRegistrationFailed;
    }
    g_controller.initialized = true;
    static_cast<void>(queue_keyboard_report(g_controller));
    log::write(log::Level::Info, "USB", "xHCI USB HID boot keyboard ready");
    terminal::println("[TEST] xhci_keyboard_enumeration: PASS");
    return Status::Ok;
}

size_t poll(size_t budget) {
    if (!g_controller.initialized || budget == 0U) return 0U;
    size_t processed = 0U;
    while (processed < budget) {
        Trb event{};
        if (!next_event(g_controller, &event)) break;
        ++processed;
        if (trb_type(event) == TRB_TRANSFER_EVENT &&
            static_cast<uint8_t>(event.control >> 24U) == g_controller.slot_id &&
            static_cast<uint8_t>((event.control >> 16U) & 0x1FU) ==
                g_controller.interrupt_dci) {
            handle_keyboard_report(g_controller, event);
        }
    }
    return processed;
}

bool initialized() { return g_controller.initialized; }
bool keyboard_ready() {
    return g_controller.initialized &&
        g_controller.keyboard_device != device::INVALID_DEVICE_ID;
}
uint64_t reports_received() { return g_controller.reports; }

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::InvalidArgument: return "invalid xHCI device";
        case Status::UnsupportedController: return "unsupported xHCI capabilities";
        case Status::MmioUnavailable: return "xHCI MMIO unavailable";
        case Status::BiosHandoffTimeout: return "xHCI firmware handoff timeout";
        case Status::ControllerResetTimeout: return "xHCI reset timeout";
        case Status::DmaAllocationFailed: return "xHCI DMA allocation failed";
        case Status::ControllerStartTimeout: return "xHCI start timeout";
        case Status::NoDevice: return "no USB device connected";
        case Status::PortResetTimeout: return "USB port reset timeout";
        case Status::CommandFailed: return "xHCI command failed";
        case Status::TransferFailed: return "USB control transfer failed";
        case Status::DescriptorInvalid: return "invalid USB descriptor";
        case Status::HidKeyboardNotFound: return "USB HID boot keyboard not found";
        case Status::DeviceRegistrationFailed: return "USB device registration failed";
    }
    return "unknown xHCI status";
}

} // namespace drivers::usb::xhci
