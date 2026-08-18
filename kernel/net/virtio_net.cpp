#include "virtio_net.hpp"

#include "../drivers/pci.hpp"
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
constexpr uintptr_t kCommonVirtualBase = UINT64_C(0xFFFFB20000000000);
constexpr uintptr_t kNotifyVirtualBase = UINT64_C(0xFFFFB20000010000);
constexpr uintptr_t kDeviceVirtualBase = UINT64_C(0xFFFFB20000020000);

struct VirtioCapability {
    bool present;
    uint8_t bar;
    uint32_t offset;
    uint32_t length;
    uint32_t notify_multiplier;
};

struct MappedRegion {
    volatile uint8_t* base;
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

bool map_capability(
    const pci::Device& device,
    const VirtioCapability& capability,
    uintptr_t virtual_base,
    MappedRegion* output) {
    if (output == nullptr || !capability_valid(capability)) return false;
    bool io_space = false;
    const uint64_t bar = pci::bar_address(device, capability.bar, &io_space);
    if (bar == 0U || io_space || bar > UINT64_MAX - capability.offset) return false;

    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) return false;
    constexpr uint64_t page_size = memory::virtual_memory::PAGE_SIZE;
    constexpr uint64_t page_mask = page_size - 1U;
    const uint64_t physical = bar + capability.offset;
    const uint64_t aligned = physical & ~page_mask;
    const size_t prefix = static_cast<size_t>(physical & page_mask);
    const size_t extent = prefix + capability.length;
    const size_t pages = static_cast<size_t>((extent + page_mask) / page_size);
    if (pages == 0U || pages > 2U) return false;

    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    size_t mapped = 0U;
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
    }
    if (mapped != pages) {
        while (mapped != 0U) {
            --mapped;
            static_cast<void>(memory::virtual_memory::unmap_page(
                address_space,
                virtual_base + mapped * page_size));
        }
        return false;
    }
    output->base = reinterpret_cast<volatile uint8_t*>(virtual_base + prefix);
    output->length = capability.length;
    return true;
}

void release_queue(Queue* queue) {
    if (queue == nullptr) return;
    for (size_t index = 0U; index < kQueueCapacity; ++index) {
        if (queue->buffers[index].allocated) {
            static_cast<void>(storage::dma::release_page(&queue->buffers[index]));
        }
    }
    if (queue->descriptor_page.allocated) {
        static_cast<void>(storage::dma::release_page(&queue->descriptor_page));
    }
    if (queue->available_page.allocated) {
        static_cast<void>(storage::dma::release_page(&queue->available_page));
    }
    if (queue->used_page.allocated) {
        static_cast<void>(storage::dma::release_page(&queue->used_page));
    }
    *queue = {};
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
    mmio_write16(g_common, 26U, kNoMsixVector);
    if (!write_queue_address(32U, queue->descriptor_page.physical_address) ||
        !write_queue_address(40U, queue->available_page.physical_address) ||
        !write_queue_address(48U, queue->used_page.physical_address)) {
        release_queue(queue);
        return false;
    }

    const uint16_t notify_offset = mmio_read16(g_common, 30U);
    const uint64_t byte_offset =
        static_cast<uint64_t>(notify_offset) * notify_capability.notify_multiplier;
    if (byte_offset > SIZE_MAX ||
        static_cast<size_t>(byte_offset) + sizeof(uint16_t) > g_notify.length) {
        release_queue(queue);
        return false;
    }
    queue->notify = reinterpret_cast<volatile uint16_t*>(
        g_notify.base + static_cast<size_t>(byte_offset));
    memory_barrier();
    mmio_write16(g_common, 28U, 1U);
    if (mmio_read16(g_common, 28U) != 1U) {
        release_queue(queue);
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

Status fail(Status status) {
    mark_failed();
    release_queue(&g_receive_queue);
    release_queue(&g_transmit_queue);
    g_initialized = false;
    g_status = status;
    return status;
}

} // namespace

Status initialize() {
    if (g_initialized) return Status::AlreadyInitialized;
    g_status = Status::NotInitialized;
    g_detected = false;
    g_common = {};
    g_notify = {};
    g_device_config = {};
    g_receive_queue = {};
    g_transmit_queue = {};
    g_interface = {};
    g_mac = {};

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

    const uint32_t command = pci::read32(g_device, UINT8_C(0x04));
    pci::write32(
        g_device,
        UINT8_C(0x04),
        (command & UINT32_C(0x0000FFFF)) | UINT32_C(0x00000006));

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

    mmio_write8(g_common, 20U, 0U);
    memory_barrier();
    if (mmio_read8(g_common, 20U) != 0U) {
        return fail(Status::DeviceFault);
    }
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
        return fail(Status::QueueConfigurationFailed);
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
    }
    return "unknown VirtIO-net status";
}

} // namespace net::virtio_net
