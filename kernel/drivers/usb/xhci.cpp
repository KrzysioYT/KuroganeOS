#include "xhci.hpp"

#include "protocol.hpp"
#include "hid_report.hpp"
#include "hub_port.hpp"
#include "mass_storage_protocol.hpp"
#include "xhci_bulk.hpp"
#include "xhci_layout.hpp"
#include "xhci_route.hpp"
#include "../../arch/x86_64/hpet.hpp"
#include "../../core/log.hpp"
#include "../../input/input.hpp"
#include "../../memory/kernel_virtual_memory.hpp"
#include "../../memory/virtual_memory.hpp"
#include "../../storage/dma.hpp"
#include "../../storage/device_registry.hpp"
#include "../../storage/scratch_test.hpp"
#include "../../storage/block_device.hpp"
#include "../../terminal.hpp"

namespace drivers::usb::xhci {
namespace {

constexpr uint64_t MMIO_VIRTUAL_BASE = UINT64_C(0xFFFFB20000000000);
constexpr size_t MMIO_BYTES = 64U * 1024U;
constexpr size_t RING_TRB_COUNT = 256U;
constexpr size_t USABLE_RING_TRBS = RING_TRB_COUNT - 1U;
constexpr size_t MAXIMUM_SCRATCHPADS = 32U;
constexpr uint32_t POLL_BUDGET = 2000000U;
constexpr size_t MAXIMUM_DEFERRED_EVENTS = 32U;

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
constexpr uint32_t PORT_CONNECT_CHANGED = UINT32_C(1) << 17U;
constexpr uint8_t USB_HUB_CLASS = 0x09U;

constexpr uint8_t TRB_NORMAL = 1U;
constexpr uint8_t TRB_SETUP_STAGE = 2U;
constexpr uint8_t TRB_DATA_STAGE = 3U;
constexpr uint8_t TRB_STATUS_STAGE = 4U;
constexpr uint8_t TRB_LINK = 6U;
constexpr uint8_t TRB_ENABLE_SLOT = 9U;
constexpr uint8_t TRB_DISABLE_SLOT = 10U;
constexpr uint8_t TRB_ADDRESS_DEVICE = 11U;
constexpr uint8_t TRB_CONFIGURE_ENDPOINT = 12U;
constexpr uint8_t TRB_EVALUATE_CONTEXT = 13U;
constexpr uint8_t TRB_TRANSFER_EVENT = 32U;
constexpr uint8_t TRB_COMMAND_COMPLETION = 33U;
constexpr uint8_t TRB_PORT_STATUS_CHANGE = 34U;

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

enum class HidKind : uint8_t {
    None, Keyboard, Mouse, ReportPointer,
};

enum class HidLifecycle : uint8_t {
    Active, DrainInput, ReleaseInput, WaitingForDevice, Failed,
};

struct CompanionHid {
    uint8_t slot_id;
    uint8_t port_id;
    uint8_t port_speed;
    uint8_t interrupt_dci;
    uint8_t ignored_port_id;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t ep0_packet_size;
    uint16_t interrupt_packet_size;
    device::DeviceId child_device;
    storage::dma::Page input_context_page;
    storage::dma::Page device_context_page;
    storage::dma::Page data_page;
    ProducerRing ep0_ring;
    ProducerRing interrupt_ring;
    HidKind hid_kind;
    HidBootKeyboardInterface keyboard_interface;
    HidBootMouseInterface mouse_interface;
    HidReportInterface report_interface;
    hid::PointerReportLayout pointer_layout;
    KeyboardDecoder keyboard_decoder;
    MouseDecoder mouse_decoder;
    hid::PointerDecoder pointer_decoder;
    keyboard::KeyEvent pending_keys[MAXIMUM_KEYBOARD_EVENTS_PER_REPORT];
    size_t pending_key_count;
    size_t pending_key_index;
    mouse::Sample pending_mouse;
    bool pending_mouse_valid;
    hid::PointerReport pending_pointer;
    bool pending_pointer_valid;
    HidLifecycle hid_lifecycle;
    bool report_queued;
    uint64_t report_trb;
    bool input_proven;
    uint64_t reports;
    bool allocated;
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
    bool deferred_hub_ports[256U];
    bool hub_reset_attempted;
    uint8_t slot_id;
    uint8_t port_id;
    uint8_t port_speed;
    uint8_t interrupt_dci;
    uint8_t bulk_in_dci;
    uint8_t bulk_out_dci;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t ep0_packet_size;
    uint16_t interrupt_packet_size;
    device::DeviceId parent_device;
    device::DriverId owner_driver;
    device::DeviceId keyboard_device;
    device::DeviceId mouse_device;
    device::DeviceId mass_storage_device;
    storage::dma::Page dcbaa_page;
    storage::dma::Page event_ring_page;
    storage::dma::Page erst_page;
    storage::dma::Page input_context_page;
    storage::dma::Page device_context_page;
    storage::dma::Page data_page;
    storage::dma::Page storage_io_page;
    storage::dma::Page scratchpad_array_page;
    storage::dma::Page scratchpads[MAXIMUM_SCRATCHPADS];
    size_t scratchpad_count;
    ProducerRing command_ring;
    ProducerRing ep0_ring;
    ProducerRing interrupt_ring;
    ProducerRing bulk_in_ring;
    ProducerRing bulk_out_ring;
    size_t event_dequeue;
    bool event_cycle;
    HidKind hid_kind;
    HidBootKeyboardInterface keyboard_interface;
    HidBootMouseInterface mouse_interface;
    HidReportInterface report_interface;
    hid::PointerReportLayout pointer_layout;
    mass_storage::BulkOnlyInterface mass_storage_interface;
    bool mass_storage_present;
    uint32_t mass_storage_tag;
    uint32_t mass_storage_block_size;
    uint64_t mass_storage_block_count;
    storage::block::Device mass_storage_block;
    bool mass_storage_block_ready;
    KeyboardDecoder keyboard_decoder;
    MouseDecoder mouse_decoder;
    hid::PointerDecoder pointer_decoder;
    keyboard::KeyEvent pending_keys[MAXIMUM_KEYBOARD_EVENTS_PER_REPORT];
    size_t pending_key_count;
    size_t pending_key_index;
    mouse::Sample pending_mouse;
    bool pending_mouse_valid;
    hid::PointerReport pending_pointer;
    bool pending_pointer_valid;
    HidLifecycle hid_lifecycle;
    Status runtime_status;
    bool report_queued;
    uint64_t report_trb;
    bool input_proven;
    bool initialized;
    bool dma_published;
    bool bus_master_enabled;
    bool cleanup_pending;
    uint64_t reports;
    CompanionHid companion;
    Trb deferred_events[MAXIMUM_DEFERRED_EVENTS];
    size_t deferred_event_count;
    bool multi_hid_proven;
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

void reset_ring(ProducerRing* ring) {
    clear_bytes(ring->page.virtual_address, memory::virtual_memory::PAGE_SIZE);
    ring->enqueue = 0U;
    ring->cycle = true;
    Trb& link = trbs(*ring)[USABLE_RING_TRBS];
    link.parameter = ring->page.physical_address;
    link.status = 0U;
    link.control = static_cast<uint32_t>(TRB_LINK) << 10U |
        UINT32_C(1) << 1U | UINT32_C(1);
}

bool initialize_ring(ProducerRing* ring) {
    if (ring == nullptr || !allocate_page(&ring->page)) return false;
    reset_ring(ring);
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
    controller->mapped_base = MMIO_VIRTUAL_BASE;
    controller->mapped_pages = 0U;
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
        ++controller->mapped_pages;
    }
    if (mapped != page_count) {
        // The caller's retryable cleanup owns even an incomplete mapping.
        // Do not discard ownership if a rollback unmap itself fails.
        return false;
    }
    controller->registers = reinterpret_cast<volatile uint8_t*>(
        MMIO_VIRTUAL_BASE + offset);
    return true;
}

bool release_dma_page(storage::dma::Page* page) {
    if (page != nullptr && page->allocated) {
        return storage::dma::release_page(page) == storage::dma::Status::Ok;
    }
    return true;
}

Status quiesce_dma(Controller& controller) {
    bool halted = !controller.dma_published;
    if (controller.dma_published && controller.operational != nullptr) {
        const uint32_t command = read32(controller.operational, OP_USBCMD);
        if (command != UINT32_MAX) {
            write32(controller.operational, OP_USBCMD, command & ~CMD_RUN);
            for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
                const uint32_t status = read32(controller.operational, OP_USBSTS);
                if (status != UINT32_MAX && (status & STS_HALTED) != 0U) {
                    halted = true;
                    break;
                }
                relax();
            }
        }
    }
    // A stopped command bit alone does not acknowledge DMA completion.
    // Disable bus mastering as containment even when halt times out, but
    // retain every DMA page until the controller actually acknowledges halt.
    if (controller.bus_master_enabled) {
        const uint16_t command = pci::read16(controller.pci_device, 0x04U);
        if (command == UINT16_MAX) return Status::ResourceReleaseFailed;
        pci::write16(controller.pci_device, 0x04U,
                     static_cast<uint16_t>(command & ~UINT16_C(4)));
        const uint16_t readback = pci::read16(controller.pci_device, 0x04U);
        if (readback == UINT16_MAX || (readback & 4U) != 0U) {
            return Status::ResourceReleaseFailed;
        }
        controller.bus_master_enabled = false;
    }
    if (!halted) return Status::ControllerHaltTimeout;
    controller.dma_published = false;
    return Status::Ok;
}

Status remove_hid_devices(Controller* controller) {
    if (controller == nullptr) return Status::InvalidArgument;
    if (controller->mass_storage_block.context != nullptr) {
        static_cast<void>(storage::device_registry::unregister_device(
            &controller->mass_storage_block));
    }
    device::DeviceId* const children[] = {
        &controller->keyboard_device,
        &controller->mouse_device,
        &controller->mass_storage_device,
    };
    for (auto* child_id : children) {
        if (*child_id == device::INVALID_DEVICE_ID) continue;
        const auto* child = device::get(*child_id);
        if (child != nullptr) {
            if (child->driver != device::INVALID_DRIVER_ID &&
                (child->driver != controller->owner_driver ||
                 device::release(child->id, controller->owner_driver) !=
                     KStatus::Ok)) {
                return Status::ResourceReleaseFailed;
            }
            if (device::remove_device(*child_id) != KStatus::Ok) {
                return Status::ResourceReleaseFailed;
            }
        }
        *child_id = device::INVALID_DEVICE_ID;
    }
    return Status::Ok;
}

Status remove_companion_device(Controller* controller) {
    if (controller == nullptr) return Status::InvalidArgument;
    auto& companion = controller->companion;
    if (!companion.allocated ||
        companion.child_device == device::INVALID_DEVICE_ID) {
        return Status::Ok;
    }
    const auto* child = device::get(companion.child_device);
    if (child != nullptr) {
        if (child->driver != device::INVALID_DRIVER_ID &&
            (child->driver != controller->owner_driver ||
             device::release(child->id, controller->owner_driver) !=
                 KStatus::Ok)) {
            return Status::ResourceReleaseFailed;
        }
        if (device::remove_device(companion.child_device) != KStatus::Ok) {
            return Status::ResourceReleaseFailed;
        }
    }
    companion.child_device = device::INVALID_DEVICE_ID;
    return Status::Ok;
}

Status release_resources(Controller* controller) {
    if (controller == nullptr) return Status::InvalidArgument;
    controller->initialized = false;
    controller->cleanup_pending = true;
    const Status stopped = quiesce_dma(*controller);
    if (stopped != Status::Ok) return stopped;
    const Status removed = remove_hid_devices(controller);
    if (removed != Status::Ok) return removed;
    const Status companion_removed = remove_companion_device(controller);
    if (companion_removed != Status::Ok) return companion_removed;
    bool released = true;
    storage::dma::Page* const pages[] = {
        &controller->command_ring.page, &controller->ep0_ring.page,
        &controller->interrupt_ring.page, &controller->bulk_in_ring.page,
        &controller->bulk_out_ring.page, &controller->dcbaa_page,
        &controller->event_ring_page, &controller->erst_page,
        &controller->input_context_page, &controller->device_context_page,
        &controller->data_page, &controller->storage_io_page,
        &controller->scratchpad_array_page,
        &controller->companion.input_context_page,
        &controller->companion.device_context_page,
        &controller->companion.data_page,
        &controller->companion.ep0_ring.page,
        &controller->companion.interrupt_ring.page,
    };
    for (auto* page : pages) {
        if (!release_dma_page(page)) released = false;
    }
    for (size_t index = 0U; index < MAXIMUM_SCRATCHPADS; ++index) {
        if (!release_dma_page(&controller->scratchpads[index])) released = false;
    }
    if (!released) return Status::ResourceReleaseFailed;
    auto* space = memory::kernel_virtual_memory::address_space();
    if (space == nullptr && controller->mapped_pages != 0U) {
        return Status::ResourceReleaseFailed;
    }
    // Release the tail first so a failed unmap leaves a contiguous, retryable
    // prefix, never an object reset that loses the remaining mapping owner.
    while (controller->mapped_pages != 0U) {
        const size_t index = controller->mapped_pages - 1U;
        if (memory::virtual_memory::unmap_page(
                space, controller->mapped_base +
                    index * memory::virtual_memory::PAGE_SIZE) !=
            memory::virtual_memory::Status::Ok) {
            return Status::ResourceReleaseFailed;
        }
        --controller->mapped_pages;
    }
    *controller = {};
    controller->keyboard_device = device::INVALID_DEVICE_ID;
    controller->mouse_device = device::INVALID_DEVICE_ID;
    controller->mass_storage_device = device::INVALID_DEVICE_ID;
    return Status::Ok;
}

Status fail_initialization(Status failure) {
    const Status cleanup = release_resources(&g_controller);
    return cleanup == Status::Ok ? failure : cleanup;
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
        !initialize_ring(&controller.bulk_in_ring) ||
        !initialize_ring(&controller.bulk_out_ring) ||
        !allocate_page(&controller.data_page) ||
        !allocate_page(&controller.storage_io_page) ||
        !allocate_page(&controller.companion.input_context_page) ||
        !allocate_page(&controller.companion.device_context_page) ||
        !allocate_page(&controller.companion.data_page) ||
        !initialize_ring(&controller.companion.ep0_ring) ||
        !initialize_ring(&controller.companion.interrupt_ring)) {
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
    controller.companion.child_device = device::INVALID_DEVICE_ID;
    controller.companion.hid_kind = HidKind::None;
    controller.companion.hid_lifecycle = HidLifecycle::WaitingForDevice;
    controller.companion.allocated = true;
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
    controller.dma_published = true;
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

bool defer_event(Controller& controller, const Trb& event) {
    if (controller.deferred_event_count >= MAXIMUM_DEFERRED_EVENTS) {
        log::write(log::Level::Error, "XHCI", "deferred event queue overflow");
        return false;
    }
    controller.deferred_events[controller.deferred_event_count++] = event;
    return true;
}

bool next_poll_event(Controller& controller, Trb* output) {
    if (output == nullptr) return false;
    if (controller.deferred_event_count == 0U) {
        return next_event(controller, output);
    }
    *output = controller.deferred_events[0U];
    for (size_t index = 1U; index < controller.deferred_event_count; ++index) {
        controller.deferred_events[index - 1U] = controller.deferred_events[index];
    }
    --controller.deferred_event_count;
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
        if (!defer_event(controller, event)) return false;
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

bool disable_slot(Controller& controller, uint8_t& slot) {
    if (slot == 0U) return true;
    Trb completion{};
    if (!submit_command(controller, 0U, 0U,
            static_cast<uint32_t>(TRB_DISABLE_SLOT) << 10U |
            static_cast<uint32_t>(slot) << 24U, &completion)) {
        return false;
    }
    // Transfers precede Disable Slot completion on the same event ring.
    // Discard retired transfers before reusing slot IDs or TRB addresses.
    size_t kept = 0U;
    for (size_t index = 0U; index < controller.deferred_event_count; ++index) {
        const Trb& event = controller.deferred_events[index];
        if (trb_type(event) == TRB_TRANSFER_EVENT &&
            (event.control >> 24U) == slot) {
            continue;
        }
        controller.deferred_events[kept++] = event;
    }
    controller.deferred_event_count = kept;
    static_cast<uint64_t*>(controller.dcbaa_page.virtual_address)[slot] = 0U;
    barrier();
    slot = 0U;
    return true;
}

bool port_claimed(const Controller& controller, uint8_t port) {
    if (port == 0U) return false;
    if (controller.slot_id != 0U && controller.port_id == port) return true;
    if (controller.companion.slot_id != 0U &&
        controller.companion.port_id == port) {
        return true;
    }
    return controller.companion.ignored_port_id == port;
}

bool connected_probe_candidate(Controller& controller, uint8_t port) {
    const size_t index = static_cast<size_t>(port - 1U);
    const uint32_t status = read32(controller.operational, OP_PORTS + index * PORT_STRIDE);
    if (status == UINT32_MAX) return false;
    if ((status & PORT_CONNECTED) == 0U ||
        (status & PORT_CONNECT_CHANGED) != 0U) {
        controller.deferred_hub_ports[port] = false;
    }
    return (status & PORT_CONNECTED) != 0U && !controller.deferred_hub_ports[port];
}

uint8_t first_unclaimed_connected_port(Controller& controller) {
    // MaxPorts can be 255: keep the loop index wider than a port number.
    for (size_t index = 0U; index < controller.maximum_ports; ++index) {
        const uint8_t port = static_cast<uint8_t>(index + 1U);
        if (port_claimed(controller, port)) continue;
        if (connected_probe_candidate(controller, port)) return port;
    }
    return 0U;
}

bool reset_port(
    Controller& controller,
    uint8_t port,
    uint8_t* speed) {
    if (port == 0U || port > controller.maximum_ports || speed == nullptr) {
        return false;
    }
    const size_t offset = OP_PORTS +
        static_cast<size_t>(port - 1U) * PORT_STRIDE;
    write32(controller.operational, offset, PORT_POWER | PORT_RESET);
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        const uint32_t status = read32(controller.operational, offset);
        if ((status & PORT_CONNECTED) != 0U &&
            (status & PORT_RESET) == 0U &&
            (status & PORT_ENABLED) != 0U) {
            *speed = static_cast<uint8_t>((status >> 10U) & 0x0FU);
            return *speed != 0U;
        }
        relax();
    }
    return false;
}

void acknowledge_port_change(Controller& controller, uint8_t port) {
    if (port == 0U || port > controller.maximum_ports) return;
    const size_t offset = OP_PORTS + static_cast<size_t>(port - 1U) * PORT_STRIDE;
    const uint32_t status = read32(controller.operational, offset);
    if (status == UINT32_MAX) return;
    constexpr uint32_t changes = UINT32_C(0x7F) << 17U;
    constexpr uint32_t preserved = PORT_POWER | (UINT32_C(3) << 14U) |
        (UINT32_C(7) << 25U);
    if ((status & changes) != 0U) {
        // Do not echo PED/PR/LWS: they have side effects, not RW semantics.
        write32(controller.operational, offset, status & (preserved | changes));
    }
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
    if (!route::slot_context_words(
            {controller.port_id, 0U, 0U}, controller.port_speed, 1U,
            &slot[0U], &slot[1U]))
        return false;
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

bool read_descriptors(Controller& controller, bool& is_hub) {
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
    is_hub = bytes[4U] == USB_HUB_CLASS;
    if (is_hub) return false;
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
    const auto* configuration = static_cast<const uint8_t*>(
        controller.data_page.virtual_address);
    controller.hid_kind = HidKind::None;
    controller.mass_storage_present = false;
    if (find_boot_keyboard_interface(
            configuration, total, &controller.keyboard_interface)) {
        controller.hid_kind = HidKind::Keyboard;
        return true;
    }
    if (find_boot_mouse_interface(
            configuration, total, &controller.mouse_interface)) {
        HidReportInterface mouse_report_interface{};
        if (find_hid_report_interface_for_interface(
                configuration,
                total,
                controller.mouse_interface.interface_number,
                &mouse_report_interface) &&
            mouse_report_interface.report_descriptor_length <=
                memory::virtual_memory::PAGE_SIZE) {
            controller.report_interface = mouse_report_interface;
            if (control_transfer(
                    controller, 0x00U, 9U,
                    mouse_report_interface.configuration_value,
                    0U, 0U, false)) {
                clear_bytes(
                    controller.data_page.virtual_address,
                    memory::virtual_memory::PAGE_SIZE);
                if (control_transfer(
                        controller, 0x81U, 6U, 0x2200U,
                        mouse_report_interface.interface_number,
                        mouse_report_interface.report_descriptor_length,
                        true) &&
                    hid::parse_pointer_report_descriptor(
                        static_cast<const uint8_t*>(
                            controller.data_page.virtual_address),
                        mouse_report_interface.report_descriptor_length,
                        &controller.pointer_layout)) {
                    controller.hid_kind = HidKind::ReportPointer;
                    return true;
                }
            }
        }
        controller.hid_kind = HidKind::Mouse;
        return true;
    }
    HidReportInterface report_interface{};
    if (find_hid_report_interface(
            configuration, total, &report_interface) &&
        report_interface.report_descriptor_length <=
            memory::virtual_memory::PAGE_SIZE) {
        controller.report_interface = report_interface;
        if (control_transfer(
                controller, 0x00U, 9U,
                report_interface.configuration_value,
                0U, 0U, false)) {
            clear_bytes(
                controller.data_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
            if (control_transfer(
                    controller, 0x81U, 6U, 0x2200U,
                    report_interface.interface_number,
                    report_interface.report_descriptor_length,
                    true) &&
                hid::parse_pointer_report_descriptor(
                    static_cast<const uint8_t*>(
                        controller.data_page.virtual_address),
                    report_interface.report_descriptor_length,
                    &controller.pointer_layout)) {
                controller.hid_kind = HidKind::ReportPointer;
                return true;
            }
        }
    }
    if (mass_storage::find_bulk_only_scsi_interface(
            configuration, total, &controller.mass_storage_interface)) {
        controller.mass_storage_present = true;
        return true;
    }
    return false;
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

struct HidInterruptEndpoint {
    uint8_t configuration_value;
    uint8_t interface_number;
    uint8_t endpoint_address;
    uint16_t maximum_packet_size;
    uint8_t interval;
    uint16_t transfer_size;
};

bool configure_hid_interrupt_endpoint(
    Controller& controller,
    const HidInterruptEndpoint& hid) {
    if (!control_transfer(
            controller, 0x00U, 9U, hid.configuration_value,
            0U, 0U, false)) {
        return false;
    }
    // Boot protocol keeps report decoding deterministic. SET_IDLE is harmless
    // for the current keyboard path and remains explicit for later HID kinds.
    static_cast<void>(control_transfer(
        controller, 0x21U, 0x0BU, 0U,
        hid.interface_number, 0U, false));
    static_cast<void>(control_transfer(
        controller, 0x21U, 0x0AU, 0U,
        hid.interface_number, 0U, false));

    const uint8_t endpoint_number = hid.endpoint_address & 0x0FU;
    controller.interrupt_dci = static_cast<uint8_t>(
        endpoint_number * 2U + 1U);
    if (endpoint_number == 0U || controller.interrupt_dci >= 32U ||
        hid.transfer_size == 0U ||
        hid.transfer_size > hid.maximum_packet_size) {
        return false;
    }
    controller.interrupt_packet_size = hid.transfer_size;
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
    endpoint[0U] = static_cast<uint32_t>(
        endpoint_interval(controller.port_speed, hid.interval)) << 16U;
    endpoint[1U] = UINT32_C(3) << 1U | UINT32_C(7) << 3U |
        static_cast<uint32_t>(hid.maximum_packet_size) << 16U;
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

bool configure_keyboard_endpoint(Controller& controller) {
    const HidInterruptEndpoint hid{
        controller.keyboard_interface.configuration_value,
        controller.keyboard_interface.interface_number,
        controller.keyboard_interface.endpoint_address,
        controller.keyboard_interface.maximum_packet_size,
        controller.keyboard_interface.interval,
        8U,
    };
    return configure_hid_interrupt_endpoint(controller, hid);
}

bool configure_mouse_endpoint(Controller& controller) {
    const HidInterruptEndpoint hid{
        controller.mouse_interface.configuration_value,
        controller.mouse_interface.interface_number,
        controller.mouse_interface.endpoint_address,
        controller.mouse_interface.maximum_packet_size,
        controller.mouse_interface.interval,
        3U,
    };
    return configure_hid_interrupt_endpoint(controller, hid);
}

bool configure_report_pointer_endpoint(Controller& controller) {
    const HidInterruptEndpoint hid_endpoint{
        controller.report_interface.configuration_value,
        controller.report_interface.interface_number,
        controller.report_interface.endpoint_address,
        controller.report_interface.maximum_packet_size,
        controller.report_interface.interval,
        controller.pointer_layout.transfer_bytes,
    };
    return controller.pointer_layout.valid &&
        configure_hid_interrupt_endpoint(controller, hid_endpoint);
}


struct BulkTarget {
    const storage::dma::Page& data_page;
    uint8_t slot_id;
    uint8_t in_dci;
    uint8_t out_dci;
};

BulkTarget primary_bulk_target(const Controller& controller) {
    return {controller.data_page, controller.slot_id,
        controller.bulk_in_dci, controller.bulk_out_dci};
}

bool configure_bulk_endpoints(
    Controller& controller,
    const mass_storage::BulkOnlyInterface& interface,
    const storage::dma::Page& input_page,
    const uint32_t* output_slot,
    BulkTarget& target) {
    const auto context_at = [&](size_t index) {
        return reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(input_page.virtual_address) + index * controller.context_size);
    };
    bulk::EndpointPlan bulk_in{};
    bulk::EndpointPlan bulk_out{};
    if (!bulk::build_endpoint_plan(
            interface.bulk_in_endpoint,
            interface.bulk_in_maximum_packet_size,
            &bulk_in) ||
        !bulk::build_endpoint_plan(
            interface.bulk_out_endpoint,
            interface.bulk_out_maximum_packet_size,
            &bulk_out) ||
        bulk_in.direction != bulk::EndpointDirection::In ||
        bulk_out.direction != bulk::EndpointDirection::Out ||
        bulk_in.device_context_index == bulk_out.device_context_index) {
        return false;
    }

    bulk::EndpointContextImage in_context{};
    bulk::EndpointContextImage out_context{};
    if (!bulk::build_endpoint_context(
            bulk_in,
            controller.bulk_in_ring.page.physical_address,
            interface.bulk_in_maximum_packet_size,
            &in_context) ||
        !bulk::build_endpoint_context(
            bulk_out,
            controller.bulk_out_ring.page.physical_address,
            interface.bulk_out_maximum_packet_size,
            &out_context)) {
        return false;
    }

    const uint8_t current_entries = static_cast<uint8_t>(
        (output_slot[0U] >> 27U) & UINT32_C(0x1F));
    uint8_t context_entries = current_entries;
    if (!bulk::extend_context_entries(
            context_entries, bulk_in.device_context_index, &context_entries) ||
        !bulk::extend_context_entries(
            context_entries, bulk_out.device_context_index, &context_entries)) {
        return false;
    }

    target.in_dci = bulk_in.device_context_index;
    target.out_dci = bulk_out.device_context_index;
    clear_bytes(input_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
    context_at(0U)[1U] =
        UINT32_C(1) |
        (UINT32_C(1) << target.in_dci) |
        (UINT32_C(1) << target.out_dci);
    copy_bytes(context_at(1U), output_slot,
               controller.context_size);
    uint32_t* slot = context_at(1U);
    slot[0U] &= ~(UINT32_C(0x1F) << 27U);
    slot[0U] |= static_cast<uint32_t>(context_entries) << 27U;

    copy_bytes(
        context_at(static_cast<size_t>(target.in_dci) + 1U),
        in_context.words,
        sizeof(in_context.words));
    copy_bytes(
        context_at(static_cast<size_t>(target.out_dci) + 1U),
        out_context.words,
        sizeof(out_context.words));

    Trb completion{};
    return submit_command(
        controller,
        input_page.physical_address,
        0U,
        static_cast<uint32_t>(TRB_CONFIGURE_ENDPOINT) << 10U |
            static_cast<uint32_t>(target.slot_id) << 24U,
        &completion);
}

bool configure_mass_storage_endpoints(Controller& controller) {
    const auto& interface = controller.mass_storage_interface;
    if (!controller.mass_storage_present ||
        !control_transfer(controller, 0x00U, 9U, interface.configuration_value,
            0U, 0U, false)) return false;
    auto target = primary_bulk_target(controller);
    const bool configured = configure_bulk_endpoints(controller, interface,
        controller.input_context_page, output_context(controller, 0U), target);
    controller.bulk_in_dci = target.in_dci;
    controller.bulk_out_dci = target.out_dci;
    return configured;
}

bool wait_bulk_transfer(
    Controller& controller,
    const BulkTarget& target,
    uint64_t expected_trb,
    uint8_t endpoint_dci,
    size_t requested_length,
    bulk::TransferCompletion* completion) {
    if (completion == nullptr) return false;
    for (uint32_t attempt = 0U; attempt < POLL_BUDGET; ++attempt) {
        Trb event{};
        if (!next_event(controller, &event)) {
            relax();
            continue;
        }
        if (trb_type(event) != TRB_TRANSFER_EVENT) {
            if (!defer_event(controller, event)) return false;
            continue;
        }
        const uint8_t event_slot =
            static_cast<uint8_t>(event.control >> 24U);
        if (event_slot != target.slot_id) {
            if (!defer_event(controller, event)) return false;
            continue;
        }
        const auto status = bulk::classify_transfer_completion(
            expected_trb,
            target.slot_id,
            endpoint_dci,
            requested_length,
            event.parameter,
            event.status,
            event.control,
            completion);
        if (status == bulk::TransferCompletionStatus::Complete ||
            status == bulk::TransferCompletionStatus::ShortPacket) {
            return true;
        }
        // Any other Transfer Event is either foreign, malformed or a device
        // failure. Consuming it and continuing would hide ownership loss.
        return false;
    }
    return false;
}

bool bulk_out_transfer(
    Controller& controller,
    const BulkTarget& target,
    const void* source,
    size_t length) {
    if (source == nullptr || length == 0U ||
        length > memory::virtual_memory::PAGE_SIZE ||
        target.out_dci < 2U) {
        return false;
    }

    bulk::TransferChunk chunk{};
    if (!bulk::plan_normal_trb_chunk(
            target.data_page.physical_address, length, &chunk) ||
        chunk.length != length) {
        return false;
    }

    copy_bytes(target.data_page.virtual_address, source, length);
    const uint64_t trb = enqueue_trb(
        controller.bulk_out_ring,
        chunk.physical_address,
        static_cast<uint32_t>(chunk.length),
        static_cast<uint32_t>(TRB_NORMAL) << 10U | UINT32_C(1) << 5U);
    controller.doorbells[target.slot_id] = target.out_dci;

    bulk::TransferCompletion completion{};
    return wait_bulk_transfer(
               controller, target, trb, target.out_dci, length, &completion) &&
        completion.transferred == length &&
        completion.completion_code == COMPLETION_SUCCESS;
}

bool bulk_in_transfer(
    Controller& controller,
    const BulkTarget& target,
    void* destination,
    size_t capacity,
    size_t* transferred) {
    if (destination == nullptr || transferred == nullptr || capacity == 0U ||
        capacity > memory::virtual_memory::PAGE_SIZE ||
        target.in_dci < 2U) {
        return false;
    }

    bulk::TransferChunk chunk{};
    if (!bulk::plan_normal_trb_chunk(
            target.data_page.physical_address, capacity, &chunk) ||
        chunk.length != capacity) {
        return false;
    }

    clear_bytes(target.data_page.virtual_address, capacity);
    const uint64_t trb = enqueue_trb(
        controller.bulk_in_ring,
        chunk.physical_address,
        static_cast<uint32_t>(chunk.length),
        static_cast<uint32_t>(TRB_NORMAL) << 10U | UINT32_C(1) << 5U);
    controller.doorbells[target.slot_id] = target.in_dci;

    bulk::TransferCompletion completion{};
    if (!wait_bulk_transfer(
            controller, target, trb, target.in_dci, capacity, &completion) ||
        completion.transferred > capacity) {
        return false;
    }
    copy_bytes(destination, target.data_page.virtual_address,
               completion.transferred);
    *transferred = completion.transferred;
    return true;
}

uint32_t next_mass_storage_tag(Controller& controller) {
    ++controller.mass_storage_tag;
    if (controller.mass_storage_tag == 0U) ++controller.mass_storage_tag;
    return controller.mass_storage_tag;
}

bool execute_mass_storage_bot(
    Controller& controller,
    const BulkTarget& target,
    const uint8_t* cdb,
    size_t cdb_length,
    mass_storage::DataDirection direction,
    void* data,
    size_t data_length,
    size_t* data_received,
    mass_storage::CommandStatusWrapper* output_status) {
    if (cdb == nullptr || cdb_length == 0U ||
        cdb_length > mass_storage::MAXIMUM_COMMAND_BLOCK_SIZE ||
        output_status == nullptr ||
        (direction == mass_storage::DataDirection::None &&
         (data != nullptr || data_length != 0U || data_received != nullptr)) ||
        ((direction == mass_storage::DataDirection::In ||
          direction == mass_storage::DataDirection::Out) &&
         (data == nullptr || data_length == 0U)) ||
        (direction == mass_storage::DataDirection::In &&
         data_received == nullptr) ||
        (direction == mass_storage::DataDirection::Out &&
         data_received != nullptr)) {
        return false;
    }

    const uint32_t tag = next_mass_storage_tag(controller);
    mass_storage::CommandBlockWrapper wrapper{};
    wrapper.tag = tag;
    wrapper.data_transfer_length = static_cast<uint32_t>(data_length);
    wrapper.direction = direction;
    wrapper.logical_unit_number = 0U;
    wrapper.command_length = static_cast<uint8_t>(cdb_length);
    copy_bytes(wrapper.command, cdb, cdb_length);

    uint8_t cbw[mass_storage::COMMAND_BLOCK_WRAPPER_SIZE]{};
    if (!mass_storage::encode_command_block_wrapper(
            &wrapper, cbw, sizeof(cbw)) ||
        !bulk_out_transfer(controller, target, cbw, sizeof(cbw))) {
        return false;
    }

    size_t received = 0U;
    if (direction == mass_storage::DataDirection::In) {
        if (!bulk_in_transfer(
                controller, target, data, data_length, &received) ||
            received > data_length) {
            return false;
        }
    } else if (direction == mass_storage::DataDirection::Out) {
        if (!bulk_out_transfer(controller, target, data, data_length)) {
            return false;
        }
    }

    uint8_t csw[mass_storage::COMMAND_STATUS_WRAPPER_SIZE]{};
    size_t csw_received = 0U;
    if (!bulk_in_transfer(
            controller, target, csw, sizeof(csw), &csw_received) ||
        csw_received != sizeof(csw)) {
        return false;
    }

    mass_storage::CommandStatusWrapper status{};
    if (!mass_storage::decode_command_status_wrapper(
            csw, sizeof(csw), tag,
            static_cast<uint32_t>(data_length), &status) ||
        status.status == mass_storage::CommandStatus::PhaseError) {
        return false;
    }
    if (direction == mass_storage::DataDirection::In &&
        received + static_cast<size_t>(status.data_residue) != data_length) {
        return false;
    }
    if (data_received != nullptr) *data_received = received;
    *output_status = status;
    return true;
}

bool request_mass_storage_sense(Controller& controller, const BulkTarget& target) {
    constexpr size_t sense_length = 18U;
    uint8_t cdb[mass_storage::scsi::CDB6_SIZE]{};
    if (!mass_storage::scsi::build_request_sense(
            static_cast<uint8_t>(sense_length), cdb, sizeof(cdb))) {
        return false;
    }
    uint8_t sense[sense_length]{};
    size_t received = 0U;
    mass_storage::CommandStatusWrapper status{};
    return execute_mass_storage_bot(
               controller, target, cdb, sizeof(cdb),
               mass_storage::DataDirection::In,
               sense, sizeof(sense), &received, &status) &&
        status.status == mass_storage::CommandStatus::Passed &&
        received == sizeof(sense) &&
        status.data_residue == 0U;
}

bool test_mass_storage_ready(Controller& controller, const BulkTarget& target) {
    uint8_t cdb[mass_storage::scsi::CDB6_SIZE]{};
    if (!mass_storage::scsi::build_test_unit_ready(cdb, sizeof(cdb))) {
        return false;
    }
    mass_storage::CommandStatusWrapper status{};
    if (!execute_mass_storage_bot(
            controller, target, cdb, sizeof(cdb),
            mass_storage::DataDirection::None,
            nullptr, 0U, nullptr, &status) ||
        status.data_residue != 0U) {
        return false;
    }
    terminal::println("[TEST] xhci_mass_storage_bot_transport: PASS");
    if (status.status == mass_storage::CommandStatus::Passed) {
        return true;
    }

    terminal::println(
        "[TEST] xhci_mass_storage_test_unit_ready: CHECK_CONDITION");
    if (!request_mass_storage_sense(controller, target)) return false;

    status = {};
    if (!execute_mass_storage_bot(
            controller, target, cdb, sizeof(cdb),
            mass_storage::DataDirection::None,
            nullptr, 0U, nullptr, &status) ||
        status.data_residue != 0U ||
        status.status != mass_storage::CommandStatus::Passed) {
        return false;
    }
    return true;
}

constexpr size_t SCSI_INQUIRY_BYTES = 36U;

bool read_scsi_inquiry(Controller& controller, const BulkTarget& target,
    uint8_t (&inquiry)[SCSI_INQUIRY_BYTES]) {
    uint8_t inquiry_cdb[mass_storage::scsi::CDB6_SIZE]{};
    if (!mass_storage::scsi::build_inquiry(
            static_cast<uint8_t>(sizeof(inquiry)),
            inquiry_cdb, sizeof(inquiry_cdb))) {
        return false;
    }
    size_t inquiry_received = 0U;
    mass_storage::CommandStatusWrapper inquiry_status{};
    return execute_mass_storage_bot(
            controller, target, inquiry_cdb, sizeof(inquiry_cdb),
            mass_storage::DataDirection::In,
            inquiry, sizeof(inquiry), &inquiry_received, &inquiry_status) &&
        inquiry_status.status == mass_storage::CommandStatus::Passed &&
        inquiry_status.data_residue == 0U &&
        inquiry_received == sizeof(inquiry);
}

bool read_scsi_capacity(Controller& controller, const BulkTarget& target,
    mass_storage::scsi::ReadCapacity10Data& output) {
    uint8_t capacity_cdb[mass_storage::scsi::CDB10_SIZE]{};
    if (!mass_storage::scsi::build_read_capacity10(
            capacity_cdb, sizeof(capacity_cdb))) {
        return false;
    }
    uint8_t capacity[8U]{};
    size_t capacity_received = 0U;
    mass_storage::CommandStatusWrapper capacity_status{};
    if (!execute_mass_storage_bot(
            controller, target, capacity_cdb, sizeof(capacity_cdb),
            mass_storage::DataDirection::In,
            capacity, sizeof(capacity), &capacity_received, &capacity_status) ||
        capacity_status.status != mass_storage::CommandStatus::Passed ||
        capacity_status.data_residue != 0U ||
        capacity_received != sizeof(capacity)) {
        return false;
    }

    mass_storage::scsi::ReadCapacity10Data geometry{};
    if (!mass_storage::scsi::parse_read_capacity10(
            capacity, sizeof(capacity), &geometry) ||
        geometry.block_size < 512U ||
        geometry.block_size > memory::virtual_memory::PAGE_SIZE ||
        (memory::virtual_memory::PAGE_SIZE % geometry.block_size) != 0U ||
        geometry.block_count == 0U) {
        return false;
    }

    output = geometry;
    return true;
}

bool probe_mass_storage_scsi_geometry(Controller& controller) {
    if (!test_mass_storage_ready(controller, primary_bulk_target(controller))) {
        terminal::println("[TEST] xhci_mass_storage_test_unit_ready: FAIL");
        return false;
    }
    terminal::println("[TEST] xhci_mass_storage_test_unit_ready: PASS");

    uint8_t inquiry[SCSI_INQUIRY_BYTES]{};
    if (!read_scsi_inquiry(controller, primary_bulk_target(controller), inquiry)) {
        terminal::println("[TEST] xhci_mass_storage_inquiry: FAIL");
        return false;
    }
    terminal::println("[TEST] xhci_mass_storage_inquiry: PASS");

    mass_storage::scsi::ReadCapacity10Data geometry{};
    if (!read_scsi_capacity(controller, primary_bulk_target(controller), geometry)) {
        terminal::println("[TEST] xhci_mass_storage_read_capacity: FAIL");
        return false;
    }

    controller.mass_storage_block_size = geometry.block_size;
    controller.mass_storage_block_count = geometry.block_count;
    log::write_u64(
        log::Level::Info, "USB", "Mass Storage logical block bytes=",
        controller.mass_storage_block_size);
    log::write_u64(
        log::Level::Info, "USB", "Mass Storage logical block count=",
        controller.mass_storage_block_count);
    terminal::println("[TEST] xhci_mass_storage_read_capacity: PASS");
    terminal::println("[TEST] xhci_mass_storage_scsi_geometry: PASS");
    return true;
}

storage::block::Status read_scsi_chunk(
    Controller& controller,
    const BulkTarget& target,
    uint32_t first_block,
    uint16_t block_count,
    size_t transfer_bytes,
    void* destination) {
    if (destination == nullptr || transfer_bytes == 0U ||
        transfer_bytes > memory::virtual_memory::PAGE_SIZE) {
        return storage::block::Status::InvalidArgument;
    }
    uint8_t cdb[mass_storage::scsi::CDB10_SIZE]{};
    if (!mass_storage::scsi::build_read10(
            first_block,
            block_count,
            cdb,
            sizeof(cdb))) {
        return storage::block::Status::InvalidArgument;
    }

    size_t received = 0U;
    mass_storage::CommandStatusWrapper status{};
    if (!execute_mass_storage_bot(
            controller, target,
            cdb,
            sizeof(cdb),
            mass_storage::DataDirection::In,
            destination,
            transfer_bytes,
            &received,
            &status)) {
        return storage::block::Status::IoError;
    }
    if (status.status != mass_storage::CommandStatus::Passed ||
        status.data_residue != 0U ||
        received != transfer_bytes) {
        return storage::block::Status::CommandFailed;
    }
    return storage::block::Status::Ok;
}

storage::block::Status read_mass_storage_blocks(
    Controller& controller,
    uint64_t first_block,
    uint64_t block_count,
    void* destination) {
    if (destination == nullptr || block_count == 0U ||
        controller.mass_storage_block_size == 0U ||
        controller.mass_storage_block_count == 0U) {
        return storage::block::Status::InvalidArgument;
    }
    if (first_block >= controller.mass_storage_block_count ||
        block_count > controller.mass_storage_block_count - first_block) {
        return storage::block::Status::OutOfRange;
    }

    const size_t blocks_per_transfer =
        memory::virtual_memory::PAGE_SIZE /
        controller.mass_storage_block_size;
    if (blocks_per_transfer == 0U) {
        return storage::block::Status::InvalidGeometry;
    }

    auto* output = static_cast<uint8_t*>(destination);
    uint64_t completed = 0U;
    while (completed < block_count) {
        const uint64_t logical_block = first_block + completed;
        if (logical_block > UINT32_MAX) {
            return storage::block::Status::AddressNotSupported;
        }

        uint64_t remaining = block_count - completed;
        size_t chunk_blocks = blocks_per_transfer;
        if (remaining < static_cast<uint64_t>(chunk_blocks)) {
            chunk_blocks = static_cast<size_t>(remaining);
        }
        if (chunk_blocks == 0U || chunk_blocks > UINT16_MAX) {
            return storage::block::Status::InvalidArgument;
        }

        const size_t transfer_bytes =
            chunk_blocks * static_cast<size_t>(controller.mass_storage_block_size);
        const auto status = read_scsi_chunk(
            controller, primary_bulk_target(controller),
            static_cast<uint32_t>(logical_block), static_cast<uint16_t>(chunk_blocks),
            transfer_bytes, output + static_cast<size_t>(completed) *
                controller.mass_storage_block_size);
        if (status != storage::block::Status::Ok) return status;
        completed += static_cast<uint64_t>(chunk_blocks);
    }

    return storage::block::Status::Ok;
}

storage::block::Status write_scsi_chunk(
    Controller& controller,
    const BulkTarget& target,
    uint32_t first_block,
    uint16_t block_count,
    size_t transfer_bytes,
    const void* source) {
    if (source == nullptr || transfer_bytes == 0U ||
        transfer_bytes > memory::virtual_memory::PAGE_SIZE) {
        return storage::block::Status::InvalidArgument;
    }
    uint8_t cdb[mass_storage::scsi::CDB10_SIZE]{};
    if (!mass_storage::scsi::build_write10(
            first_block,
            block_count,
            cdb,
            sizeof(cdb))) {
        return storage::block::Status::InvalidArgument;
    }

    mass_storage::CommandStatusWrapper status{};
    if (!execute_mass_storage_bot(
            controller, target,
            cdb,
            sizeof(cdb),
            mass_storage::DataDirection::Out,
            const_cast<void*>(source),
            transfer_bytes,
            nullptr,
            &status)) {
        return storage::block::Status::IoError;
    }
    if (status.status != mass_storage::CommandStatus::Passed ||
        status.data_residue != 0U) {
        return storage::block::Status::CommandFailed;
    }
    return storage::block::Status::Ok;
}

storage::block::Status write_mass_storage_blocks(
    Controller& controller,
    uint64_t first_block,
    uint64_t block_count,
    const void* source) {
    if (source == nullptr || block_count == 0U ||
        controller.mass_storage_block_size == 0U ||
        controller.mass_storage_block_count == 0U) {
        return storage::block::Status::InvalidArgument;
    }
    if (first_block >= controller.mass_storage_block_count ||
        block_count > controller.mass_storage_block_count - first_block) {
        return storage::block::Status::OutOfRange;
    }

    const size_t blocks_per_transfer =
        memory::virtual_memory::PAGE_SIZE /
        controller.mass_storage_block_size;
    if (blocks_per_transfer == 0U) {
        return storage::block::Status::InvalidGeometry;
    }

    const auto* input = static_cast<const uint8_t*>(source);
    uint64_t completed = 0U;
    while (completed < block_count) {
        const uint64_t logical_block = first_block + completed;
        if (logical_block > UINT32_MAX) {
            return storage::block::Status::AddressNotSupported;
        }

        uint64_t remaining = block_count - completed;
        size_t chunk_blocks = blocks_per_transfer;
        if (remaining < static_cast<uint64_t>(chunk_blocks)) {
            chunk_blocks = static_cast<size_t>(remaining);
        }
        if (chunk_blocks == 0U || chunk_blocks > UINT16_MAX) {
            return storage::block::Status::InvalidArgument;
        }

        const size_t transfer_bytes =
            chunk_blocks * static_cast<size_t>(controller.mass_storage_block_size);
        const auto status = write_scsi_chunk(
            controller, primary_bulk_target(controller),
            static_cast<uint32_t>(logical_block), static_cast<uint16_t>(chunk_blocks),
            transfer_bytes, input + static_cast<size_t>(completed) *
                controller.mass_storage_block_size);
        if (status != storage::block::Status::Ok) return status;
        completed += static_cast<uint64_t>(chunk_blocks);
    }

    return storage::block::Status::Ok;
}

storage::block::Status synchronize_scsi_cache(Controller& controller, const BulkTarget& target) {
    uint8_t cdb[mass_storage::scsi::CDB10_SIZE]{};
    if (!mass_storage::scsi::build_synchronize_cache10(cdb, sizeof(cdb))) {
        return storage::block::Status::InvalidArgument;
    }

    mass_storage::CommandStatusWrapper status{};
    if (!execute_mass_storage_bot(
            controller, target,
            cdb,
            sizeof(cdb),
            mass_storage::DataDirection::None,
            nullptr,
            0U,
            nullptr,
            &status)) {
        return storage::block::Status::IoError;
    }
    return status.status == mass_storage::CommandStatus::Passed &&
        status.data_residue == 0U
        ? storage::block::Status::Ok
        : storage::block::Status::CommandFailed;
}

storage::block::Status synchronize_mass_storage(Controller& controller) {
    return synchronize_scsi_cache(controller, primary_bulk_target(controller));
}

storage::block::Status mass_storage_block_read(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    void* destination) {
    auto* controller = static_cast<Controller*>(context);
    if (controller == nullptr ||
        controller != &g_controller ||
        !controller->mass_storage_block_ready ||
        !controller->mass_storage_present ||
        controller->hid_lifecycle != HidLifecycle::Active) {
        return storage::block::Status::NoDevice;
    }
    return read_mass_storage_blocks(
        *controller, first_block, block_count, destination);
}

storage::block::Status mass_storage_block_write(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    const void* source) {
    auto* controller = static_cast<Controller*>(context);
    if (controller == nullptr ||
        controller != &g_controller ||
        !controller->mass_storage_block_ready ||
        !controller->mass_storage_present ||
        controller->hid_lifecycle != HidLifecycle::Active) {
        return storage::block::Status::NoDevice;
    }
    return write_mass_storage_blocks(
        *controller, first_block, block_count, source);
}

storage::block::Status mass_storage_block_flush(void* context) {
    auto* controller = static_cast<Controller*>(context);
    if (controller == nullptr ||
        controller != &g_controller ||
        !controller->mass_storage_block_ready ||
        !controller->mass_storage_present ||
        controller->hid_lifecycle != HidLifecycle::Active) {
        return storage::block::Status::NoDevice;
    }
    return synchronize_mass_storage(*controller);
}

constexpr char USB_TEST_MAGIC[] = "KUROGANE_USB_READ_V1";

bool matches_usb_test_magic(const uint8_t* bytes) {
    for (size_t index = 0U; index < sizeof(USB_TEST_MAGIC) - 1U; ++index) {
        if (bytes[index] != static_cast<uint8_t>(USB_TEST_MAGIC[index])) return false;
    }
    return true;
}

bool matches_usb_test_sector(const uint8_t* bytes, size_t length) {
    if (length != 512U || !matches_usb_test_magic(bytes)) return false;
    constexpr size_t prefix = sizeof(USB_TEST_MAGIC) - 1U;
    for (size_t index = prefix; index < length; ++index) {
        const auto expected = static_cast<uint8_t>(((index - prefix) * 37U + 11U) & 0xFFU);
        if (bytes[index] != expected) return false;
    }
    return true;
}

bool initialize_mass_storage_block_device(Controller& controller) {
    if (controller.mass_storage_block_size == 0U ||
        controller.mass_storage_block_count == 0U) {
        return false;
    }

    controller.mass_storage_block = {
        &controller,
        controller.mass_storage_block_size,
        controller.mass_storage_block_count,
        mass_storage_block_read,
        mass_storage_block_write,
        mass_storage_block_flush,
    };
    controller.mass_storage_block_ready = false;

    clear_bytes(
        controller.storage_io_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    if (read_mass_storage_blocks(
            controller,
            0U,
            1U,
            controller.storage_io_page.virtual_address) !=
        storage::block::Status::Ok) {
        terminal::println("[TEST] xhci_mass_storage_read10: FAIL");
        controller.mass_storage_block = {};
        return false;
    }

    terminal::println("[TEST] xhci_mass_storage_read10: PASS");
    const auto* bytes = static_cast<const uint8_t*>(
        controller.storage_io_page.virtual_address);
    const bool known = matches_usb_test_magic(bytes);
    if (known) {
        terminal::println("[TEST] xhci_mass_storage_known_read: PASS");
    }

    // Never perform a destructive qualification write on arbitrary media.
    // Only the dedicated 8 MiB / 512-byte-sector QEMU scratch image carries
    // this signature and exact geometry.
    const bool qualification_media =
        known &&
        controller.mass_storage_block_size == 512U &&
        controller.mass_storage_block_count == UINT64_C(16384);
    if (qualification_media) {
        auto* io = static_cast<uint8_t*>(
            controller.storage_io_page.virtual_address);
        for (size_t index = 0U; index < controller.mass_storage_block_size;
             ++index) {
            io[index] = static_cast<uint8_t>((index * 29U + 7U) & 0xFFU);
        }
        if (write_mass_storage_blocks(
                controller, 1U, 1U, io) != storage::block::Status::Ok) {
            terminal::println("[TEST] xhci_mass_storage_write10: FAIL");
            controller.mass_storage_block = {};
            return false;
        }
        terminal::println("[TEST] xhci_mass_storage_write10: PASS");

        if (synchronize_mass_storage(controller) != storage::block::Status::Ok) {
            terminal::println("[TEST] xhci_mass_storage_sync_cache: FAIL");
            controller.mass_storage_block = {};
            return false;
        }
        terminal::println("[TEST] xhci_mass_storage_sync_cache: PASS");

        clear_bytes(io, controller.mass_storage_block_size);
        if (read_mass_storage_blocks(controller, 1U, 1U, io) !=
            storage::block::Status::Ok) {
            terminal::println("[TEST] xhci_mass_storage_write_readback: FAIL");
            controller.mass_storage_block = {};
            return false;
        }
        bool matches = true;
        for (size_t index = 0U; index < controller.mass_storage_block_size;
             ++index) {
            const uint8_t expected =
                static_cast<uint8_t>((index * 29U + 7U) & 0xFFU);
            if (io[index] != expected) {
                matches = false;
                break;
            }
        }
        if (!matches) {
            terminal::println("[TEST] xhci_mass_storage_write_readback: FAIL");
            controller.mass_storage_block = {};
            return false;
        }
        terminal::println("[TEST] xhci_mass_storage_write_readback: PASS");
    }

    controller.mass_storage_block_ready = true;
    if (!storage::device_registry::register_device(
            storage::device_registry::Backend::UsbMassStorage,
            0U,
            &controller.mass_storage_block,
            "USB Mass Storage")) {
        controller.mass_storage_block_ready = false;
        controller.mass_storage_block = {};
        return false;
    }
    return true;
}

bool queue_hid_report(Controller& controller) {
    if (controller.report_queued) return true;
    if (controller.hid_kind == HidKind::Keyboard &&
        controller.pending_key_count != 0U) {
        return false;
    }
    if (controller.hid_kind == HidKind::Mouse &&
        controller.pending_mouse_valid) {
        return false;
    }
    if (controller.hid_kind == HidKind::ReportPointer &&
        controller.pending_pointer_valid) {
        return false;
    }
    if (controller.hid_kind == HidKind::None ||
        controller.interrupt_packet_size == 0U) {
        return false;
    }
    clear_bytes(
        controller.data_page.virtual_address,
        controller.interrupt_packet_size);
    controller.report_trb = enqueue_trb(
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
    if (device::register_device(descriptor, &id) != KStatus::Ok) return false;
    // Retain the child identity even when claim or activation fails so the
    // error path can unwind its parent link and generation-owned slot.
    controller.keyboard_device = id;
    if (device::claim(id, controller.owner_driver, "usb-hid-boot") !=
            KStatus::Ok ||
        device::set_status(id, device::Status::Ready) != KStatus::Ok) {
        return false;
    }
    return true;
}

bool register_mouse(Controller& controller) {
    const device::Descriptor descriptor{
        device::Type::Input,
        device::Bus::Usb,
        "USB HID boot mouse",
        controller.vendor_id,
        controller.product_id,
        3U, 1U, 2U,
        {0U, 0U, controller.port_id, 0U},
        controller.parent_device,
        nullptr,
        0U,
    };
    device::DeviceId id = device::INVALID_DEVICE_ID;
    if (device::register_device(descriptor, &id) != KStatus::Ok) return false;
    controller.mouse_device = id;
    if (device::claim(id, controller.owner_driver, "usb-hid-boot") !=
            KStatus::Ok ||
        device::set_status(id, device::Status::Ready) != KStatus::Ok) {
        return false;
    }
    return true;
}

bool register_report_pointer(Controller& controller) {
    const device::Descriptor descriptor{
        device::Type::Input,
        device::Bus::Usb,
        "USB HID report pointer",
        controller.vendor_id,
        controller.product_id,
        3U, 0U, 0U,
        {0U, 0U, controller.port_id, 0U},
        controller.parent_device,
        nullptr,
        0U,
    };
    device::DeviceId id = device::INVALID_DEVICE_ID;
    if (device::register_device(descriptor, &id) != KStatus::Ok) return false;
    controller.mouse_device = id;
    if (device::claim(id, controller.owner_driver, "usb-hid-report") !=
            KStatus::Ok ||
        device::set_status(id, device::Status::Ready) != KStatus::Ok) {
        return false;
    }
    return true;
}


bool register_mass_storage(Controller& controller) {
    const device::Descriptor descriptor{
        device::Type::Block,
        device::Bus::Usb,
        "USB Mass Storage",
        controller.vendor_id,
        controller.product_id,
        mass_storage::USB_CLASS_MASS_STORAGE,
        mass_storage::USB_SUBCLASS_SCSI_TRANSPARENT,
        mass_storage::USB_PROTOCOL_BULK_ONLY,
        {0U, 0U, controller.port_id, 0U},
        controller.parent_device,
        nullptr,
        0U,
    };
    device::DeviceId id = device::INVALID_DEVICE_ID;
    if (device::register_device(descriptor, &id) != KStatus::Ok) return false;
    controller.mass_storage_device = id;
    if (device::claim(id, controller.owner_driver, "usb-mass-storage") !=
            KStatus::Ok ||
        device::set_status(
            id,
            controller.mass_storage_block_ready
                ? device::Status::Ready
                : device::Status::Initializing) != KStatus::Ok) {
        return false;
    }
    return true;
}

void record_keyboard_input(Controller& controller, const keyboard::KeyEvent& event) {
    if (!controller.input_proven && event.pressed) {
        controller.input_proven = true;
        log::write(log::Level::Info, "USB",
                   "hardware xHCI HID keyboard report received");
        terminal::println("[TEST] usb_hid_keyboard_input: PASS");
    }
}

bool flush_keyboard_events(Controller& controller) {
    while (controller.pending_key_index < controller.pending_key_count) {
        const auto& event = controller.pending_keys[controller.pending_key_index];
        if (!input::submit_key(event)) return false;
        record_keyboard_input(controller, event);
        ++controller.pending_key_index;
    }
    controller.pending_key_index = 0U;
    controller.pending_key_count = 0U;
    return true;
}

void record_mouse_input(Controller& controller, const mouse::Sample& sample) {
    if (!controller.input_proven &&
        (sample.delta_x != 0 || sample.delta_y != 0 || sample.wheel != 0 ||
         sample.changed_buttons != 0U)) {
        controller.input_proven = true;
        log::write(log::Level::Info, "USB",
                   "hardware xHCI HID mouse report received");
        terminal::println("[TEST] usb_hid_mouse_input: PASS");
    }
}

int16_t clamp_pointer_delta(int32_t value) {
    if (value < INT16_MIN) return INT16_MIN;
    if (value > INT16_MAX) return INT16_MAX;
    return static_cast<int16_t>(value);
}

bool submit_pointer_report(const hid::PointerReport& report) {
    if (report.absolute) {
        const input::AbsolutePointerSample sample{
            report.x,
            report.y,
            report.logical_minimum_x,
            report.logical_maximum_x,
            report.logical_minimum_y,
            report.logical_maximum_y,
            report.wheel,
            report.buttons,
            report.changed_buttons,
        };
        return input::submit_absolute_pointer(sample);
    }
    const mouse::Sample sample{
        clamp_pointer_delta(report.x),
        clamp_pointer_delta(report.y),
        report.wheel,
        report.buttons,
        report.changed_buttons,
    };
    return input::submit_mouse(sample);
}

void record_report_pointer_input(
    Controller& controller,
    const hid::PointerReport& report) {
    if (!controller.input_proven &&
        (report.x != 0 || report.y != 0 || report.wheel != 0 ||
         report.changed_buttons != 0U)) {
        controller.input_proven = true;
        log::write(
            log::Level::Info,
            "USB",
            report.absolute
                ? "hardware xHCI HID absolute pointer report received"
                : "hardware xHCI HID report-protocol mouse received");
        terminal::println("[TEST] usb_hid_mouse_input: PASS");
        terminal::println("[TEST] usb_hid_report_pointer_input: PASS");
    }
}

bool flush_mouse_sample(Controller& controller) {
    if (!controller.pending_mouse_valid) return true;
    if (!input::submit_mouse(controller.pending_mouse)) return false;
    record_mouse_input(controller, controller.pending_mouse);
    controller.pending_mouse_valid = false;
    return true;
}

bool flush_pointer_report(Controller& controller) {
    if (!controller.pending_pointer_valid) return true;
    if (!submit_pointer_report(controller.pending_pointer)) return false;
    record_report_pointer_input(
        controller, controller.pending_pointer);
    controller.pending_pointer_valid = false;
    return true;
}

bool flush_hid_input(Controller& controller) {
    if (controller.hid_kind == HidKind::Keyboard) {
        return flush_keyboard_events(controller);
    }
    if (controller.hid_kind == HidKind::Mouse) {
        return flush_mouse_sample(controller);
    }
    if (controller.hid_kind == HidKind::ReportPointer) {
        return flush_pointer_report(controller);
    }
    return true;
}

void handle_keyboard_report(Controller& controller, const Trb& event) {
    // Exactly one report is outstanding. Slot/endpoint alone cannot identify
    // its owner: a stale or malformed completion must not consume new DMA.
    // We submit Normal TRBs, never Event Data TRBs (ED flag bit 2).
    if (!controller.report_queued || (event.control & (1U << 2U)) != 0U ||
        event.parameter != controller.report_trb) {
        return;
    }
    controller.report_queued = false;
    controller.report_trb = 0U;
    const uint32_t remaining = event.status & 0x00FFFFFFU;
    const size_t actual = remaining <= controller.interrupt_packet_size
        ? controller.interrupt_packet_size - remaining
        : 0U;
    if (completion_ok(event) && actual >= 8U) {
        if (decode_boot_keyboard_report(
                &controller.keyboard_decoder,
                static_cast<const uint8_t*>(
                    controller.data_page.virtual_address),
                actual,
                controller.pending_keys,
                MAXIMUM_KEYBOARD_EVENTS_PER_REPORT,
                &controller.pending_key_count)) {
            ++controller.reports;
        }
    }
    if (flush_keyboard_events(controller)) {
        static_cast<void>(queue_hid_report(controller));
    }
}

void handle_mouse_report(Controller& controller, const Trb& event) {
    if (!controller.report_queued || (event.control & (1U << 2U)) != 0U ||
        event.parameter != controller.report_trb) {
        return;
    }
    controller.report_queued = false;
    controller.report_trb = 0U;
    const uint32_t remaining = event.status & 0x00FFFFFFU;
    const size_t actual = remaining <= controller.interrupt_packet_size
        ? controller.interrupt_packet_size - remaining
        : 0U;
    if (completion_ok(event) && actual >= 3U) {
        mouse::Sample sample{};
        if (decode_boot_mouse_report(
                &controller.mouse_decoder,
                static_cast<const uint8_t*>(
                    controller.data_page.virtual_address),
                actual,
                &sample)) {
            controller.pending_mouse = sample;
            controller.pending_mouse_valid = true;
            ++controller.reports;
        }
    }
    if (flush_mouse_sample(controller)) {
        static_cast<void>(queue_hid_report(controller));
    }
}


void handle_pointer_report(Controller& controller, const Trb& event) {
    if (!controller.report_queued || (event.control & (1U << 2U)) != 0U ||
        event.parameter != controller.report_trb) {
        return;
    }
    controller.report_queued = false;
    controller.report_trb = 0U;
    const uint32_t remaining = event.status & 0x00FFFFFFU;
    const size_t actual = remaining <= controller.interrupt_packet_size
        ? controller.interrupt_packet_size - remaining
        : 0U;
    if (completion_ok(event) &&
        actual >= controller.pointer_layout.report_bytes) {
        hid::PointerReport report{};
        if (hid::decode_pointer_report(
                controller.pointer_layout,
                &controller.pointer_decoder,
                static_cast<const uint8_t*>(
                    controller.data_page.virtual_address),
                actual,
                &report)) {
            controller.pending_pointer = report;
            controller.pending_pointer_valid = true;
            ++controller.reports;
        }
    }
    if (flush_pointer_report(controller)) {
        static_cast<void>(queue_hid_report(controller));
    }
}

uint32_t* companion_input_context(
    Controller& controller,
    size_t index) {
    return reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(
            controller.companion.input_context_page.virtual_address) +
        index * controller.context_size);
}

uint32_t* companion_output_context(
    Controller& controller,
    size_t index) {
    return reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(
            controller.companion.device_context_page.virtual_address) +
        index * controller.context_size);
}

bool address_companion_device(Controller& controller, const route::Address& topology) {
    auto& companion = controller.companion;
    Trb completion{};
    if (!submit_command(
            controller, 0U, 0U,
            static_cast<uint32_t>(TRB_ENABLE_SLOT) << 10U,
            &completion)) {
        return false;
    }
    companion.slot_id = static_cast<uint8_t>(completion.control >> 24U);
    if (companion.slot_id == 0U ||
        companion.slot_id > controller.maximum_slots) {
        return false;
    }
    static_cast<uint64_t*>(controller.dcbaa_page.virtual_address)
        [companion.slot_id] =
        companion.device_context_page.physical_address;

    clear_bytes(
        companion.input_context_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    companion_input_context(controller, 0U)[1U] = 3U;
    uint32_t* slot = companion_input_context(controller, 1U);
    if (!route::slot_context_words(
            topology, companion.port_speed, 1U,
            &slot[0U], &slot[1U]))
        return false;
    companion.ep0_packet_size = initial_packet_size(companion.port_speed);
    uint32_t* ep0 = companion_input_context(controller, 2U);
    ep0[1U] = UINT32_C(3) << 1U | UINT32_C(4) << 3U |
        static_cast<uint32_t>(companion.ep0_packet_size) << 16U;
    ep0[2U] = static_cast<uint32_t>(
        companion.ep0_ring.page.physical_address) | 1U;
    ep0[3U] = static_cast<uint32_t>(
        companion.ep0_ring.page.physical_address >> 32U);
    ep0[4U] = 8U;

    return submit_command(
        controller,
        companion.input_context_page.physical_address,
        0U,
        static_cast<uint32_t>(TRB_ADDRESS_DEVICE) << 10U |
            static_cast<uint32_t>(companion.slot_id) << 24U,
        &completion);
}

bool companion_control_transfer(
    Controller& controller,
    uint8_t request_type,
    uint8_t request,
    uint16_t value,
    uint16_t index,
    uint16_t length,
    bool direction_in) {
    auto& companion = controller.companion;
    const uint32_t transfer_type = length == 0U
        ? 0U
        : (direction_in ? 3U : 2U);
    enqueue_trb(
        companion.ep0_ring,
        setup_packet(request_type, request, value, index, length),
        8U,
        static_cast<uint32_t>(TRB_SETUP_STAGE) << 10U |
            UINT32_C(1) << 6U | transfer_type << 16U);
    if (length != 0U) {
        enqueue_trb(
            companion.ep0_ring,
            companion.data_page.physical_address,
            length,
            static_cast<uint32_t>(TRB_DATA_STAGE) << 10U |
                (direction_in ? UINT32_C(1) << 16U : 0U));
    }
    const bool status_in = length == 0U || !direction_in;
    const uint64_t status_trb = enqueue_trb(
        companion.ep0_ring,
        0U,
        0U,
        static_cast<uint32_t>(TRB_STATUS_STAGE) << 10U |
            UINT32_C(1) << 5U |
            (status_in ? UINT32_C(1) << 16U : 0U));
    controller.doorbells[companion.slot_id] = 1U;
    Trb completion{};
    return wait_event(
               controller, TRB_TRANSFER_EVENT, status_trb, &completion) &&
        completion_ok(completion) &&
        static_cast<uint8_t>(completion.control >> 24U) ==
            companion.slot_id;
}

bool wait_hpet_us(uint32_t microseconds) {
    namespace timer = arch::x86_64::hpet;
    const auto* caps = timer::capabilities();
    if (caps == nullptr || caps->counter_period_femtoseconds == 0U) return false;
    const uint64_t period = caps->counter_period_femtoseconds;
    const uint64_t ticks =
        (static_cast<uint64_t>(microseconds) * UINT64_C(1000000000) + period - 1U) / period;
    if (!caps->counter_64_bit && ticks > UINT32_MAX) return false;
    const uint64_t start = timer::counter();
    for (unsigned attempt = 0U; attempt < 20000000U; ++attempt) {
        const uint64_t delta = timer::counter() - start;
        if ((caps->counter_64_bit ? delta : static_cast<uint32_t>(delta)) >= ticks) return true;
        relax();
    }
    return false;
}

bool update_companion_ep0_packet_size(
    Controller& controller,
    uint16_t packet_size) {
    auto& companion = controller.companion;
    if (packet_size == companion.ep0_packet_size) return true;
    clear_bytes(
        companion.input_context_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    companion_input_context(controller, 0U)[1U] = UINT32_C(1) << 1U;
    copy_bytes(
        companion_input_context(controller, 2U),
        companion_output_context(controller, 1U),
        controller.context_size);
    companion_input_context(controller, 2U)[1U] &= UINT32_C(0x0000FFFF);
    companion_input_context(controller, 2U)[1U] |=
        static_cast<uint32_t>(packet_size) << 16U;
    Trb completion{};
    if (!submit_command(
            controller,
            companion.input_context_page.physical_address,
            0U,
            static_cast<uint32_t>(TRB_EVALUATE_CONTEXT) << 10U |
                static_cast<uint32_t>(companion.slot_id) << 24U,
            &completion)) {
        return false;
    }
    companion.ep0_packet_size = packet_size;
    return true;
}

bool read_companion_descriptors(Controller& controller, bool& is_hub) {
    auto& companion = controller.companion;
    clear_bytes(
        companion.data_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    if (!companion_control_transfer(
            controller, 0x80U, 6U, 0x0100U, 0U, 8U, true)) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(
        companion.data_page.virtual_address);
    if (bytes[0U] < 18U || bytes[1U] != 1U) return false;
    uint16_t packet_size = bytes[7U];
    if (companion.port_speed == 4U) {
        if (packet_size > 9U) return false;
        packet_size = static_cast<uint16_t>(UINT16_C(1) << packet_size);
    }
    if (packet_size < 8U || packet_size > 512U ||
        !update_companion_ep0_packet_size(controller, packet_size)) {
        return false;
    }

    clear_bytes(
        companion.data_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    if (!companion_control_transfer(
            controller, 0x80U, 6U, 0x0100U, 0U, 18U, true)) {
        return false;
    }
    bytes = static_cast<const uint8_t*>(
        companion.data_page.virtual_address);
    if (bytes[0U] < 18U || bytes[1U] != 1U) return false;
    is_hub = bytes[4U] == USB_HUB_CLASS;
    if (is_hub) return false;
    companion.vendor_id = static_cast<uint16_t>(bytes[8U]) |
        static_cast<uint16_t>(bytes[9U]) << 8U;
    companion.product_id = static_cast<uint16_t>(bytes[10U]) |
        static_cast<uint16_t>(bytes[11U]) << 8U;

    clear_bytes(
        companion.data_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    if (!companion_control_transfer(
            controller, 0x80U, 6U, 0x0200U, 0U, 9U, true)) {
        return false;
    }
    bytes = static_cast<const uint8_t*>(
        companion.data_page.virtual_address);
    const uint16_t total = static_cast<uint16_t>(bytes[2U]) |
        static_cast<uint16_t>(bytes[3U]) << 8U;
    if (bytes[0U] < 9U || bytes[1U] != 2U ||
        total < 9U || total > 512U) {
        return false;
    }
    clear_bytes(
        companion.data_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    if (!companion_control_transfer(
            controller, 0x80U, 6U, 0x0200U, 0U, total, true)) {
        return false;
    }
    const auto* configuration = static_cast<const uint8_t*>(
        companion.data_page.virtual_address);
    companion.hid_kind = HidKind::None;
    if (find_boot_keyboard_interface(
            configuration, total, &companion.keyboard_interface)) {
        companion.hid_kind = HidKind::Keyboard;
        return true;
    }
    if (find_boot_mouse_interface(
            configuration, total, &companion.mouse_interface)) {
        companion.hid_kind = HidKind::Mouse;
        return true;
    }

    HidReportInterface report_interface{};
    if (find_hid_report_interface(
            configuration, total, &report_interface) &&
        report_interface.report_descriptor_length <=
            memory::virtual_memory::PAGE_SIZE) {
        companion.report_interface = report_interface;
        if (companion_control_transfer(
                controller, 0x00U, 9U,
                report_interface.configuration_value,
                0U, 0U, false)) {
            clear_bytes(
                companion.data_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
            if (companion_control_transfer(
                    controller, 0x81U, 6U, 0x2200U,
                    report_interface.interface_number,
                    report_interface.report_descriptor_length,
                    true) &&
                hid::parse_pointer_report_descriptor(
                    static_cast<const uint8_t*>(
                        companion.data_page.virtual_address),
                    report_interface.report_descriptor_length,
                    &companion.pointer_layout)) {
                companion.hid_kind = HidKind::ReportPointer;
                return true;
            }
        }
    }
    return false;
}

bool configure_companion_hid_endpoint(Controller& controller) {
    auto& companion = controller.companion;
    HidInterruptEndpoint hid{};
    if (companion.hid_kind == HidKind::Keyboard) {
        hid = {
            companion.keyboard_interface.configuration_value,
            companion.keyboard_interface.interface_number,
            companion.keyboard_interface.endpoint_address,
            companion.keyboard_interface.maximum_packet_size,
            companion.keyboard_interface.interval,
            8U,
        };
    } else if (companion.hid_kind == HidKind::Mouse) {
        hid = {
            companion.mouse_interface.configuration_value,
            companion.mouse_interface.interface_number,
            companion.mouse_interface.endpoint_address,
            companion.mouse_interface.maximum_packet_size,
            companion.mouse_interface.interval,
            3U,
        };
    } else if (companion.hid_kind == HidKind::ReportPointer) {
        hid = {
            companion.report_interface.configuration_value,
            companion.report_interface.interface_number,
            companion.report_interface.endpoint_address,
            companion.report_interface.maximum_packet_size,
            companion.report_interface.interval,
            companion.pointer_layout.transfer_bytes,
        };
    } else {
        return false;
    }

    if (!companion_control_transfer(
            controller, 0x00U, 9U, hid.configuration_value,
            0U, 0U, false)) {
        return false;
    }
    static_cast<void>(companion_control_transfer(
        controller, 0x21U, 0x0BU, 0U,
        hid.interface_number, 0U, false));
    static_cast<void>(companion_control_transfer(
        controller, 0x21U, 0x0AU, 0U,
        hid.interface_number, 0U, false));

    const uint8_t endpoint_number = hid.endpoint_address & 0x0FU;
    companion.interrupt_dci = static_cast<uint8_t>(
        endpoint_number * 2U + 1U);
    if (endpoint_number == 0U || companion.interrupt_dci >= 32U ||
        hid.transfer_size == 0U ||
        hid.transfer_size > hid.maximum_packet_size) {
        return false;
    }
    companion.interrupt_packet_size = hid.transfer_size;

    clear_bytes(
        companion.input_context_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    companion_input_context(controller, 0U)[1U] = UINT32_C(1) |
        (UINT32_C(1) << companion.interrupt_dci);
    copy_bytes(
        companion_input_context(controller, 1U),
        companion_output_context(controller, 0U),
        controller.context_size);
    uint32_t* slot = companion_input_context(controller, 1U);
    slot[0U] &= ~(UINT32_C(0x1F) << 27U);
    slot[0U] |= static_cast<uint32_t>(
        companion.interrupt_dci) << 27U;

    uint32_t* endpoint = companion_input_context(
        controller,
        static_cast<size_t>(companion.interrupt_dci) + 1U);
    endpoint[0U] = static_cast<uint32_t>(
        endpoint_interval(companion.port_speed, hid.interval)) << 16U;
    endpoint[1U] = UINT32_C(3) << 1U | UINT32_C(7) << 3U |
        static_cast<uint32_t>(hid.maximum_packet_size) << 16U;
    endpoint[2U] = static_cast<uint32_t>(
        companion.interrupt_ring.page.physical_address) | 1U;
    endpoint[3U] = static_cast<uint32_t>(
        companion.interrupt_ring.page.physical_address >> 32U);
    endpoint[4U] = companion.interrupt_packet_size;

    Trb completion{};
    return submit_command(
        controller,
        companion.input_context_page.physical_address,
        0U,
        static_cast<uint32_t>(TRB_CONFIGURE_ENDPOINT) << 10U |
            static_cast<uint32_t>(companion.slot_id) << 24U,
        &completion);
}

bool register_companion_hid(Controller& controller) {
    auto& companion = controller.companion;
    if (companion.hid_kind != HidKind::Keyboard &&
        companion.hid_kind != HidKind::Mouse &&
        companion.hid_kind != HidKind::ReportPointer) {
        return false;
    }
    const bool keyboard_kind =
        companion.hid_kind == HidKind::Keyboard;
    const bool report_pointer =
        companion.hid_kind == HidKind::ReportPointer;
    const device::Descriptor descriptor{
        device::Type::Input,
        device::Bus::Usb,
        keyboard_kind
            ? "USB HID boot keyboard"
            : (report_pointer
                ? "USB HID report pointer"
                : "USB HID boot mouse"),
        companion.vendor_id,
        companion.product_id,
        3U,
        static_cast<uint8_t>(report_pointer ? 0U : 1U),
        static_cast<uint8_t>(keyboard_kind ? 1U :
            (report_pointer ? 0U : 2U)),
        {0U, 0U, companion.port_id, 0U},
        controller.parent_device,
        nullptr,
        0U,
    };
    device::DeviceId id = device::INVALID_DEVICE_ID;
    if (device::register_device(descriptor, &id) != KStatus::Ok) {
        return false;
    }
    companion.child_device = id;
    if (device::claim(
            id, controller.owner_driver,
            report_pointer ? "usb-hid-report" : "usb-hid-boot") != KStatus::Ok ||
        device::set_status(id, device::Status::Ready) != KStatus::Ok) {
        return false;
    }
    return true;
}

bool queue_companion_report(Controller& controller) {
    auto& companion = controller.companion;
    if (companion.report_queued) return true;
    if (companion.hid_kind == HidKind::Keyboard &&
        companion.pending_key_count != 0U) {
        return false;
    }
    if (companion.hid_kind == HidKind::Mouse &&
        companion.pending_mouse_valid) {
        return false;
    }
    if (companion.hid_kind == HidKind::ReportPointer &&
        companion.pending_pointer_valid) {
        return false;
    }
    if (companion.hid_kind == HidKind::None ||
        companion.interrupt_packet_size == 0U) {
        return false;
    }
    clear_bytes(
        companion.data_page.virtual_address,
        companion.interrupt_packet_size);
    companion.report_trb = enqueue_trb(
        companion.interrupt_ring,
        companion.data_page.physical_address,
        companion.interrupt_packet_size,
        static_cast<uint32_t>(TRB_NORMAL) << 10U |
            UINT32_C(1) << 5U);
    companion.report_queued = true;
    controller.doorbells[companion.slot_id] =
        companion.interrupt_dci;
    return true;
}

void record_companion_keyboard_input(
    CompanionHid& companion,
    const keyboard::KeyEvent& event) {
    if (!companion.input_proven && event.pressed) {
        companion.input_proven = true;
        terminal::println("[TEST] usb_hid_keyboard_input: PASS");
    }
}

bool flush_companion_keyboard(CompanionHid& companion) {
    while (companion.pending_key_index <
           companion.pending_key_count) {
        const auto& event =
            companion.pending_keys[companion.pending_key_index];
        if (!input::submit_key(event)) return false;
        record_companion_keyboard_input(companion, event);
        ++companion.pending_key_index;
    }
    companion.pending_key_index = 0U;
    companion.pending_key_count = 0U;
    return true;
}

void record_companion_mouse_input(
    CompanionHid& companion,
    const mouse::Sample& sample) {
    if (!companion.input_proven &&
        (sample.delta_x != 0 || sample.delta_y != 0 ||
         sample.wheel != 0 || sample.changed_buttons != 0U)) {
        companion.input_proven = true;
        terminal::println("[TEST] usb_hid_mouse_input: PASS");
    }
}

bool flush_companion_mouse(CompanionHid& companion) {
    if (!companion.pending_mouse_valid) return true;
    if (!input::submit_mouse(companion.pending_mouse)) return false;
    record_companion_mouse_input(
        companion, companion.pending_mouse);
    companion.pending_mouse_valid = false;
    return true;
}

void record_companion_pointer_input(
    CompanionHid& companion,
    const hid::PointerReport& report) {
    if (!companion.input_proven &&
        (report.x != 0 || report.y != 0 || report.wheel != 0 ||
         report.changed_buttons != 0U)) {
        companion.input_proven = true;
        terminal::println("[TEST] usb_hid_mouse_input: PASS");
        terminal::println("[TEST] usb_hid_report_pointer_input: PASS");
    }
}

bool flush_companion_pointer(CompanionHid& companion) {
    if (!companion.pending_pointer_valid) return true;
    if (!submit_pointer_report(companion.pending_pointer)) return false;
    record_companion_pointer_input(
        companion, companion.pending_pointer);
    companion.pending_pointer_valid = false;
    return true;
}

bool flush_companion_input(CompanionHid& companion) {
    if (companion.hid_kind == HidKind::Keyboard) {
        return flush_companion_keyboard(companion);
    }
    if (companion.hid_kind == HidKind::Mouse) {
        return flush_companion_mouse(companion);
    }
    if (companion.hid_kind == HidKind::ReportPointer) {
        return flush_companion_pointer(companion);
    }
    return true;
}

void handle_companion_keyboard_report(
    Controller& controller,
    const Trb& event) {
    auto& companion = controller.companion;
    if (!companion.report_queued ||
        (event.control & (1U << 2U)) != 0U ||
        event.parameter != companion.report_trb) {
        return;
    }
    companion.report_queued = false;
    companion.report_trb = 0U;
    const uint32_t remaining = event.status & 0x00FFFFFFU;
    const size_t actual =
        remaining <= companion.interrupt_packet_size
            ? companion.interrupt_packet_size - remaining
            : 0U;
    if (completion_ok(event) && actual >= 8U) {
        if (decode_boot_keyboard_report(
                &companion.keyboard_decoder,
                static_cast<const uint8_t*>(
                    companion.data_page.virtual_address),
                actual,
                companion.pending_keys,
                MAXIMUM_KEYBOARD_EVENTS_PER_REPORT,
                &companion.pending_key_count)) {
            ++companion.reports;
        }
    }
    if (flush_companion_keyboard(companion)) {
        static_cast<void>(queue_companion_report(controller));
    }
}

void handle_companion_mouse_report(
    Controller& controller,
    const Trb& event) {
    auto& companion = controller.companion;
    if (!companion.report_queued ||
        (event.control & (1U << 2U)) != 0U ||
        event.parameter != companion.report_trb) {
        return;
    }
    companion.report_queued = false;
    companion.report_trb = 0U;
    const uint32_t remaining = event.status & 0x00FFFFFFU;
    const size_t actual =
        remaining <= companion.interrupt_packet_size
            ? companion.interrupt_packet_size - remaining
            : 0U;
    if (completion_ok(event) && actual >= 3U) {
        mouse::Sample sample{};
        if (decode_boot_mouse_report(
                &companion.mouse_decoder,
                static_cast<const uint8_t*>(
                    companion.data_page.virtual_address),
                actual,
                &sample)) {
            companion.pending_mouse = sample;
            companion.pending_mouse_valid = true;
            ++companion.reports;
        }
    }
    if (flush_companion_mouse(companion)) {
        static_cast<void>(queue_companion_report(controller));
    }
}

void handle_companion_pointer_report(
    Controller& controller,
    const Trb& event) {
    auto& companion = controller.companion;
    if (!companion.report_queued ||
        (event.control & (1U << 2U)) != 0U ||
        event.parameter != companion.report_trb) {
        return;
    }
    companion.report_queued = false;
    companion.report_trb = 0U;
    const uint32_t remaining = event.status & 0x00FFFFFFU;
    const size_t actual =
        remaining <= companion.interrupt_packet_size
            ? companion.interrupt_packet_size - remaining
            : 0U;
    if (completion_ok(event) &&
        actual >= companion.pointer_layout.report_bytes) {
        hid::PointerReport report{};
        if (hid::decode_pointer_report(
                companion.pointer_layout,
                &companion.pointer_decoder,
                static_cast<const uint8_t*>(
                    companion.data_page.virtual_address),
                actual,
                &report)) {
            companion.pending_pointer = report;
            companion.pending_pointer_valid = true;
            ++companion.reports;
        }
    }
    if (flush_companion_pointer(companion)) {
        static_cast<void>(queue_companion_report(controller));
    }
}

void fail_hotplug(Controller& controller, Status reason);

bool retire_companion_slot(Controller& controller) {
    auto& companion = controller.companion;
    if (!disable_slot(controller, companion.slot_id)) {
        fail_hotplug(controller, Status::CommandFailed);
        return false;
    }
    if (remove_companion_device(&controller) != Status::Ok) {
        companion.hid_lifecycle = HidLifecycle::Failed;
        return false;
    }

    companion.port_id = 0U;
    companion.port_speed = 0U;
    companion.interrupt_dci = 0U;
    companion.vendor_id = 0U;
    companion.product_id = 0U;
    companion.ep0_packet_size = 0U;
    companion.interrupt_packet_size = 0U;
    companion.hid_kind = HidKind::None;
    companion.keyboard_interface = {};
    companion.mouse_interface = {};
    companion.report_interface = {};
    companion.pointer_layout = {};
    companion.keyboard_decoder = {};
    companion.mouse_decoder = {};
    companion.pointer_decoder = {};
    companion.pending_key_count = 0U;
    companion.pending_key_index = 0U;
    companion.pending_mouse = {};
    companion.pending_mouse_valid = false;
    companion.pending_pointer = {};
    companion.pending_pointer_valid = false;
    companion.report_queued = false;
    companion.report_trb = 0U;
    companion.input_proven = false;
    reset_ring(&companion.ep0_ring);
    reset_ring(&companion.interrupt_ring);
    clear_bytes(
        companion.input_context_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    clear_bytes(
        companion.device_context_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    clear_bytes(
        companion.data_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    companion.hid_lifecycle = HidLifecycle::WaitingForDevice;
    return true;
}

void mark_multi_hid_if_ready(Controller& controller) {
    if (controller.multi_hid_proven) return;
    const bool primary_keyboard =
        controller.hid_lifecycle == HidLifecycle::Active &&
        controller.hid_kind == HidKind::Keyboard;
    const bool primary_mouse =
        controller.hid_lifecycle == HidLifecycle::Active &&
        (controller.hid_kind == HidKind::Mouse ||
         controller.hid_kind == HidKind::ReportPointer);
    const bool companion_keyboard =
        controller.companion.hid_lifecycle ==
            HidLifecycle::Active &&
        controller.companion.hid_kind == HidKind::Keyboard;
    const bool companion_mouse =
        controller.companion.hid_lifecycle ==
            HidLifecycle::Active &&
        (controller.companion.hid_kind == HidKind::Mouse ||
         controller.companion.hid_kind == HidKind::ReportPointer);
    if ((primary_keyboard && companion_mouse) ||
        (primary_mouse && companion_keyboard)) {
        controller.multi_hid_proven = true;
        terminal::println("[TEST] xhci_multi_hid_enumeration: PASS");
    }
}

bool configure_full_speed_hub_slot(Controller& controller, uint8_t port_count) {
    clear_bytes(controller.input_context_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
    input_context(controller, 0U)[1U] = 1U; // Add only the slot context.
    uint32_t* slot = input_context(controller, 1U);
    copy_bytes(slot, output_context(controller, 0U), controller.context_size);
    slot[0U] |= UINT32_C(1) << 26U; // Hub
    slot[0U] &= ~(UINT32_C(1) << 25U); // Full speed has no TT.
    slot[1U] = (slot[1U] & UINT32_C(0x00FFFFFF)) |
        static_cast<uint32_t>(port_count) << 24U;
    slot[3U] = 0U; // Device address and slot state are controller-owned.
    Trb completion{};
    return submit_command(controller, controller.input_context_page.physical_address,
        0U, static_cast<uint32_t>(TRB_CONFIGURE_ENDPOINT) << 10U |
            static_cast<uint32_t>(controller.slot_id) << 24U, &completion);
}

uint32_t read_downstream_identity(Controller& controller) {
    auto* bytes = static_cast<uint8_t*>(controller.companion.data_page.virtual_address);
    clear_bytes(bytes, 18U);
    if (!companion_control_transfer(controller, 0x80U, 6U, 0x0100U, 0U, 8U, true) ||
        bytes[0U] != 18U || bytes[1U] != 1U) return 0U;
    const uint8_t packet = bytes[7U];
    if (packet != 8U && (controller.companion.port_speed == 2U ||
        (packet != 16U && packet != 32U && packet != 64U))) return 0U;
    if (!update_companion_ep0_packet_size(controller, packet)) return 0U;

    clear_bytes(bytes, 18U);
    if (!companion_control_transfer(controller, 0x80U, 6U, 0x0100U, 0U, 18U, true) ||
        bytes[0U] != 18U || bytes[1U] != 1U || bytes[7U] != packet ||
        bytes[17U] == 0U) return 0U;
    const uint32_t vendor = bytes[8U] | static_cast<uint32_t>(bytes[9U]) << 8U;
    const uint32_t product = bytes[10U] | static_cast<uint32_t>(bytes[11U]) << 8U;
    return vendor == 0U || vendor == UINT16_MAX ? 0U : (vendor << 16U) | product;
}

enum class DownstreamStorageProbe : uint8_t {
    None, UnsupportedInterface, Inquiry, BlockRead, KnownSector, ScratchFailed, ScratchRestored,
};

DownstreamStorageProbe run_downstream_scratch(Controller& controller, const BulkTarget& target,
    uint64_t sector_count) {
    using Probe = DownstreamStorageProbe;
    namespace block = storage::block;
    namespace scratch = storage::scratch_test;
    struct ScratchView {
        Controller& controller;
        const BulkTarget& target;
    };
    ScratchView view{controller, target};
    // The unified scratch header is on physical LBA 1. The LBA 0 read probe
    // remains intact, and the scratch runner rejects writes over its header.
    const block::Device device{
        &view, scratch::SCRATCH_SECTOR_SIZE, sector_count - 1U,
        [](void* context, uint64_t first, uint64_t count, void* destination) {
            auto& state = *static_cast<ScratchView*>(context);
            return read_scsi_chunk(state.controller, state.target,
                static_cast<uint32_t>(first + 1U), static_cast<uint16_t>(count),
                static_cast<size_t>(count) * scratch::SCRATCH_SECTOR_SIZE, destination);
        },
        [](void* context, uint64_t first, uint64_t count, const void* source) {
            auto& state = *static_cast<ScratchView*>(context);
            return write_scsi_chunk(state.controller, state.target,
                static_cast<uint32_t>(first + 1U), static_cast<uint16_t>(count),
                static_cast<size_t>(count) * scratch::SCRATCH_SECTOR_SIZE, source);
        },
        [](void* context) {
            auto& state = *static_cast<ScratchView*>(context);
            return synchronize_scsi_cache(state.controller, state.target);
        },
    };
    // The bounded probe runs once during initialization, before publication.
    static scratch::Workspace workspace{};
    const auto result = scratch::run(&device, &workspace);
    if (result.status == scratch::Status::NotTagged) return Probe::KnownSector;
    if (result.status == scratch::Status::Ok && result.write_attempted && result.restored) {
        return Probe::ScratchRestored;
    }
    log::write(log::Level::Warn, "XHCI", scratch::status_message(result.status));
    log::write(log::Level::Warn, "XHCI", block::status_message(result.block_status));
    return Probe::ScratchFailed;
}

DownstreamStorageProbe probe_downstream_storage(Controller& controller, uint32_t identity) {
    using Probe = DownstreamStorageProbe;
    auto& companion = controller.companion;
    if (companion.port_speed != 1U) return Probe::None; // Full-speed bulk only.
    auto* bytes = static_cast<uint8_t*>(companion.data_page.virtual_address);
    clear_bytes(bytes, 9U);
    if (!companion_control_transfer(controller, 0x80U, 6U, 0x0200U, 0U, 9U, true) ||
        bytes[0U] != 9U || bytes[1U] != 2U) return Probe::None;
    const uint16_t length = static_cast<uint16_t>(bytes[2U] |
        static_cast<uint16_t>(bytes[3U]) << 8U);
    if (length < 9U || length > 512U) return Probe::None;

    clear_bytes(bytes, length);
    mass_storage::BulkOnlyInterface interface{};
    if (!companion_control_transfer(controller, 0x80U, 6U, 0x0200U, 0U, length, true))
        return Probe::None;
    if (!mass_storage::find_bulk_only_scsi_interface(bytes, length, &interface))
        return Probe::UnsupportedInterface;
    const auto valid_packet = [](uint16_t packet) {
        return packet == 8U || packet == 16U || packet == 32U || packet == 64U;
    };
    if (!valid_packet(interface.bulk_in_maximum_packet_size) ||
        !valid_packet(interface.bulk_out_maximum_packet_size) ||
        !companion_control_transfer(controller, 0x00U, 9U, interface.configuration_value,
            0U, 0U, false)) return Probe::None;

    BulkTarget target{companion.data_page, companion.slot_id, 0U, 0U};
    if (!configure_bulk_endpoints(controller, interface, companion.input_context_page,
            companion_output_context(controller, 0U), target)) return Probe::None;
    uint8_t inquiry[SCSI_INQUIRY_BYTES]{};
    if (!read_scsi_inquiry(controller, target, inquiry) ||
        inquiry[0U] != 0U || inquiry[4U] < sizeof(inquiry) - 5U) return Probe::None;

    mass_storage::scsi::ReadCapacity10Data geometry{};
    if (!test_mass_storage_ready(controller, target) ||
        !read_scsi_capacity(controller, target, geometry)) return Probe::Inquiry;

    auto* data = static_cast<uint8_t*>(controller.storage_io_page.virtual_address);
    if (read_scsi_chunk(controller, target, 0U, 1U, geometry.block_size, data) !=
        storage::block::Status::Ok) return Probe::Inquiry;
    log::write_u64(log::Level::Info, "XHCI",
        "USB2 downstream block count: ", geometry.block_count);
    if (geometry.block_count != UINT64_C(16384) ||
        !matches_usb_test_sector(data, geometry.block_size)) return Probe::BlockRead;
    // Only the explicitly tagged QEMU fixture reaches the write path.
    constexpr uint32_t qemu_storage_identity = UINT32_C(0x46F40001);
    if (identity != qemu_storage_identity) return Probe::KnownSector;
    return run_downstream_scratch(controller, target, geometry.block_count);
}

// Reuse the unbound companion's buffers and the unused bulk rings during
// cold boot. Retire the child before recycling any DMA or disabling its port.
bool probe_downstream_device(Controller& controller, uint8_t port, uint8_t speed) {
    route::Address child{};
    if (!route::extend({controller.port_id, 0U, 0U}, port, &child)) return true;
    auto& companion = controller.companion;
    companion.port_id = child.root_port;
    companion.port_speed = speed;
    const bool addressed = address_companion_device(controller, child);
    const uint32_t identity = addressed ? read_downstream_identity(controller) : 0U;
    const auto storage = identity == 0U ? DownstreamStorageProbe::None :
        probe_downstream_storage(controller, identity);
    if (!retire_companion_slot(controller)) return false;
    reset_ring(&controller.bulk_in_ring);
    reset_ring(&controller.bulk_out_ring);
    if (identity != 0U) {
        log::write_u64(log::Level::Info, "XHCI",
            "USB2 downstream descriptor read; slot retired; VID/PID: ", identity);
    }
    if (storage == DownstreamStorageProbe::UnsupportedInterface) {
        log::write_u64(log::Level::Warn, "XHCI",
            "USB2 unsupported downstream interface; slot retired at port: ", port);
    }
    if (storage >= DownstreamStorageProbe::Inquiry) {
        terminal::println("[TEST] xhci_downstream_scsi_inquiry: PASS");
    }
    if (storage >= DownstreamStorageProbe::BlockRead) {
        terminal::println("[TEST] xhci_downstream_read10: PASS");
    }
    if (storage >= DownstreamStorageProbe::KnownSector) {
        terminal::println("[TEST] xhci_downstream_known_sector: PASS");
    }
    if (storage == DownstreamStorageProbe::ScratchRestored) {
        terminal::println("[TEST] xhci_downstream_write_flush_readback_restore: PASS");
    } else if (storage == DownstreamStorageProbe::ScratchFailed) {
        terminal::println("[TEST] xhci_downstream_write_flush_readback_restore: FAIL");
    }
    return true;
}

// Return false when hub state or child slot retirement cannot be verified.
bool probe_usb2_hub_ports(Controller& controller, bool companion) {
    const uint8_t speed = companion
        ? controller.companion.port_speed : controller.port_speed;
    if (speed != 1U && speed != 3U) return true; // USB 1.x/2.0 full or high speed.
    const auto transfer = companion ? companion_control_transfer : control_transfer;
    const auto& page = companion ? controller.companion.data_page : controller.data_page;
    auto* bytes = static_cast<uint8_t*>(page.virtual_address);

    clear_bytes(bytes, 9U);
    if (!transfer(controller, 0x80U, 6U, 0x0200U, 0U, 9U, true) ||
        bytes[0U] != 9U || bytes[1U] != 2U || bytes[5U] == 0U ||
        (bytes[3U] == 0U && bytes[2U] < 9U) || (bytes[7U] & 0x80U) == 0U) return true;
    const uint8_t configuration_value = bytes[5U];
    if (!transfer(controller, 0x00U, 9U, configuration_value, 0U, 0U, false)) return true;

    constexpr uint16_t descriptor = UINT16_C(0x2900);
    clear_bytes(bytes, 7U);
    if (!transfer(controller, 0xA0U, 6U, descriptor, 0U, 7U, true)) return true;
    const uint8_t ports = bytes[2U];
    if (ports == 0U || ports > hub::MAX_ROUTE_PORTS) return true;
    const auto length = static_cast<uint16_t>(
        7U + 2U * hub::descriptor_bitmap_bytes(ports));
    if (bytes[0U] != length || bytes[1U] != hub::DESCRIPTOR_TYPE_USB2_HUB) return true;

    clear_bytes(bytes, length);
    hub::Descriptor parsed{};
    if (!transfer(controller, 0xA0U, 6U, descriptor, 0U, length, true) ||
        hub::parse_usb2_descriptor(bytes, length, &parsed) != hub::ParseStatus::Ok ||
        parsed.port_count != ports) return true;
    terminal::println("[USB] USB2 hub descriptor read");

    // Limit synchronous resets to the first hub seen at cold boot. Its
    // descriptor bounds this loop to 15 ports, each visited once.
    const bool probe_ports = !controller.initialized &&
        !controller.hub_reset_attempted &&
        arch::x86_64::hpet::capabilities() != nullptr;
    if (probe_ports) controller.hub_reset_attempted = true;
    const auto exchange_port = [&](uint8_t port, hub::PortRequest kind, hub::PortStatus* status) {
        hub::ControlRequest request{};
        if (!hub::make_port_request(parsed.port_count, port, kind, &request)) return false;
        for (size_t i = 0U; i < request.length; ++i) bytes[i] = UINT8_MAX;
        if (!transfer(controller, request.request_type, request.request,
                request.value, request.index, request.length, request.length != 0U)) {
            return false;
        }
        return request.length == 0U ||
            hub::parse_port_status(bytes, request.length, status) == hub::PortParse::Ok;
    };
    if (probe_ports && parsed.power_mode == hub::PowerMode::Ganged) {
        const auto power = hub::power_ganged_hub_for_probe(parsed, exchange_port, wait_hpet_us);
        if (power == hub::PowerResult::UnsafePort) {
            log::write(log::Level::Error, "XHCI", "USB2 hub shared power unverified; stopping controller");
            return false;
        }
        if (power == hub::PowerResult::Unavailable) {
            log::write(log::Level::Warn, "XHCI", "USB2 hub shared power deferred; group is not inactive");
            return true;
        }
        log::write(log::Level::Info, "XHCI", "USB2 hub shared power group ready");
    }
    for (uint8_t port = 1U; port <= parsed.port_count; ++port) {
        const auto exchange = [&](hub::PortRequest kind, hub::PortStatus* status) {
            return exchange_port(port, kind, status);
        };
        hub::PortStatus status{};
        if (!exchange(hub::PortRequest::GetStatus, &status)) {
            log::write_u64(log::Level::Error, "XHCI",
                "USB2 hub port status unverified; stopping controller at port: ", port);
            return false;
        }
        if (!controller.initialized &&
            (status.status & hub::PORT_POWERED) == 0U &&
            arch::x86_64::hpet::capabilities() != nullptr) {
            const auto power = hub::power_port_for_probe(parsed, status, exchange, wait_hpet_us);
            if (power == hub::PowerResult::UnsafePort) {
                log::write_u64(log::Level::Error, "XHCI",
                    "USB2 hub port power unverified; stopping controller at port: ", port);
                return false;
            }
            if (power == hub::PowerResult::Unavailable) {
                log::write_u64(log::Level::Warn, "XHCI",
                    "USB2 hub port power unavailable: ", port);
                continue;
            }
            log::write_u64(log::Level::Info, "XHCI",
                "USB2 hub port power verified: ", port);
        }
        if ((status.status & hub::PORT_CONNECTED) != 0U) {
            log::write_u64(log::Level::Info, "XHCI",
                "USB2 hub downstream port connected: ", port);
        }
        // Do not wait on the timer inside the normal HID polling path.
        if (probe_ports && hub::inactive_reset_candidate(status)) {
            const auto after_reset = [&] {
                // This first topology has no high-speed transaction translator.
                if (companion || speed != 1U || controller.maximum_slots < 2U ||
                    controller.companion.slot_id != 0U ||
                    (read32(controller.registers, 0U) >> 16U) < 0x0100U ||
                    !configure_full_speed_hub_slot(controller, parsed.port_count) ||
                    !wait_hpet_us(10000U)) return true;
                hub::PortStatus ready{};
                constexpr uint16_t required = hub::PORT_POWERED | hub::PORT_CONNECTED |
                    hub::PORT_ENABLED;
                constexpr uint16_t blocked = hub::PORT_OVER_CURRENT | hub::PORT_SUSPENDED |
                    hub::PORT_RESET | hub::PORT_HIGH_SPEED;
                if (!exchange(hub::PortRequest::GetStatus, &ready) ||
                    (ready.status & required) != required || (ready.status & blocked) != 0U ||
                    (ready.changed & ~hub::CHANGE_ENABLE) != 0U) return true;
                return probe_downstream_device(controller, port,
                    (ready.status & hub::PORT_LOW_SPEED) != 0U ? 2U : 1U);
            };
            const auto result = hub::probe_port_reset(
                status, exchange, wait_hpet_us, after_reset);
            if (result == hub::ResetResult::UnsafePort) {
                log::write_u64(log::Level::Error, "XHCI",
                    "USB2 hub port cleanup unverified; stopping controller at port: ", port);
                return false;
            }
            if (result == hub::ResetResult::Verified) {
                log::write_u64(log::Level::Info, "XHCI",
                    "USB2 hub downstream reset and disable verified: ", port);
            } else {
                log::write_u64(log::Level::Warn, "XHCI",
                    "USB2 hub downstream reset probe incomplete: ", port);
            }
        }
    }
    return true;
}

void finish_unsupported_companion_probe(Controller& controller, bool is_hub,
                                        bool probe_safe) {
    if (!probe_safe) {
        fail_hotplug(controller, Status::CommandFailed);
        return;
    }

    const uint8_t port = controller.companion.port_id;
    if (!retire_companion_slot(controller)) return;
    if (is_hub) {
        controller.deferred_hub_ports[port] = true;
        terminal::println("[USB] external hub deferred; slot retired");
    } else {
        controller.companion.ignored_port_id = port;
    }
}

bool attach_companion_hid(Controller& controller) {
    auto& companion = controller.companion;
    if (!companion.allocated || companion.slot_id != 0U) {
        return false;
    }
    const uint8_t port =
        first_unclaimed_connected_port(controller);
    if (port == 0U) return false;

    uint8_t speed = 0U;
    if (!reset_port(controller, port, &speed)) return false;
    companion.port_id = port;
    companion.port_speed = speed;
    acknowledge_port_change(controller, port);

    if (!address_companion_device(controller, {port, 0U, 0U})) {
        static_cast<void>(retire_companion_slot(controller));
        return false;
    }
    bool is_hub = false;
    if (!read_companion_descriptors(controller, is_hub)) {
        const bool probe_safe = !is_hub || probe_usb2_hub_ports(controller, true);
        finish_unsupported_companion_probe(controller, is_hub, probe_safe);
        return false;
    }
    if (!configure_companion_hid_endpoint(controller) ||
        !register_companion_hid(controller)) {
        static_cast<void>(retire_companion_slot(controller));
        companion.hid_lifecycle = HidLifecycle::Failed;
        return false;
    }

    if (companion.hid_kind == HidKind::Keyboard) {
        reset_keyboard_decoder(&companion.keyboard_decoder);
    } else if (companion.hid_kind == HidKind::Mouse) {
        reset_mouse_decoder(&companion.mouse_decoder);
    } else {
        hid::reset_pointer_decoder(&companion.pointer_decoder);
    }
    companion.hid_lifecycle = HidLifecycle::Active;
    acknowledge_port_change(controller, companion.port_id);
    static_cast<void>(queue_companion_report(controller));

    if (companion.hid_kind == HidKind::Keyboard) {
        terminal::println("[TEST] xhci_keyboard_enumeration: PASS");
    } else {
        terminal::println("[TEST] xhci_mouse_enumeration: PASS");
        if (companion.hid_kind == HidKind::ReportPointer) {
            terminal::println("[TEST] xhci_report_pointer_enumeration: PASS");
        }
    }
    mark_multi_hid_if_ready(controller);
    return true;
}

bool progress_companion_lifecycle(Controller& controller) {
    auto& companion = controller.companion;
    if (!companion.allocated) return false;

    if (companion.hid_lifecycle == HidLifecycle::Active &&
        companion.port_id != 0U) {
        const size_t offset = OP_PORTS +
            static_cast<size_t>(
                companion.port_id - 1U) * PORT_STRIDE;
        const uint32_t port =
            read32(controller.operational, offset);
        if (port == UINT32_MAX) {
            companion.hid_lifecycle = HidLifecycle::Failed;
            return false;
        }
        if ((port & PORT_CONNECTED) == 0U ||
            (port & (UINT32_C(1) << 17U)) != 0U) {
            companion.hid_lifecycle =
                HidLifecycle::DrainInput;
            acknowledge_port_change(
                controller, companion.port_id);
        }
    }

    if (companion.hid_lifecycle ==
        HidLifecycle::DrainInput) {
        if (!flush_companion_input(companion)) return false;
        if (companion.hid_kind == HidKind::Keyboard) {
            const uint8_t released[8]{};
            static_cast<void>(decode_boot_keyboard_report(
                &companion.keyboard_decoder,
                released,
                sizeof(released),
                companion.pending_keys,
                MAXIMUM_KEYBOARD_EVENTS_PER_REPORT,
                &companion.pending_key_count));
        } else if (companion.hid_kind == HidKind::Mouse) {
            const uint8_t released[3]{};
            mouse::Sample sample{};
            if (decode_boot_mouse_report(
                    &companion.mouse_decoder,
                    released,
                    sizeof(released),
                    &sample)) {
                companion.pending_mouse = sample;
                companion.pending_mouse_valid = true;
            }
        }
        else if (companion.hid_kind == HidKind::ReportPointer) {
            const uint8_t changed = companion.pointer_decoder.previous_buttons;
            companion.pointer_decoder.previous_buttons = 0U;
            companion.pending_pointer = {
                false, 0, 0, 0, 0, 0, 0, 0,
                0U, changed
            };
            companion.pending_pointer_valid = changed != 0U;
        }
        companion.hid_lifecycle =
            HidLifecycle::ReleaseInput;
    }

    if (companion.hid_lifecycle ==
        HidLifecycle::ReleaseInput) {
        if (!flush_companion_input(companion)) return false;
        if (!retire_companion_slot(controller)) return false;
        return false;
    }

    if (companion.hid_lifecycle ==
        HidLifecycle::WaitingForDevice) {
        return attach_companion_hid(controller);
    }
    return companion.hid_lifecycle == HidLifecycle::Active;
}

void reset_primary_endpoint_memory(Controller& controller) {
    reset_ring(&controller.ep0_ring);
    reset_ring(&controller.interrupt_ring);
    reset_ring(&controller.bulk_in_ring);
    reset_ring(&controller.bulk_out_ring);
    clear_bytes(controller.device_context_page.virtual_address,
                memory::virtual_memory::PAGE_SIZE);
}

Status find_supported_root_device(Controller& controller) {
    // Retry another root port only after the controller acknowledges slot
    // retirement. Hub children still require a separate downstream path.
    for (size_t attempt = 0U; attempt < controller.maximum_ports; ++attempt) {
        const uint8_t port = first_unclaimed_connected_port(controller);
        if (port == 0U) return Status::NoDevice;
        uint8_t speed = 0U;
        if (!reset_port(controller, port, &speed)) return Status::PortResetTimeout;
        controller.port_id = port;
        controller.port_speed = speed;
        acknowledge_port_change(controller, port);
        log::write(log::Level::Info, "XHCI", "connected USB port reset completed");
        if (!address_device(controller)) return Status::CommandFailed;
        bool is_hub = false;
        if (read_descriptors(controller, is_hub)) return Status::Ok;
        if (!is_hub) return Status::HidKeyboardNotFound;
        if (!probe_usb2_hub_ports(controller, false)) return Status::CommandFailed;
        if (!disable_slot(controller, controller.slot_id)) return Status::CommandFailed;
        reset_primary_endpoint_memory(controller);
        controller.port_id = 0U;
        controller.port_speed = 0U;
        controller.deferred_hub_ports[port] = true;
        terminal::println("[USB] external hub deferred; slot retired");
    }
    return Status::NoDevice;
}

Status attach_hid_device(Controller& controller) {
    const Status found = find_supported_root_device(controller);
    if (found != Status::Ok) return found;
    log::write(log::Level::Info, "XHCI", "supported USB descriptors accepted");

    bool configured = false;
    bool registered = false;
    if (controller.hid_kind == HidKind::Keyboard) {
        configured = configure_keyboard_endpoint(controller);
        if (configured) {
            reset_keyboard_decoder(&controller.keyboard_decoder);
            registered = register_keyboard(controller);
        }
    } else if (controller.hid_kind == HidKind::Mouse) {
        configured = configure_mouse_endpoint(controller);
        if (configured) {
            reset_mouse_decoder(&controller.mouse_decoder);
            registered = register_mouse(controller);
        }
    } else if (controller.hid_kind == HidKind::ReportPointer) {
        configured = configure_report_pointer_endpoint(controller);
        if (configured) {
            hid::reset_pointer_decoder(&controller.pointer_decoder);
            registered = register_report_pointer(controller);
        }
    } else if (controller.mass_storage_present) {
        configured = configure_mass_storage_endpoints(controller);
        if (configured) {
            configured = probe_mass_storage_scsi_geometry(controller);
        }
        if (configured) {
            configured = initialize_mass_storage_block_device(controller);
        }
        if (configured) {
            registered = register_mass_storage(controller);
        }
    }
    if (!configured) return Status::CommandFailed;
    if (!registered) return Status::DeviceRegistrationFailed;

    controller.hid_lifecycle = HidLifecycle::Active;
    controller.runtime_status = Status::Ok;
    acknowledge_port_change(controller, controller.port_id);
    static_cast<void>(queue_hid_report(controller));
    if (controller.hid_kind == HidKind::Keyboard) {
        log::write(log::Level::Info, "USB", "xHCI USB HID boot keyboard ready");
        terminal::println("[TEST] xhci_keyboard_enumeration: PASS");
    } else if (controller.hid_kind == HidKind::Mouse) {
        log::write(log::Level::Info, "USB", "xHCI USB HID boot mouse ready");
        terminal::println("[TEST] xhci_mouse_enumeration: PASS");
    } else if (controller.hid_kind == HidKind::ReportPointer) {
        log::write(
            log::Level::Info, "USB",
            "xHCI USB HID report-protocol pointer ready");
        terminal::println("[TEST] xhci_mouse_enumeration: PASS");
        terminal::println("[TEST] xhci_report_pointer_enumeration: PASS");
    } else {
        log::write(
            log::Level::Info, "USB",
            "xHCI USB Mass Storage read/write block device ready");
        terminal::println("[TEST] xhci_mass_storage_enumeration: PASS");
    }
    return Status::Ok;
}

void fail_hotplug(Controller& controller, Status reason) {
    controller.initialized = false;
    controller.hid_lifecycle = HidLifecycle::Failed;
    controller.companion.hid_lifecycle = HidLifecycle::Failed;
    controller.cleanup_pending = true;
    const Status stopped = quiesce_dma(controller);
    controller.runtime_status = stopped == Status::Ok ? reason : stopped;
    if (controller.keyboard_device != device::INVALID_DEVICE_ID) {
        static_cast<void>(device::set_status(controller.keyboard_device,
                                            device::Status::Failed));
    }
    if (controller.mouse_device != device::INVALID_DEVICE_ID) {
        static_cast<void>(device::set_status(controller.mouse_device,
                                            device::Status::Failed));
    }
    if (controller.mass_storage_device != device::INVALID_DEVICE_ID) {
        static_cast<void>(device::set_status(controller.mass_storage_device,
                                            device::Status::Failed));
    }
    if (controller.companion.child_device != device::INVALID_DEVICE_ID) {
        static_cast<void>(device::set_status(controller.companion.child_device,
                                            device::Status::Failed));
    }
    log::write(log::Level::Error, "XHCI", status_message(controller.runtime_status));
    // Keep all DMA/mapping ownership until explicit initialize/cleanup retry.
}

bool progress_hid_lifecycle(Controller& controller) {
    if (controller.hid_lifecycle == HidLifecycle::Active &&
        controller.port_id != 0U) {
        const size_t offset = OP_PORTS +
            static_cast<size_t>(controller.port_id - 1U) * PORT_STRIDE;
        const uint32_t port = read32(controller.operational, offset);
        if (port == UINT32_MAX) {
            fail_hotplug(controller, Status::MmioUnavailable);
            return false;
        }
        // CSC also catches a disconnect/reconnect completed between polls.
        // The new physical attachment must never inherit the old slot/handle.
        if ((port & PORT_CONNECTED) == 0U || (port & (UINT32_C(1) << 17U)) != 0U) {
            controller.hid_lifecycle = HidLifecycle::DrainInput;
            acknowledge_port_change(controller, controller.port_id);
        }
    }
    if (controller.hid_lifecycle == HidLifecycle::DrainInput) {
        if (!flush_hid_input(controller)) return false;
        if (controller.hid_kind == HidKind::Keyboard) {
            const uint8_t released[8]{};
            static_cast<void>(decode_boot_keyboard_report(
                &controller.keyboard_decoder, released, sizeof(released),
                controller.pending_keys, MAXIMUM_KEYBOARD_EVENTS_PER_REPORT,
                &controller.pending_key_count));
        } else if (controller.hid_kind == HidKind::Mouse) {
            const uint8_t released[3]{};
            mouse::Sample sample{};
            if (decode_boot_mouse_report(
                    &controller.mouse_decoder, released, sizeof(released),
                    &sample)) {
                controller.pending_mouse = sample;
                controller.pending_mouse_valid = true;
            }
        }
        else if (controller.hid_kind == HidKind::ReportPointer) {
            const uint8_t changed = controller.pointer_decoder.previous_buttons;
            controller.pointer_decoder.previous_buttons = 0U;
            controller.pending_pointer = {
                false, 0, 0, 0, 0, 0, 0, 0,
                0U, changed
            };
            controller.pending_pointer_valid = changed != 0U;
        }
        controller.hid_lifecycle = HidLifecycle::ReleaseInput;
    }
    if (controller.hid_lifecycle == HidLifecycle::ReleaseInput) {
        if (!flush_hid_input(controller)) return false;
        if (!disable_slot(controller, controller.slot_id)) {
            fail_hotplug(controller, Status::CommandFailed);
            return false;
        }
        // Only acknowledged Disable Slot permits reuse of endpoint DMA.
        controller.report_queued = false;
        controller.report_trb = 0U;
        const Status removed = remove_hid_devices(&controller);
        if (removed != Status::Ok) {
            fail_hotplug(controller, removed);
            return false;
        }
        controller.hid_kind = HidKind::None;
        controller.mass_storage_present = false;
        controller.mass_storage_block_ready = false;
        controller.mass_storage_block = {};
        controller.mass_storage_block_size = 0U;
        controller.mass_storage_block_count = 0U;
        controller.mass_storage_tag = 0U;
        controller.bulk_in_dci = 0U;
        controller.bulk_out_dci = 0U;
        controller.port_id = 0U;
        controller.port_speed = 0U;
        controller.pending_mouse_valid = false;
        controller.pending_pointer = {};
        controller.pending_pointer_valid = false;
        controller.report_interface = {};
        controller.pointer_layout = {};
        controller.pointer_decoder = {};
        controller.hid_lifecycle = HidLifecycle::WaitingForDevice;
        controller.runtime_status = Status::NoDevice;
        log::write(log::Level::Info, "USB", "HID device disconnected; slot retired");
        return false;
    }
    if (controller.hid_lifecycle == HidLifecycle::WaitingForDevice) {
        if (first_unclaimed_connected_port(controller) == 0U) return false;
        // Command/event rings stay live. Only disabled-slot endpoint/context
        // pages are reused; no DMA allocation or global controller reset.
        reset_primary_endpoint_memory(controller);
        const Status attached = attach_hid_device(controller);
        if (attached == Status::NoDevice) return false;
        if (attached != Status::Ok) {
            fail_hotplug(controller, attached);
            return false;
        }
    }
    return controller.hid_lifecycle == HidLifecycle::Active;
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
    if (g_controller.cleanup_pending) {
        const Status cleanup = release_resources(&g_controller);
        if (cleanup != Status::Ok) return cleanup;
    }
    g_controller = {};
    g_controller.keyboard_device = device::INVALID_DEVICE_ID;
    g_controller.mouse_device = device::INVALID_DEVICE_ID;
    g_controller.mass_storage_device = device::INVALID_DEVICE_ID;
    g_controller.companion.child_device = device::INVALID_DEVICE_ID;
    g_controller.companion.hid_lifecycle = HidLifecycle::WaitingForDevice;
    g_controller.pci_device = pci_device;
    g_controller.parent_device = parent_device;
    g_controller.owner_driver = owner_driver;
    bool is_io = false;
    const uint64_t bar = pci::bar_address(pci_device, 0U, &is_io);
    if (bar == 0U || is_io || !map_mmio(bar, &g_controller)) {
        return fail_initialization(Status::MmioUnavailable);
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
    if (validate_capability_layout(
            cap_length,
            dboff,
            rtsoff,
            g_controller.maximum_slots,
            g_controller.maximum_ports,
            MMIO_BYTES) != LayoutStatus::Ok) {
        return fail_initialization(Status::UnsupportedController);
    }
    g_controller.operational = g_controller.registers + cap_length;
    g_controller.runtime = g_controller.registers + rtsoff;
    g_controller.doorbells = reinterpret_cast<volatile uint32_t*>(
        g_controller.registers + dboff);
    if (!take_ownership(g_controller, hccparams1)) {
        return fail_initialization(Status::BiosHandoffTimeout);
    }
    if (!reset_controller(g_controller)) {
        return fail_initialization(Status::ControllerResetTimeout);
    }
    if (!allocate_controller_memory(g_controller, hcsparams2)) {
        return fail_initialization(Status::DmaAllocationFailed);
    }
    pci::enable_bus_mastering(pci_device);
    g_controller.bus_master_enabled = true;
    if (!configure_controller(g_controller)) {
        return fail_initialization(Status::ControllerStartTimeout);
    }
    const Status attached = attach_hid_device(g_controller);
    if (attached == Status::NoDevice) {
        // A running empty controller remains owned and polled so its first
        // HID device can arrive after boot. No slot or child handle exists yet.
        g_controller.hid_lifecycle = HidLifecycle::WaitingForDevice;
        g_controller.runtime_status = Status::NoDevice;
        g_controller.initialized = true;
        g_controller.companion.hid_lifecycle =
            HidLifecycle::WaitingForDevice;
        log::write(log::Level::Info, "XHCI", "controller ready; waiting for USB HID device");
        return Status::Ok;
    }
    if (attached != Status::Ok) return fail_initialization(attached);
    g_controller.initialized = true;
    static_cast<void>(progress_companion_lifecycle(g_controller));
    return g_controller.initialized ? Status::Ok : g_controller.runtime_status;
}

size_t poll(size_t budget) {
    if (!g_controller.initialized || budget == 0U) return 0U;
    if (!progress_hid_lifecycle(g_controller) &&
        g_controller.hid_lifecycle != HidLifecycle::WaitingForDevice &&
        g_controller.hid_lifecycle != HidLifecycle::ReleaseInput) {
        return 0U;
    }
    static_cast<void>(progress_companion_lifecycle(g_controller));
    if (!g_controller.initialized) return 0U;

    const bool input_pending =
        (g_controller.hid_kind == HidKind::Keyboard &&
         g_controller.pending_key_count != 0U) ||
        (g_controller.hid_kind == HidKind::Mouse &&
         g_controller.pending_mouse_valid) ||
        (g_controller.hid_kind == HidKind::ReportPointer &&
         g_controller.pending_pointer_valid);
    if (g_controller.hid_lifecycle == HidLifecycle::Active && input_pending) {
        if (!flush_hid_input(g_controller)) return 0U;
        static_cast<void>(queue_hid_report(g_controller));
    }

    auto& companion = g_controller.companion;
    const bool companion_pending =
        (companion.hid_kind == HidKind::Keyboard &&
         companion.pending_key_count != 0U) ||
        (companion.hid_kind == HidKind::Mouse &&
         companion.pending_mouse_valid) ||
        (companion.hid_kind == HidKind::ReportPointer &&
         companion.pending_pointer_valid);
    if (companion.hid_lifecycle == HidLifecycle::Active &&
        companion_pending) {
        if (!flush_companion_input(companion)) return 0U;
        static_cast<void>(queue_companion_report(g_controller));
    }

    size_t processed = 0U;
    while (processed < budget) {
        Trb event{};
        if (!next_poll_event(g_controller, &event)) break;
        ++processed;

        if (trb_type(event) == TRB_PORT_STATUS_CHANGE) {
            const uint8_t port =
                static_cast<uint8_t>(event.parameter >> 24U);
            if (port == companion.ignored_port_id) {
                companion.ignored_port_id = 0U;
            }
            if (port == g_controller.port_id) {
                static_cast<void>(
                    progress_hid_lifecycle(g_controller));
            } else if (port == companion.port_id) {
                static_cast<void>(
                    progress_companion_lifecycle(g_controller));
            } else {
                if (port != 0U && port <= g_controller.maximum_ports &&
                    g_controller.deferred_hub_ports[port]) {
                    // Observe CSC before acknowledging it, or a replacement
                    // device on this port could remain permanently deferred.
                    static_cast<void>(connected_probe_candidate(g_controller, port));
                }
                acknowledge_port_change(g_controller, port);
                if (g_controller.hid_lifecycle ==
                    HidLifecycle::WaitingForDevice) {
                    static_cast<void>(
                        progress_hid_lifecycle(g_controller));
                }
                if (companion.hid_lifecycle ==
                    HidLifecycle::WaitingForDevice) {
                    static_cast<void>(
                        progress_companion_lifecycle(g_controller));
                }
            }
        }

        // Either lifecycle can contain a failed slot retirement by halting
        // the controller. Do not consume more events or requeue input then.
        if (!g_controller.initialized) return processed;
        if (trb_type(event) != TRB_TRANSFER_EVENT) continue;
        const uint8_t event_slot =
            static_cast<uint8_t>(event.control >> 24U);
        const uint8_t event_dci = static_cast<uint8_t>(
            (event.control >> 16U) & 0x1FU);

        if (g_controller.hid_lifecycle == HidLifecycle::Active &&
            event_slot == g_controller.slot_id &&
            event_dci == g_controller.interrupt_dci) {
            if (g_controller.hid_kind == HidKind::Keyboard) {
                handle_keyboard_report(g_controller, event);
            } else if (g_controller.hid_kind == HidKind::Mouse) {
                handle_mouse_report(g_controller, event);
            } else if (g_controller.hid_kind == HidKind::ReportPointer) {
                handle_pointer_report(g_controller, event);
            }
            continue;
        }

        if (companion.hid_lifecycle == HidLifecycle::Active &&
            event_slot == companion.slot_id &&
            event_dci == companion.interrupt_dci) {
            if (companion.hid_kind == HidKind::Keyboard) {
                handle_companion_keyboard_report(
                    g_controller, event);
            } else if (companion.hid_kind == HidKind::Mouse) {
                handle_companion_mouse_report(
                    g_controller, event);
            } else if (companion.hid_kind == HidKind::ReportPointer) {
                handle_companion_pointer_report(
                    g_controller, event);
            }
        }
    }

    if (companion.hid_lifecycle ==
        HidLifecycle::WaitingForDevice) {
        static_cast<void>(
            progress_companion_lifecycle(g_controller));
    }
    mark_multi_hid_if_ready(g_controller);
    return processed;
}

bool initialized() { return g_controller.initialized; }
bool keyboard_ready() {
    if (!g_controller.initialized) return false;
    const bool primary =
        g_controller.hid_lifecycle == HidLifecycle::Active &&
        g_controller.hid_kind == HidKind::Keyboard &&
        g_controller.keyboard_device != device::INVALID_DEVICE_ID;
    const bool companion =
        g_controller.companion.hid_lifecycle ==
            HidLifecycle::Active &&
        g_controller.companion.hid_kind == HidKind::Keyboard &&
        g_controller.companion.child_device !=
            device::INVALID_DEVICE_ID;
    return primary || companion;
}
bool mouse_ready() {
    if (!g_controller.initialized) return false;
    const bool primary =
        g_controller.hid_lifecycle == HidLifecycle::Active &&
        (g_controller.hid_kind == HidKind::Mouse ||
         g_controller.hid_kind == HidKind::ReportPointer) &&
        g_controller.mouse_device != device::INVALID_DEVICE_ID;
    const bool companion =
        g_controller.companion.hid_lifecycle ==
            HidLifecycle::Active &&
        (g_controller.companion.hid_kind == HidKind::Mouse ||
         g_controller.companion.hid_kind == HidKind::ReportPointer) &&
        g_controller.companion.child_device !=
            device::INVALID_DEVICE_ID;
    return primary || companion;
}
const storage::block::Device* mass_storage_block_device() {
    return g_controller.initialized &&
        g_controller.hid_lifecycle == HidLifecycle::Active &&
        g_controller.mass_storage_present &&
        g_controller.mass_storage_block_ready &&
        g_controller.mass_storage_device != device::INVALID_DEVICE_ID
        ? &g_controller.mass_storage_block
        : nullptr;
}
Status runtime_status() { return g_controller.runtime_status; }
uint64_t reports_received() {
    return g_controller.reports + g_controller.companion.reports;
}

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
        case Status::HidKeyboardNotFound: return "USB HID boot keyboard/mouse not found";
        case Status::DeviceRegistrationFailed: return "USB device registration failed";
        case Status::ControllerHaltTimeout:
            return "xHCI halt timeout; DMA resources quarantined";
        case Status::ResourceReleaseFailed:
            return "xHCI resource release failed; ownership retained";
    }
    return "unknown xHCI status";
}

} // namespace drivers::usb::xhci
