#include "virtio_net.hpp"

#include "../drivers/pci.hpp"
#include "../drivers/pci_bar.hpp"
#include "../drivers/pci_msix.hpp"
#include "../arch/x86_64/apic.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/virtual_memory.hpp"
#include "../storage/dma.hpp"

namespace net::virtio_net {
namespace {

constexpr uint16_t kVirtioVendor = UINT16_C(0x1AF4);
constexpr uint16_t kVirtioNetTransitionalDevice = UINT16_C(0x1000);
constexpr uint16_t kVirtioNetModernDevice = UINT16_C(0x1041);
constexpr uint8_t kVendorCapabilityId = UINT8_C(0x09);
constexpr uint8_t kCommonConfigType = 1U;
constexpr uint8_t kNotifyConfigType = 2U;
constexpr uint8_t kDeviceConfigType = 4U;
constexpr uint8_t kCapabilityIterations = 48U;
constexpr uint8_t kMaxBarIndex = 5U;
constexpr size_t kQueueCapacity = 8U;
constexpr size_t kVirtioNetHeaderSize = 12U;
constexpr size_t kDmaBufferSize = memory::virtual_memory::PAGE_SIZE;
constexpr uint16_t kVirtqDescWrite = UINT16_C(2);
constexpr uint16_t kAvailNoInterrupt = UINT16_C(1);
constexpr uint16_t kNoMsixVector = UINT16_C(0xFFFF);
constexpr uint32_t kMacFeature = UINT32_C(1) << 5U;
constexpr uint32_t kVersion1FeatureHigh = UINT32_C(1);
constexpr uint8_t kStatusAcknowledge = UINT8_C(1);
constexpr uint8_t kStatusDriver = UINT8_C(2);
constexpr uint8_t kStatusDriverOk = UINT8_C(4);
constexpr uint8_t kStatusFeaturesOk = UINT8_C(8);
constexpr uint8_t kStatusFailed = UINT8_C(128);
constexpr uint16_t kCommandMemory = UINT16_C(2);
constexpr uint16_t kCommandBusMaster = UINT16_C(4);
constexpr size_t kResetPollLimit = 100000U;
constexpr uintptr_t kCommonVirtualBase = UINT64_C(0xFFFFB20000000000);
constexpr uintptr_t kNotifyVirtualBase = UINT64_C(0xFFFFB20000010000);
constexpr uintptr_t kDeviceVirtualBase = UINT64_C(0xFFFFB20000020000);
constexpr uintptr_t kMsixTableVirtualBase = UINT64_C(0xFFFFB20000100000);
constexpr uintptr_t kMsixPendingVirtualBase = UINT64_C(0xFFFFB20000200000);
constexpr size_t kMaximumMsixBarBytes = 256U * 1024U;

struct VirtioCapability {
    bool present;
    uint8_t bar;
    uint32_t offset;
    uint32_t length;
    uint32_t notify_multiplier;
};

struct MappedRegion {
    uintptr_t virtual_base;
    volatile uint8_t* base;
    size_t mapped_pages;
    size_t length;
};

struct [[gnu::packed]] Descriptor {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
};

struct [[gnu::packed]] UsedElement {
    uint32_t id;
    uint32_t length;
};

static_assert(sizeof(Descriptor) == 16U, "VirtIO split descriptor ABI mismatch");
static_assert(sizeof(UsedElement) == 8U, "VirtIO split used element ABI mismatch");

struct Queue {
    uint16_t index;
    uint16_t size;
    storage::dma::Page descriptor_page;
    storage::dma::Page available_page;
    storage::dma::Page used_page;
    storage::dma::Page buffers[kQueueCapacity];
    Descriptor* descriptors;
    volatile uint16_t* available;
    volatile uint16_t* used_header;
    volatile UsedElement* used_elements;
    volatile uint16_t* notify;
    uint16_t available_index;
    uint16_t last_used_index;
    bool buffer_free[kQueueCapacity];
    bool configured;
};

pci::Device g_device{};
bool g_detected = false;
bool g_initialized = false;
Status g_status = Status::NotInitialized;
MappedRegion g_common{};
MappedRegion g_notify{};
MappedRegion g_device_config{};
Queue g_receive_queue{};
Queue g_transmit_queue{};
NetworkInterface g_interface{};
MacAddress g_mac{};
uint16_t g_original_command = 0U;
bool g_command_owned = false;
bool g_cleanup_blocked = false;
MappedRegion g_msix_table{};
MappedRegion g_msix_pending{};
pci::msix::Route g_msix_route{};
InterruptStatus g_interrupt_status = InterruptStatus::NotInitialized;
uint64_t g_interrupt_count = 0U;
bool g_interrupt_pending = false;

uint8_t pci_read8(const pci::Device& device, uint8_t offset) {
    const uint8_t aligned = static_cast<uint8_t>(offset & UINT8_C(0xFC));
    const uint32_t value = pci::read32(device, aligned);
    const unsigned shift = static_cast<unsigned>((offset & UINT8_C(3)) * 8U);
    return static_cast<uint8_t>((value >> shift) & UINT32_C(0xFF));
}

uint16_t mmio_read16(const MappedRegion& region, size_t offset) {
    if (region.base == nullptr || offset + sizeof(uint16_t) > region.length) return 0U;
    return *reinterpret_cast<volatile uint16_t*>(region.base + offset);
}

uint32_t mmio_read32(const MappedRegion& region, size_t offset) {
    if (region.base == nullptr || offset + sizeof(uint32_t) > region.length) return 0U;
    return *reinterpret_cast<volatile uint32_t*>(region.base + offset);
}

uint8_t mmio_read8(const MappedRegion& region, size_t offset) {
    if (region.base == nullptr || offset >= region.length) return 0U;
    return *(region.base + offset);
}

void mmio_write8(const MappedRegion& region, size_t offset, uint8_t value) {
    if (region.base == nullptr || offset >= region.length) return;
    *(region.base + offset) = value;
}

void mmio_write16(const MappedRegion& region, size_t offset, uint16_t value) {
    if (region.base == nullptr || offset + sizeof(uint16_t) > region.length) return;
    *reinterpret_cast<volatile uint16_t*>(region.base + offset) = value;
}

void mmio_write32(const MappedRegion& region, size_t offset, uint32_t value) {
    if (region.base == nullptr || offset + sizeof(uint32_t) > region.length) return;
    *reinterpret_cast<volatile uint32_t*>(region.base + offset) = value;
}

void memory_barrier() {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    if (bytes == nullptr) return;
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

bool capability_valid(const VirtioCapability& capability) {
    return capability.present && capability.bar <= kMaxBarIndex &&
        capability.length != 0U &&
        capability.length <= memory::virtual_memory::PAGE_SIZE &&
        capability.offset <= UINT32_MAX - capability.length;
}

bool scan_capabilities(
    const pci::Device& device,
    VirtioCapability* common,
    VirtioCapability* notify,
    VirtioCapability* device_config) {
    if (common == nullptr || notify == nullptr || device_config == nullptr) return false;
    *common = {};
    *notify = {};
    *device_config = {};

    uint8_t pointer = static_cast<uint8_t>(pci_read8(device, UINT8_C(0x34)) & UINT8_C(0xFC));
    for (uint8_t iteration = 0U;
         pointer >= UINT8_C(0x40) && iteration < kCapabilityIterations;
         ++iteration) {
        const uint32_t header = pci::read32(device, pointer);
        const uint8_t capability_id = static_cast<uint8_t>(header & UINT32_C(0xFF));
        const uint8_t next = static_cast<uint8_t>((header >> 8U) & UINT32_C(0xFC));
        if (capability_id == kVendorCapabilityId) {
            const uint8_t length = static_cast<uint8_t>((header >> 16U) & UINT32_C(0xFF));
            const uint8_t type = static_cast<uint8_t>((header >> 24U) & UINT32_C(0xFF));
            if (length >= 16U && pointer <= UINT8_C(0xF0)) {
                const uint32_t second = pci::read32(
                    device, static_cast<uint8_t>(pointer + 4U));
                VirtioCapability candidate{};
                candidate.present = true;
                candidate.bar = static_cast<uint8_t>(second & UINT32_C(0xFF));
                candidate.offset = pci::read32(
                    device, static_cast<uint8_t>(pointer + 8U));
                candidate.length = pci::read32(
                    device, static_cast<uint8_t>(pointer + 12U));
                if (type == kNotifyConfigType && length >= 20U &&
                    pointer <= UINT8_C(0xEC)) {
                    candidate.notify_multiplier = pci::read32(
                        device, static_cast<uint8_t>(pointer + 16U));
                }
                if (capability_valid(candidate)) {
                    if (type == kCommonConfigType && !common->present) {
                        *common = candidate;
                    } else if (type == kNotifyConfigType && !notify->present) {
                        *notify = candidate;
                    } else if (type == kDeviceConfigType && !device_config->present) {
                        *device_config = candidate;
                    }
                }
            }
        }
        if (next == 0U || next == pointer) break;
        pointer = next;
    }
    return common->present && notify->present;
}

bool unmap_capability(MappedRegion* region) {
    if (region == nullptr) return false;
    if (region->mapped_pages == 0U) {
        *region = {};
        return true;
    }
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) return false;
    // Retain the remaining mapping on failure so retry cannot overwrite it.
    while (region->mapped_pages != 0U) {
        const uintptr_t target = region->virtual_base +
            (region->mapped_pages - 1U) * memory::virtual_memory::PAGE_SIZE;
        const auto status = memory::virtual_memory::unmap_page(address_space, target);
        if (status != memory::virtual_memory::Status::Ok &&
            status != memory::virtual_memory::Status::NotMapped) return false;
        --region->mapped_pages;
    }
    *region = {};
    return true;
}

bool map_mmio(
    uint64_t physical,
    size_t length,
    uintptr_t virtual_base,
    MappedRegion* output) {
    if (output == nullptr || output->base != nullptr ||
        output->mapped_pages != 0U || physical == 0U || length == 0U ||
        length > kMaximumMsixBarBytes) return false;

    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) return false;
    constexpr uint64_t page_size = memory::virtual_memory::PAGE_SIZE;
    constexpr uint64_t page_mask = page_size - 1U;
    const uint64_t aligned = physical & ~page_mask;
    const size_t prefix = static_cast<size_t>(physical & page_mask);
    const size_t extent = prefix + length;
    const size_t pages = static_cast<size_t>((extent + page_mask) / page_size);
    if (pages == 0U || (virtual_base & page_mask) != 0U ||
        virtual_base > UINTPTR_MAX - pages * page_size ||
        aligned > UINT64_MAX - pages * page_size) return false;

    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    size_t mapped = 0U;
    output->virtual_base = virtual_base;
    for (; mapped < pages; ++mapped) {
        memory::virtual_memory::Mapping existing{};
        const uintptr_t target = virtual_base + mapped * page_size;
        if (memory::virtual_memory::query_page(
                address_space,
                target,
                &existing) != memory::virtual_memory::Status::NotMapped ||
            memory::virtual_memory::map_page(
                address_space,
                target,
                aligned + mapped * page_size,
                flags) != memory::virtual_memory::Status::Ok) {
            break;
        }
        ++output->mapped_pages;
    }
    if (mapped != pages) {
        static_cast<void>(unmap_capability(output));
        return false;
    }
    output->base = reinterpret_cast<volatile uint8_t*>(virtual_base + prefix);
    output->length = length;
    return true;
}

bool map_capability(
    const pci::Device& device,
    const VirtioCapability& capability,
    uintptr_t virtual_base,
    MappedRegion* output) {
    if (!capability_valid(capability)) return false;
    bool io_space = false;
    const uint64_t bar = pci::bar_address(device, capability.bar, &io_space);
    if (bar == 0U || io_space || bar > UINT64_MAX - capability.offset) return false;
    return map_mmio(bar + capability.offset, capability.length, virtual_base, output);
}

void queue_interrupt(arch::x86_64::interrupts::InterruptFrame&) {
    // The IDT dispatcher owns LAPIC EOI. Packet processing stays outside IRQ;
    // one shared vector notifies the existing network pump about both queues.
    __atomic_add_fetch(&g_interrupt_count, UINT64_C(1), __ATOMIC_RELAXED);
    __atomic_store_n(&g_interrupt_pending, true, __ATOMIC_RELEASE);
}

bool interrupt_mapping_fallback(InterruptStatus reason) {
    const bool table = unmap_capability(&g_msix_table);
    const bool pending = unmap_capability(&g_msix_pending);
    g_interrupt_status = table && pending ? reason : InterruptStatus::CleanupFailed;
    return table && pending;
}

bool prepare_queue_interrupts() {
    if (!arch::x86_64::apic::local_enabled()) {
        g_interrupt_status = InterruptStatus::LocalApicUnavailable;
        return true;
    }
    pci::Capability capability{};
    if (!pci::find_capability(g_device, pci::CapabilityId::MsiX, &capability)) {
        g_interrupt_status = InterruptStatus::CapabilityUnavailable;
        return true;
    }
    pci::MsiXInfo info{};
    if (!pci::read_msix_info(g_device, &info)) {
        g_interrupt_status = InterruptStatus::CapabilityMalformed;
        return true;
    }
    if (info.enabled) {
        // Do not steal a route programmed by another owner/firmware.
        g_interrupt_status = InterruptStatus::RouteUnavailable;
        return true;
    }

    // Only the boot-serialized, reset device may be sized. Never probe an
    // active BAR, and restore decode before touching mapped MMIO again.
    const uint16_t saved_command = pci::read16(g_device, 0x04U);
    pci::write16(g_device, 0x04U, static_cast<uint16_t>(
        saved_command & ~pci::bar::COMMAND_DECODE_MASK));
    pci::bar::Info table{};
    pci::bar::Info pending{};
    bool valid = (pci::read16(g_device, 0x04U) & pci::bar::COMMAND_DECODE_MASK) == 0U;
    if (valid) {
        valid = pci::bar::probe_disabled(g_device, info.table_bar, &table) ==
            pci::bar::Status::Ok;
        if (valid && info.table_bar == info.pending_bit_array_bar) {
            pending = table;
        } else if (valid) {
            valid = pci::bar::probe_disabled(
                g_device, info.pending_bit_array_bar, &pending) == pci::bar::Status::Ok;
        }
    }
    pci::write16(g_device, 0x04U, saved_command);
    if (pci::read16(g_device, 0x04U) != saved_command) {
        g_interrupt_status = InterruptStatus::CleanupFailed;
        return false;
    }
    if (!valid || table.kind == pci::bar::Kind::Io ||
        pending.kind == pci::bar::Kind::Io || table.size == 0U ||
        pending.size == 0U || table.size > kMaximumMsixBarBytes ||
        pending.size > kMaximumMsixBarBytes) {
        g_interrupt_status = InterruptStatus::BarUnavailable;
        return true;
    }
    if (!map_mmio(table.physical_address, static_cast<size_t>(table.size),
            kMsixTableVirtualBase, &g_msix_table)) {
        return interrupt_mapping_fallback(InterruptStatus::MappingUnavailable);
    }
    if (info.table_bar != info.pending_bit_array_bar &&
        !map_mmio(pending.physical_address, static_cast<size_t>(pending.size),
            kMsixPendingVirtualBase, &g_msix_pending)) {
        return interrupt_mapping_fallback(InterruptStatus::MappingUnavailable);
    }
    const pci::msix::MmioRegion table_region{
        table.index, table.physical_address, g_msix_table.base, g_msix_table.length};
    const pci::msix::MmioRegion pending_region = info.table_bar == info.pending_bit_array_bar
        ? table_region : pci::msix::MmioRegion{pending.index, pending.physical_address,
            g_msix_pending.base, g_msix_pending.length};
    if (pci::msix::enable_single(g_device, 0U, table_region, pending_region,
            queue_interrupt, &g_msix_route) != pci::msix::Status::Ok) {
        return interrupt_mapping_fallback(InterruptStatus::RouteUnavailable);
    }
    // Reset leaves all events unmapped. Use entry zero for RX and TX only.
    mmio_write16(g_common, 16U, kNoMsixVector);
    g_interrupt_status = InterruptStatus::MsiXEnabled;
    return true;
}

bool release_queue(Queue* queue) {
    if (queue == nullptr) return false;
    bool complete = true;
    for (size_t index = 0U; index < kQueueCapacity; ++index) {
        if (queue->buffers[index].allocated) {
            if (storage::dma::release_page(&queue->buffers[index]) !=
                storage::dma::Status::Ok) complete = false;
        }
    }
    if (queue->descriptor_page.allocated) {
        if (storage::dma::release_page(&queue->descriptor_page) !=
            storage::dma::Status::Ok) complete = false;
    }
    if (queue->available_page.allocated) {
        if (storage::dma::release_page(&queue->available_page) !=
            storage::dma::Status::Ok) complete = false;
    }
    if (queue->used_page.allocated) {
        if (storage::dma::release_page(&queue->used_page) !=
            storage::dma::Status::Ok) complete = false;
    }
    if (complete) *queue = {};
    return complete;
}

uint16_t choose_queue_size(uint16_t maximum) {
    uint16_t size = static_cast<uint16_t>(kQueueCapacity);
    while (size > maximum && size > 1U) size = static_cast<uint16_t>(size >> 1U);
    return size <= maximum ? size : 0U;
}

bool allocate_queue_storage(Queue* queue, uint16_t size) {
    if (queue == nullptr || size == 0U || size > kQueueCapacity) return false;
    if (storage::dma::allocate_page(true, &queue->descriptor_page) !=
            storage::dma::Status::Ok ||
        storage::dma::allocate_page(true, &queue->available_page) !=
            storage::dma::Status::Ok ||
        storage::dma::allocate_page(true, &queue->used_page) !=
            storage::dma::Status::Ok) {
        release_queue(queue);
        return false;
    }
    clear_bytes(
        queue->descriptor_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    clear_bytes(
        queue->available_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    clear_bytes(
        queue->used_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    for (uint16_t index = 0U; index < size; ++index) {
        if (storage::dma::allocate_page(true, &queue->buffers[index]) !=
            storage::dma::Status::Ok) {
            release_queue(queue);
            return false;
        }
        clear_bytes(queue->buffers[index].virtual_address, kDmaBufferSize);
        queue->buffer_free[index] = true;
    }
    queue->size = size;
    queue->descriptors = static_cast<Descriptor*>(
        queue->descriptor_page.virtual_address);
    queue->available = static_cast<volatile uint16_t*>(
        queue->available_page.virtual_address);
    queue->used_header = static_cast<volatile uint16_t*>(
        queue->used_page.virtual_address);
    queue->used_elements = reinterpret_cast<volatile UsedElement*>(
        static_cast<uint8_t*>(queue->used_page.virtual_address) + 4U);
    queue->available[0] = kAvailNoInterrupt;
    queue->available[1] = 0U;
    queue->used_header[0] = 0U;
    queue->used_header[1] = 0U;
    return true;
}

bool write_queue_address(size_t low_offset, uint64_t address) {
    if (g_common.length < low_offset + 8U) return false;
    mmio_write32(g_common, low_offset, static_cast<uint32_t>(address));
    mmio_write32(
        g_common,
        low_offset + 4U,
        static_cast<uint32_t>(address >> 32U));
    return true;
}

bool configure_queue(
    uint16_t index,
    const VirtioCapability& notify_capability,
    Queue* queue) {
    if (queue == nullptr || g_common.length < 56U || g_notify.base == nullptr ||
        notify_capability.notify_multiplier == 0U) {
        return false;
    }
    mmio_write16(g_common, 22U, index);
    const uint16_t maximum = mmio_read16(g_common, 24U);
    const uint16_t size = choose_queue_size(maximum);
    if (size == 0U || mmio_read16(g_common, 28U) != 0U) return false;
    if (!allocate_queue_storage(queue, size)) return false;
    queue->index = index;

    mmio_write16(g_common, 24U, size);
    const uint16_t queue_vector = g_msix_route.active ? 0U : kNoMsixVector;
    mmio_write16(g_common, 26U, queue_vector);
    if (g_msix_route.active) {
        if (mmio_read16(g_common, 26U) != queue_vector) {
            g_interrupt_status = InterruptStatus::QueueRejected;
            return false;
        }
        queue->available[0] = 0U;
    }
    if (!write_queue_address(32U, queue->descriptor_page.physical_address) ||
        !write_queue_address(40U, queue->available_page.physical_address) ||
        !write_queue_address(48U, queue->used_page.physical_address)) {
        // Addresses have been exposed. The caller must reset before freeing.
        return false;
    }

    const uint16_t notify_offset = mmio_read16(g_common, 30U);
    const uint64_t byte_offset =
        static_cast<uint64_t>(notify_offset) * notify_capability.notify_multiplier;
    if (g_notify.length < sizeof(uint16_t) ||
        byte_offset > g_notify.length - sizeof(uint16_t)) {
        return false;
    }
    queue->notify = reinterpret_cast<volatile uint16_t*>(
        g_notify.base + static_cast<size_t>(byte_offset));
    memory_barrier();
    mmio_write16(g_common, 28U, 1U);
    if (mmio_read16(g_common, 28U) != 1U) {
        return false;
    }
    queue->configured = true;
    return true;
}

void notify_queue(Queue& queue) {
    memory_barrier();
    if (queue.notify != nullptr) *queue.notify = queue.index;
}

void make_available(Queue& queue, uint16_t descriptor) {
    const uint16_t slot = static_cast<uint16_t>(
        queue.available_index % queue.size);
    queue.available[2U + slot] = descriptor;
    memory_barrier();
    ++queue.available_index;
    queue.available[1] = queue.available_index;
    memory_barrier();
}

void prepare_receive_queue() {
    Queue& queue = g_receive_queue;
    for (uint16_t index = 0U; index < queue.size; ++index) {
        queue.descriptors[index].address = queue.buffers[index].physical_address;
        queue.descriptors[index].length = static_cast<uint32_t>(kDmaBufferSize);
        queue.descriptors[index].flags = kVirtqDescWrite;
        queue.descriptors[index].next = 0U;
        queue.buffer_free[index] = false;
        make_available(queue, index);
    }
    notify_queue(queue);
}

void reclaim_transmit() {
    Queue& queue = g_transmit_queue;
    const uint16_t used_index = queue.used_header[1];
    memory_barrier();
    while (queue.last_used_index != used_index) {
        const uint16_t slot = static_cast<uint16_t>(
            queue.last_used_index % queue.size);
        const uint32_t id = queue.used_elements[slot].id;
        if (id < queue.size) queue.buffer_free[id] = true;
        ++queue.last_used_index;
    }
}

bool read_mac_from_device(MacAddress* output) {
    if (output == nullptr || g_device_config.base == nullptr ||
        g_device_config.length < MAC_ADDRESS_LENGTH || g_common.length < 22U) {
        return false;
    }
    for (size_t attempt = 0U; attempt < 8U; ++attempt) {
        const uint8_t generation_before = mmio_read8(g_common, 21U);
        MacAddress candidate{};
        for (size_t index = 0U; index < MAC_ADDRESS_LENGTH; ++index) {
            candidate.bytes[index] = mmio_read8(g_device_config, index);
        }
        memory_barrier();
        const uint8_t generation_after = mmio_read8(g_common, 21U);
        if (generation_before == generation_after && !mac_is_zero(candidate) &&
            !mac_is_multicast(candidate)) {
            *output = candidate;
            return true;
        }
    }
    return false;
}

MacAddress fallback_mac(const pci::Device& device) {
    MacAddress address{{
        UINT8_C(0x02), UINT8_C(0x4B), UINT8_C(0x55),
        device.address.bus,
        device.address.slot,
        device.address.function}};
    return address;
}

Status transmit_frame(void*, const uint8_t* frame, size_t frame_length) {
    if (!g_initialized) return Status::NotInitialized;
    if (frame == nullptr || frame_length < ETHERNET_HEADER_SIZE) {
        return Status::InvalidArgument;
    }
    if (frame_length > ETHERNET_MAX_FRAME_SIZE ||
        frame_length + kVirtioNetHeaderSize > kDmaBufferSize) {
        return Status::FrameTooLarge;
    }

    reclaim_transmit();
    Queue& queue = g_transmit_queue;
    uint16_t descriptor = queue.size;
    for (uint16_t index = 0U; index < queue.size; ++index) {
        if (queue.buffer_free[index]) {
            descriptor = index;
            break;
        }
    }
    if (descriptor >= queue.size) return Status::WouldBlock;

    auto* buffer = static_cast<uint8_t*>(
        queue.buffers[descriptor].virtual_address);
    clear_bytes(buffer, kVirtioNetHeaderSize);
    for (size_t index = 0U; index < frame_length; ++index) {
        buffer[kVirtioNetHeaderSize + index] = frame[index];
    }
    queue.descriptors[descriptor].address =
        queue.buffers[descriptor].physical_address;
    queue.descriptors[descriptor].length = static_cast<uint32_t>(
        kVirtioNetHeaderSize + frame_length);
    queue.descriptors[descriptor].flags = 0U;
    queue.descriptors[descriptor].next = 0U;
    queue.buffer_free[descriptor] = false;
    make_available(queue, descriptor);
    notify_queue(queue);
    return Status::Ok;
}

Status receive_frame(
    void*,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    if (!g_initialized) return Status::NotInitialized;
    if (output == nullptr || out_length == nullptr) {
        return Status::InvalidArgument;
    }

    Queue& queue = g_receive_queue;
    if (__atomic_exchange_n(&g_interrupt_pending, false, __ATOMIC_ACQUIRE)) {
        reclaim_transmit();
    }
    const uint16_t used_index = queue.used_header[1];
    memory_barrier();
    if (queue.last_used_index == used_index) return Status::WouldBlock;

    const uint16_t slot = static_cast<uint16_t>(
        queue.last_used_index % queue.size);
    const uint32_t id = queue.used_elements[slot].id;
    const uint32_t length = queue.used_elements[slot].length;
    ++queue.last_used_index;
    if (id >= queue.size) return Status::DeviceFault;

    Status result = Status::Ok;
    if (length < kVirtioNetHeaderSize || length > kDmaBufferSize) {
        result = Status::DeviceFault;
    } else {
        const size_t frame_length = static_cast<size_t>(length) -
            kVirtioNetHeaderSize;
        if (frame_length < ETHERNET_HEADER_SIZE ||
            frame_length > ETHERNET_MAX_FRAME_SIZE) {
            result = Status::DeviceFault;
        } else if (frame_length > output_capacity) {
            result = Status::FrameTooLarge;
        } else {
            const auto* buffer = static_cast<const uint8_t*>(
                queue.buffers[id].virtual_address);
            for (size_t index = 0U; index < frame_length; ++index) {
                output[index] = buffer[kVirtioNetHeaderSize + index];
            }
            *out_length = frame_length;
        }
    }

    make_available(queue, static_cast<uint16_t>(id));
    notify_queue(queue);
    return result;
}

net::Status interface_transmit_callback(
    void* context,
    const uint8_t* frame,
    size_t frame_length) {
    const Status status = transmit_frame(context, frame, frame_length);
    switch (status) {
        case Status::Ok: return net::Status::Ok;
        case Status::NotInitialized: return net::Status::NotInitialized;
        case Status::InvalidArgument: return net::Status::InvalidArgument;
        case Status::FrameTooLarge: return net::Status::FrameTooLarge;
        case Status::WouldBlock: return net::Status::WouldBlock;
        default: return net::Status::InterfaceError;
    }
}

net::Status interface_receive_callback(
    void* context,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    const Status status = receive_frame(
        context,
        output,
        output_capacity,
        out_length);
    switch (status) {
        case Status::Ok: return net::Status::Ok;
        case Status::NotInitialized: return net::Status::NotInitialized;
        case Status::InvalidArgument: return net::Status::InvalidArgument;
        case Status::FrameTooLarge: return net::Status::BufferTooSmall;
        case Status::WouldBlock: return net::Status::WouldBlock;
        default: return net::Status::InterfaceError;
    }
}

void mark_failed() {
    if (g_common.base != nullptr && g_common.length > 20U) {
        const uint8_t current = mmio_read8(g_common, 20U);
        mmio_write8(
            g_common,
            20U,
            static_cast<uint8_t>(current | kStatusFailed));
    }
}

bool reset_device() {
    // No common mapping means queue addresses have not been exposed yet.
    if (g_common.base == nullptr || g_common.length <= 20U) return true;
    mmio_write8(g_common, 20U, 0U);
    memory_barrier();
    for (size_t poll = 0U; poll < kResetPollLimit; ++poll) {
        if (mmio_read8(g_common, 20U) == 0U) return true;
        __asm__ volatile("pause");
    }
    return false;
}

Status cleanup_after_reset(Status cause, bool reset_complete) {
    g_initialized = false;
    g_interface = {};
    if (g_command_owned) {
        const uint16_t command = pci::read16(g_device, 0x04U);
        pci::write16(g_device, 0x04U,
            static_cast<uint16_t>(command & ~kCommandBusMaster));
        // Flush the config write. Clearing BME alone does not prove that
        // outstanding DMA is drained, so only a completed reset permits free.
        static_cast<void>(pci::read16(g_device, 0x04U));
    }
    if (!reset_complete) {
        if (g_msix_route.active) {
            const uint8_t control_offset = static_cast<uint8_t>(
                g_msix_route.programmed.capability_offset + 2U);
            pci::write16(g_device, control_offset, static_cast<uint16_t>(
                pci::read16(g_device, control_offset) | pci::msix::CONTROL_FUNCTION_MASK));
            static_cast<void>(pci::read16(g_device, control_offset));
        }
        g_cleanup_blocked = true;
        g_status = Status::DeviceResetFailed;
        return g_status;
    }

    // Reset unmaps queue events before the route is released. Keep MMIO and
    // DMA owned if vector teardown fails; never erase a possibly live route.
    if (g_msix_route.active && pci::msix::disable(&g_msix_route) != pci::msix::Status::Ok) {
        g_interrupt_status = InterruptStatus::CleanupFailed;
        g_cleanup_blocked = true;
        g_status = Status::DeviceCleanupFailed;
        return g_status;
    }
    if (g_interrupt_status == InterruptStatus::MsiXEnabled) {
        g_interrupt_status = InterruptStatus::NotInitialized;
    }
    __atomic_store_n(&g_interrupt_pending, false, __ATOMIC_RELEASE);

    const bool rx_released = release_queue(&g_receive_queue);
    const bool tx_released = release_queue(&g_transmit_queue);
    const bool device_unmapped = unmap_capability(&g_device_config);
    const bool notify_unmapped = unmap_capability(&g_notify);
    const bool common_unmapped = unmap_capability(&g_common);
    const bool table_unmapped = unmap_capability(&g_msix_table);
    const bool pending_unmapped = unmap_capability(&g_msix_pending);
    bool command_restored = true;
    if (g_command_owned) {
        pci::write16(g_device, 0x04U, g_original_command);
        command_restored = pci::read16(g_device, 0x04U) == g_original_command;
        if (command_restored) g_command_owned = false;
    }
    const bool complete = rx_released && tx_released && device_unmapped &&
        notify_unmapped && common_unmapped && table_unmapped && pending_unmapped &&
        command_restored;
    g_cleanup_blocked = !complete;
    g_status = !command_restored ? Status::PciCommandFailed :
        (!complete ? Status::DeviceCleanupFailed : cause);
    return g_status;
}

Status fail(Status status) {
    mark_failed();
    return cleanup_after_reset(status, reset_device());
}

} // namespace

Status initialize() {
    if (g_initialized) return Status::AlreadyInitialized;
    if (g_cleanup_blocked) return g_status;
    g_status = Status::NotInitialized;
    g_detected = false;
    g_common = {};
    g_notify = {};
    g_device_config = {};
    g_receive_queue = {};
    g_transmit_queue = {};
    g_interface = {};
    g_mac = {};
    g_msix_table = {};
    g_msix_pending = {};
    g_msix_route = {};
    g_interrupt_status = InterruptStatus::NotInitialized;
    __atomic_store_n(&g_interrupt_count, UINT64_C(0), __ATOMIC_RELAXED);
    __atomic_store_n(&g_interrupt_pending, false, __ATOMIC_RELEASE);

    const pci::Device* found = pci::find(
        kVirtioVendor,
        kVirtioNetModernDevice);
    if (found == nullptr) {
        found = pci::find(kVirtioVendor, kVirtioNetTransitionalDevice);
    }
    if (found == nullptr) {
        g_status = Status::NoDevice;
        return g_status;
    }
    g_device = *found;
    g_detected = true;

    VirtioCapability common_capability{};
    VirtioCapability notify_capability{};
    VirtioCapability device_capability{};
    if (!scan_capabilities(
            g_device,
            &common_capability,
            &notify_capability,
            &device_capability)) {
        g_status = Status::UnsupportedTransport;
        return g_status;
    }

    g_original_command = pci::read16(g_device, 0x04U);
    g_command_owned = true;
    // Map/reset with memory decoding enabled; enable DMA only after reset.
    pci::write16(g_device, 0x04U, static_cast<uint16_t>(
        (g_original_command | kCommandMemory) & ~kCommandBusMaster));
    if ((pci::read16(g_device, 0x04U) & kCommandMemory) == 0U) {
        return fail(Status::PciCommandFailed);
    }

    if (!map_capability(
            g_device,
            common_capability,
            kCommonVirtualBase,
            &g_common) ||
        !map_capability(
            g_device,
            notify_capability,
            kNotifyVirtualBase,
            &g_notify)) {
        return fail(Status::MappingFailed);
    }
    if (device_capability.present &&
        !map_capability(
            g_device,
            device_capability,
            kDeviceVirtualBase,
            &g_device_config)) {
        return fail(Status::MappingFailed);
    }
    if (g_common.length < 56U) return fail(Status::MissingCapability);

    if (!reset_device()) return cleanup_after_reset(Status::DeviceFault, false);
    if (!prepare_queue_interrupts()) return fail(Status::QueueInterruptFailed);
    pci::write16(g_device, 0x04U, static_cast<uint16_t>(
        pci::read16(g_device, 0x04U) | kCommandMemory | kCommandBusMaster));
    if ((pci::read16(g_device, 0x04U) & (kCommandMemory | kCommandBusMaster)) !=
        (kCommandMemory | kCommandBusMaster)) return fail(Status::PciCommandFailed);
    mmio_write8(g_common, 20U, kStatusAcknowledge);
    mmio_write8(
        g_common,
        20U,
        static_cast<uint8_t>(kStatusAcknowledge | kStatusDriver));

    mmio_write32(g_common, 0U, 0U);
    const uint32_t features_low = mmio_read32(g_common, 4U);
    mmio_write32(g_common, 0U, 1U);
    const uint32_t features_high = mmio_read32(g_common, 4U);
    if ((features_high & kVersion1FeatureHigh) == 0U) {
        return fail(Status::FeatureNegotiationFailed);
    }

    const uint32_t driver_low = features_low & kMacFeature;
    mmio_write32(g_common, 8U, 0U);
    mmio_write32(g_common, 12U, driver_low);
    mmio_write32(g_common, 8U, 1U);
    mmio_write32(g_common, 12U, kVersion1FeatureHigh);
    uint8_t device_status = static_cast<uint8_t>(
        kStatusAcknowledge | kStatusDriver | kStatusFeaturesOk);
    mmio_write8(g_common, 20U, device_status);
    if ((mmio_read8(g_common, 20U) & kStatusFeaturesOk) == 0U) {
        return fail(Status::FeatureNegotiationFailed);
    }

    if ((driver_low & kMacFeature) != 0U) {
        if (!read_mac_from_device(&g_mac)) {
            return fail(Status::DeviceFault);
        }
    } else {
        g_mac = fallback_mac(g_device);
    }

    if (mmio_read16(g_common, 18U) < 2U) {
        return fail(Status::QueueUnavailable);
    }
    if (!configure_queue(0U, notify_capability, &g_receive_queue) ||
        !configure_queue(1U, notify_capability, &g_transmit_queue)) {
        return fail(g_interrupt_status == InterruptStatus::QueueRejected
            ? Status::QueueInterruptFailed : Status::QueueConfigurationFailed);
    }
    prepare_receive_queue();

    device_status = static_cast<uint8_t>(device_status | kStatusDriverOk);
    mmio_write8(g_common, 20U, device_status);
    memory_barrier();
    if ((mmio_read8(g_common, 20U) & kStatusDriverOk) == 0U) {
        return fail(Status::DeviceFault);
    }

    g_interface.context = nullptr;
    g_interface.transmit = interface_transmit_callback;
    g_interface.receive = interface_receive_callback;
    g_interface.hardware_address = g_mac;
    g_interface.mtu = ETHERNET_MTU;
    g_initialized = true;
    g_status = Status::Ok;
    return g_status;
}

bool initialized() { return g_initialized; }
InterruptDiagnostics interrupt_diagnostics() {
    return {g_interrupt_status, g_msix_route.active ? g_msix_route.vector.vector : uint8_t{0},
        __atomic_load_n(&g_interrupt_count, __ATOMIC_RELAXED),
        __atomic_load_n(&g_interrupt_pending, __ATOMIC_ACQUIRE)};
}

const char* interrupt_status_name(InterruptStatus status) {
    switch (status) {
        case InterruptStatus::NotInitialized: return "NOT_INITIALIZED";
        case InterruptStatus::MsiXEnabled: return "MSI_X_ENABLED";
        case InterruptStatus::LocalApicUnavailable: return "LOCAL_APIC_UNAVAILABLE";
        case InterruptStatus::CapabilityUnavailable: return "CAPABILITY_UNAVAILABLE";
        case InterruptStatus::CapabilityMalformed: return "CAPABILITY_MALFORMED";
        case InterruptStatus::BarUnavailable: return "BAR_UNAVAILABLE";
        case InterruptStatus::MappingUnavailable: return "MAPPING_UNAVAILABLE";
        case InterruptStatus::RouteUnavailable: return "ROUTE_UNAVAILABLE";
        case InterruptStatus::QueueRejected: return "QUEUE_VECTOR_REJECTED";
        case InterruptStatus::CleanupFailed: return "CLEANUP_FAILED";
    }
    return "UNKNOWN";
}
bool detected() { return g_detected; }
Status last_status() { return g_status; }
NetworkInterface* interface() {
    return g_initialized ? &g_interface : nullptr;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "VirtIO-net not initialized";
        case Status::AlreadyInitialized: return "VirtIO-net already initialized";
        case Status::NoDevice: return "VirtIO-net PCI function not found";
        case Status::UnsupportedTransport:
            return "VirtIO modern PCI capabilities missing";
        case Status::MissingCapability: return "VirtIO capability layout incomplete";
        case Status::MappingFailed: return "VirtIO MMIO capability mapping failed";
        case Status::FeatureNegotiationFailed:
            return "VirtIO feature negotiation failed";
        case Status::QueueUnavailable: return "VirtIO RX/TX queues unavailable";
        case Status::QueueAllocationFailed:
            return "VirtIO queue DMA allocation failed";
        case Status::QueueConfigurationFailed:
            return "VirtIO queue configuration failed";
        case Status::InvalidArgument: return "invalid VirtIO-net argument";
        case Status::FrameTooLarge: return "VirtIO-net frame exceeds MTU";
        case Status::WouldBlock: return "VirtIO-net queue would block";
        case Status::DeviceFault: return "VirtIO-net device fault";
        case Status::PciCommandFailed: return "VirtIO-net PCI command failed";
        case Status::DeviceResetFailed: return "VirtIO-net reset failed; resources quarantined";
        case Status::DeviceCleanupFailed: return "VirtIO-net resource cleanup incomplete";
        case Status::QueueInterruptFailed: return "VirtIO-net interrupt configuration failed";
    }
    return "unknown VirtIO-net status";
}

} // namespace net::virtio_net
