#include "virtio_block.hpp"

#include "device_registry.hpp"
#include "dma.hpp"
#include "virtio_block_protocol.hpp"
#include "../drivers/pci.hpp"
#include "../drivers/virtio/pci_transport.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/virtual_memory.hpp"

namespace storage::virtio_block {
namespace {

constexpr uint16_t kVirtioBlockTransitionalDevice = UINT16_C(0x1001);
constexpr uint16_t kVirtioBlockModernDevice = UINT16_C(0x1042);

constexpr uint16_t kCommandMemory = UINT16_C(2);
constexpr uint16_t kCommandBusMaster = UINT16_C(4);

constexpr uint8_t kStatusAcknowledge = UINT8_C(1);
constexpr uint8_t kStatusDriver = UINT8_C(2);
constexpr uint8_t kStatusDriverOk = UINT8_C(4);
constexpr uint8_t kStatusFeaturesOk = UINT8_C(8);
constexpr uint8_t kStatusFailed = UINT8_C(128);

constexpr uint32_t kFeatureReadOnly = UINT32_C(1) << 5U;
constexpr uint32_t kFeatureBlockSize = UINT32_C(1) << 6U;
constexpr uint32_t kFeatureFlush = UINT32_C(1) << 9U;
constexpr uint32_t kVersion1FeatureHigh = UINT32_C(1);

constexpr uint16_t kNoMsixVector = UINT16_C(0xFFFF);
constexpr uint16_t kDescriptorNext = UINT16_C(1);
constexpr uint16_t kDescriptorWrite = UINT16_C(2);
constexpr uint16_t kAvailNoInterrupt = UINT16_C(1);
constexpr uint16_t kQueueCapacity = UINT16_C(8);
constexpr size_t kRequestStatusOffset = 32U;
constexpr size_t kCommonConfigBytes = 56U;
constexpr size_t kMaximumCapabilityBytes = memory::virtual_memory::PAGE_SIZE;
constexpr size_t kPollLimit = 10000000U;

constexpr uintptr_t kCommonVirtualBase = UINT64_C(0xFFFFB90000000000);
constexpr uintptr_t kNotifyVirtualBase = UINT64_C(0xFFFFB90000010000);
constexpr uintptr_t kDeviceVirtualBase = UINT64_C(0xFFFFB90000020000);

using TransportCapability =
    drivers::virtio::pci_transport::Capability;

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

static_assert(sizeof(Descriptor) == 16U, "VirtIO descriptor ABI mismatch");
static_assert(sizeof(UsedElement) == 8U, "VirtIO used element ABI mismatch");

struct Queue {
    uint16_t index;
    uint16_t size;
    dma::Page descriptor_page;
    dma::Page available_page;
    dma::Page used_page;
    dma::Page request_page;
    dma::Page data_page;
    Descriptor* descriptors;
    volatile uint16_t* available;
    volatile uint16_t* used_header;
    volatile UsedElement* used_elements;
    volatile uint16_t* notify;
    uint16_t available_index;
    uint16_t last_used_index;
    bool configured;
};

struct Controller {
    pci::Device pci_device;
    drivers::virtio::pci_transport::Layout transport;
    MappedRegion common;
    MappedRegion notify;
    MappedRegion device_config;
    Queue queue;
    protocol::Geometry geometry;
    DeviceInfo info;
    block::Device block;
    uint16_t original_command;
    bool command_owned;
    bool ready;
    bool busy;
    bool cleanup_blocked;
    Status status;
};

Controller g_controller{};

void memory_barrier() {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    if (bytes == nullptr) return;
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

void copy_bytes(void* destination, const void* source, size_t size) {
    auto* out = static_cast<uint8_t*>(destination);
    const auto* in = static_cast<const uint8_t*>(source);
    if (out == nullptr || in == nullptr) return;
    for (size_t index = 0U; index < size; ++index) out[index] = in[index];
}

uint8_t mmio_read8(const MappedRegion& region, size_t offset) {
    if (region.base == nullptr || offset >= region.length) return 0U;
    return *(region.base + offset);
}

uint16_t mmio_read16(const MappedRegion& region, size_t offset) {
    if (region.base == nullptr || offset + sizeof(uint16_t) > region.length) {
        return 0U;
    }
    return *reinterpret_cast<volatile uint16_t*>(region.base + offset);
}

uint32_t mmio_read32(const MappedRegion& region, size_t offset) {
    if (region.base == nullptr || offset + sizeof(uint32_t) > region.length) {
        return 0U;
    }
    return *reinterpret_cast<volatile uint32_t*>(region.base + offset);
}

uint64_t mmio_read64(const MappedRegion& region, size_t offset) {
    if (region.base == nullptr || offset + sizeof(uint64_t) > region.length) {
        return 0U;
    }
    const uint64_t low = mmio_read32(region, offset);
    const uint64_t high = mmio_read32(region, offset + 4U);
    return low | (high << 32U);
}

void mmio_write8(const MappedRegion& region, size_t offset, uint8_t value) {
    if (region.base == nullptr || offset >= region.length) return;
    *(region.base + offset) = value;
}

void mmio_write16(const MappedRegion& region, size_t offset, uint16_t value) {
    if (region.base == nullptr || offset + sizeof(uint16_t) > region.length) {
        return;
    }
    *reinterpret_cast<volatile uint16_t*>(region.base + offset) = value;
}

void mmio_write32(const MappedRegion& region, size_t offset, uint32_t value) {
    if (region.base == nullptr || offset + sizeof(uint32_t) > region.length) {
        return;
    }
    *reinterpret_cast<volatile uint32_t*>(region.base + offset) = value;
}

bool unmap_region(MappedRegion* region) {
    if (region == nullptr) return false;
    if (region->mapped_pages == 0U) {
        *region = {};
        return true;
    }
    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) return false;
    while (region->mapped_pages != 0U) {
        const uintptr_t target = region->virtual_base +
            (region->mapped_pages - 1U) * memory::virtual_memory::PAGE_SIZE;
        const auto status = memory::virtual_memory::unmap_page(
            address_space, target);
        if (status != memory::virtual_memory::Status::Ok &&
            status != memory::virtual_memory::Status::NotMapped) {
            return false;
        }
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
        length > kMaximumCapabilityBytes) {
        return false;
    }

    auto* address_space = memory::kernel_virtual_memory::address_space();
    if (address_space == nullptr) return false;
    constexpr uint64_t page_size = memory::virtual_memory::PAGE_SIZE;
    constexpr uint64_t page_mask = page_size - 1U;
    const uint64_t aligned = physical & ~page_mask;
    const size_t prefix = static_cast<size_t>(physical & page_mask);
    if (length > SIZE_MAX - prefix) return false;
    const size_t extent = prefix + length;
    const size_t pages = static_cast<size_t>(
        (static_cast<uint64_t>(extent) + page_mask) / page_size);
    if (pages == 0U || (virtual_base & page_mask) != 0U ||
        pages > UINTPTR_MAX / static_cast<uintptr_t>(page_size) ||
        virtual_base > UINTPTR_MAX -
            pages * static_cast<uintptr_t>(page_size) ||
        aligned > UINT64_MAX -
            static_cast<uint64_t>(pages) * page_size) {
        return false;
    }

    const auto flags = memory::virtual_memory::MapFlags::Writable |
        memory::virtual_memory::MapFlags::WriteThrough |
        memory::virtual_memory::MapFlags::CacheDisable |
        memory::virtual_memory::MapFlags::NoExecute;
    output->virtual_base = virtual_base;
    size_t mapped = 0U;
    for (; mapped < pages; ++mapped) {
        memory::virtual_memory::Mapping existing{};
        const uintptr_t target =
            virtual_base + mapped * static_cast<uintptr_t>(page_size);
        if (memory::virtual_memory::query_page(
                address_space, target, &existing) !=
                memory::virtual_memory::Status::NotMapped ||
            memory::virtual_memory::map_page(
                address_space,
                target,
                aligned + static_cast<uint64_t>(mapped) * page_size,
                flags) != memory::virtual_memory::Status::Ok) {
            break;
        }
        ++output->mapped_pages;
    }
    if (mapped != pages) {
        static_cast<void>(unmap_region(output));
        return false;
    }
    output->base = reinterpret_cast<volatile uint8_t*>(
        virtual_base + prefix);
    output->length = length;
    return true;
}

bool map_capability(
    const TransportCapability& capability,
    uintptr_t virtual_base,
    MappedRegion* output) {
    if (!drivers::virtio::pci_transport::region_valid(
            capability, kMaximumCapabilityBytes)) {
        return false;
    }
    bool io_space = false;
    const uint64_t bar = pci::bar_address(
        g_controller.pci_device, capability.bar, &io_space);
    if (bar == 0U || io_space ||
        bar > UINT64_MAX - capability.offset) {
        return false;
    }
    return map_mmio(
        bar + capability.offset,
        capability.length,
        virtual_base,
        output);
}

bool reset_device() {
    if (g_controller.common.base == nullptr ||
        g_controller.common.length <= 20U) {
        return true;
    }
    mmio_write8(g_controller.common, 20U, 0U);
    memory_barrier();
    for (size_t poll = 0U; poll < kPollLimit; ++poll) {
        if (mmio_read8(g_controller.common, 20U) == 0U) return true;
        __asm__ volatile("pause");
    }
    return false;
}

void mark_failed() {
    if (g_controller.common.base == nullptr ||
        g_controller.common.length <= 20U) {
        return;
    }
    const uint8_t current = mmio_read8(g_controller.common, 20U);
    mmio_write8(
        g_controller.common,
        20U,
        static_cast<uint8_t>(current | kStatusFailed));
}

bool release_queue(Queue* queue) {
    if (queue == nullptr) return false;
    bool complete = true;
    for (dma::Page* page : {
            &queue->data_page,
            &queue->request_page,
            &queue->used_page,
            &queue->available_page,
            &queue->descriptor_page}) {
        if (!page->allocated) continue;
        if (dma::release_page(page) != dma::Status::Ok) complete = false;
    }
    if (complete) *queue = {};
    return complete;
}

Status cleanup_after_reset(Status cause, bool reset_complete) {
    g_controller.ready = false;
    g_controller.busy = false;
    if (!reset_complete) {
        g_controller.cleanup_blocked = true;
        g_controller.status = Status::DeviceResetFailed;
        return g_controller.status;
    }

    if (g_controller.command_owned) {
        const uint16_t command = pci::read16(g_controller.pci_device, 0x04U);
        pci::write16(
            g_controller.pci_device,
            0x04U,
            static_cast<uint16_t>(command & ~kCommandBusMaster));
        static_cast<void>(pci::read16(g_controller.pci_device, 0x04U));
    }

    const bool queue_released = release_queue(&g_controller.queue);
    const bool device_unmapped = unmap_region(&g_controller.device_config);
    const bool notify_unmapped = unmap_region(&g_controller.notify);
    const bool common_unmapped = unmap_region(&g_controller.common);

    bool command_restored = true;
    if (g_controller.command_owned) {
        pci::write16(
            g_controller.pci_device,
            0x04U,
            g_controller.original_command);
        command_restored =
            pci::read16(g_controller.pci_device, 0x04U) ==
            g_controller.original_command;
        if (command_restored) g_controller.command_owned = false;
    }

    const bool complete = queue_released && device_unmapped &&
        notify_unmapped && common_unmapped && command_restored;
    g_controller.cleanup_blocked = !complete;
    g_controller.status = complete ? cause : Status::ResourceReleaseFailed;
    return g_controller.status;
}

Status fail(Status status) {
    mark_failed();
    return cleanup_after_reset(status, reset_device());
}

uint16_t choose_queue_size(uint16_t maximum) {
    uint16_t size = kQueueCapacity;
    while (size > maximum && size > 1U) {
        size = static_cast<uint16_t>(size >> 1U);
    }
    return size >= 4U && size <= maximum ? size : 0U;
}

bool allocate_queue_storage(Queue* queue, uint16_t size) {
    if (queue == nullptr || size < 4U || size > kQueueCapacity) return false;
    if (dma::allocate_page(true, &queue->descriptor_page) != dma::Status::Ok ||
        dma::allocate_page(true, &queue->available_page) != dma::Status::Ok ||
        dma::allocate_page(true, &queue->used_page) != dma::Status::Ok ||
        dma::allocate_page(true, &queue->request_page) != dma::Status::Ok ||
        dma::allocate_page(true, &queue->data_page) != dma::Status::Ok) {
        static_cast<void>(release_queue(queue));
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
    clear_bytes(
        queue->request_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);
    clear_bytes(
        queue->data_page.virtual_address,
        memory::virtual_memory::PAGE_SIZE);

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
    if (g_controller.common.length < low_offset + 8U) return false;
    mmio_write32(
        g_controller.common, low_offset, static_cast<uint32_t>(address));
    mmio_write32(
        g_controller.common,
        low_offset + 4U,
        static_cast<uint32_t>(address >> 32U));
    return true;
}

bool configure_queue(const TransportCapability& notify_capability) {
    Queue& queue = g_controller.queue;
    if (g_controller.common.length < kCommonConfigBytes ||
        g_controller.notify.base == nullptr) {
        return false;
    }

    mmio_write16(g_controller.common, 22U, 0U);
    const uint16_t maximum = mmio_read16(g_controller.common, 24U);
    const uint16_t size = choose_queue_size(maximum);
    if (size == 0U || mmio_read16(g_controller.common, 28U) != 0U) {
        return false;
    }
    if (!allocate_queue_storage(&queue, size)) return false;
    queue.index = 0U;

    mmio_write16(g_controller.common, 24U, size);
    mmio_write16(g_controller.common, 26U, kNoMsixVector);
    if (!write_queue_address(32U, queue.descriptor_page.physical_address) ||
        !write_queue_address(40U, queue.available_page.physical_address) ||
        !write_queue_address(48U, queue.used_page.physical_address)) {
        return false;
    }

    const uint16_t notify_offset =
        mmio_read16(g_controller.common, 30U);
    const uint64_t byte_offset =
        static_cast<uint64_t>(notify_offset) *
        static_cast<uint64_t>(notify_capability.notify_multiplier);
    if (g_controller.notify.length < sizeof(uint16_t) ||
        byte_offset >
            g_controller.notify.length - sizeof(uint16_t)) {
        return false;
    }
    queue.notify = reinterpret_cast<volatile uint16_t*>(
        g_controller.notify.base + static_cast<size_t>(byte_offset));

    memory_barrier();
    mmio_write16(g_controller.common, 28U, 1U);
    if (mmio_read16(g_controller.common, 28U) != 1U) return false;
    queue.configured = true;
    return true;
}

bool read_geometry(
    bool block_size_feature,
    bool read_only,
    bool flush_supported,
    protocol::Geometry* output) {
    if (output == nullptr || g_controller.device_config.base == nullptr ||
        g_controller.device_config.length < 8U) {
        return false;
    }
    if (block_size_feature && g_controller.device_config.length < 24U) {
        return false;
    }

    for (size_t attempt = 0U; attempt < 8U; ++attempt) {
        const uint8_t generation_before =
            mmio_read8(g_controller.common, 21U);
        const uint64_t capacity =
            mmio_read64(g_controller.device_config, 0U);
        const uint32_t block_size = block_size_feature
            ? mmio_read32(g_controller.device_config, 20U)
            : protocol::kSectorBytes;
        memory_barrier();
        const uint8_t generation_after =
            mmio_read8(g_controller.common, 21U);
        if (generation_before != generation_after) continue;
        return protocol::decode_geometry(
            capacity,
            block_size_feature,
            block_size,
            read_only,
            flush_supported,
            output) == protocol::Status::Ok;
    }
    return false;
}

void notify_queue() {
    memory_barrier();
    if (g_controller.queue.notify != nullptr) {
        *g_controller.queue.notify = g_controller.queue.index;
    }
}

Status submit(
    protocol::Operation operation,
    uint64_t first_block,
    uint64_t block_count,
    size_t* out_transfer_bytes) {
    if (!g_controller.ready || !g_controller.queue.configured) {
        return Status::DeviceFault;
    }
    if (g_controller.busy) return Status::DeviceBusy;
    g_controller.busy = true;

    protocol::RequestHeader header{};
    size_t transfer_bytes = 0U;
    const protocol::Status request_status = protocol::build_request(
        operation,
        g_controller.geometry,
        first_block,
        block_count,
        &header,
        &transfer_bytes);
    if (request_status != protocol::Status::Ok) {
        g_controller.busy = false;
        switch (request_status) {
            case protocol::Status::OutOfRange:
            case protocol::Status::ArithmeticOverflow:
                return Status::UnsupportedGeometry;
            case protocol::Status::DeviceUnsupported:
                return Status::UnsupportedOperation;
            default:
                return Status::DeviceFault;
        }
    }
    if (transfer_bytes > memory::virtual_memory::PAGE_SIZE) {
        g_controller.busy = false;
        return Status::UnsupportedGeometry;
    }

    Queue& queue = g_controller.queue;
    if (queue.used_header[1] != queue.last_used_index) {
        g_controller.busy = false;
        return Status::DeviceFault;
    }

    auto* request = static_cast<protocol::RequestHeader*>(
        queue.request_page.virtual_address);
    *request = header;
    auto* request_bytes = static_cast<uint8_t*>(
        queue.request_page.virtual_address);
    volatile uint8_t* const device_status =
        reinterpret_cast<volatile uint8_t*>(
            request_bytes + kRequestStatusOffset);
    *device_status = UINT8_C(0xFF);

    Descriptor& head = queue.descriptors[0U];
    head.address = queue.request_page.physical_address;
    head.length = static_cast<uint32_t>(sizeof(protocol::RequestHeader));
    head.flags = kDescriptorNext;
    head.next = operation == protocol::Operation::Flush ? 2U : 1U;

    if (operation != protocol::Operation::Flush) {
        Descriptor& data = queue.descriptors[1U];
        data.address = queue.data_page.physical_address;
        data.length = static_cast<uint32_t>(transfer_bytes);
        data.flags = static_cast<uint16_t>(
            kDescriptorNext |
            (operation == protocol::Operation::Read
                ? kDescriptorWrite
                : UINT16_C(0)));
        data.next = 2U;
    }

    Descriptor& status_descriptor = queue.descriptors[2U];
    status_descriptor.address =
        queue.request_page.physical_address + kRequestStatusOffset;
    status_descriptor.length = 1U;
    status_descriptor.flags = kDescriptorWrite;
    status_descriptor.next = 0U;

    const uint16_t slot = static_cast<uint16_t>(
        queue.available_index % queue.size);
    queue.available[2U + slot] = 0U;
    memory_barrier();
    ++queue.available_index;
    queue.available[1] = queue.available_index;
    memory_barrier();
    notify_queue();

    const uint16_t expected_used =
        static_cast<uint16_t>(queue.last_used_index + 1U);
    bool completed = false;
    for (size_t poll = 0U; poll < kPollLimit; ++poll) {
        if (queue.used_header[1] == expected_used) {
            completed = true;
            break;
        }
        if (static_cast<uint16_t>(
                queue.used_header[1] - queue.last_used_index) > 1U) {
            break;
        }
        __asm__ volatile("pause");
    }
    if (!completed) {
        g_controller.busy = false;
        return Status::TimedOut;
    }

    memory_barrier();
    const uint16_t used_slot = static_cast<uint16_t>(
        queue.last_used_index % queue.size);
    if (queue.used_elements[used_slot].id != 0U) {
        g_controller.busy = false;
        return Status::DeviceFault;
    }
    queue.last_used_index = expected_used;

    const protocol::Status completion_status =
        protocol::decode_device_status(*device_status);
    g_controller.busy = false;
    if (out_transfer_bytes != nullptr) {
        *out_transfer_bytes = transfer_bytes;
    }
    switch (completion_status) {
        case protocol::Status::Ok: return Status::Ok;
        case protocol::Status::DeviceIoError: return Status::IoError;
        case protocol::Status::DeviceUnsupported:
            return Status::UnsupportedOperation;
        default: return Status::DeviceFault;
    }
}

block::Status block_status(Status status) {
    switch (status) {
        case Status::Ok: return block::Status::Ok;
        case Status::DeviceBusy: return block::Status::DeviceBusy;
        case Status::TimedOut: return block::Status::TimedOut;
        case Status::IoError: return block::Status::IoError;
        case Status::UnsupportedOperation:
            return block::Status::Unsupported;
        case Status::UnsupportedGeometry:
            return block::Status::OutOfRange;
        case Status::NoDevice:
            return block::Status::NoDevice;
        default:
            return block::Status::DeviceFault;
    }
}

block::Status read_blocks_callback(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    void* destination) {
    if (context != &g_controller || destination == nullptr ||
        block_count == 0U || !g_controller.ready) {
        return block::Status::InvalidArgument;
    }
    if (g_controller.geometry.block_size >
        memory::virtual_memory::PAGE_SIZE) {
        return block::Status::Unsupported;
    }

    auto* output = static_cast<uint8_t*>(destination);
    const uint64_t maximum_blocks =
        memory::virtual_memory::PAGE_SIZE /
        g_controller.geometry.block_size;
    uint64_t completed_blocks = 0U;
    while (completed_blocks < block_count) {
        const uint64_t remaining = block_count - completed_blocks;
        const uint64_t chunk =
            remaining < maximum_blocks ? remaining : maximum_blocks;
        size_t transfer_bytes = 0U;
        const Status status = submit(
            protocol::Operation::Read,
            first_block + completed_blocks,
            chunk,
            &transfer_bytes);
        if (status != Status::Ok) return block_status(status);
        copy_bytes(
            output + static_cast<size_t>(
                completed_blocks *
                static_cast<uint64_t>(g_controller.geometry.block_size)),
            g_controller.queue.data_page.virtual_address,
            transfer_bytes);
        completed_blocks += chunk;
    }
    return block::Status::Ok;
}

block::Status write_blocks_callback(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    const void* source) {
    if (context != &g_controller || source == nullptr ||
        block_count == 0U || !g_controller.ready) {
        return block::Status::InvalidArgument;
    }
    if (g_controller.geometry.read_only) return block::Status::ReadOnly;
    if (g_controller.geometry.block_size >
        memory::virtual_memory::PAGE_SIZE) {
        return block::Status::Unsupported;
    }

    const auto* input = static_cast<const uint8_t*>(source);
    const uint64_t maximum_blocks =
        memory::virtual_memory::PAGE_SIZE /
        g_controller.geometry.block_size;
    uint64_t completed_blocks = 0U;
    while (completed_blocks < block_count) {
        const uint64_t remaining = block_count - completed_blocks;
        const uint64_t chunk =
            remaining < maximum_blocks ? remaining : maximum_blocks;
        const size_t transfer_bytes = static_cast<size_t>(
            chunk *
            static_cast<uint64_t>(g_controller.geometry.block_size));
        copy_bytes(
            g_controller.queue.data_page.virtual_address,
            input + static_cast<size_t>(
                completed_blocks *
                static_cast<uint64_t>(g_controller.geometry.block_size)),
            transfer_bytes);
        size_t submitted_bytes = 0U;
        const Status status = submit(
            protocol::Operation::Write,
            first_block + completed_blocks,
            chunk,
            &submitted_bytes);
        if (status != Status::Ok) return block_status(status);
        if (submitted_bytes != transfer_bytes) {
            return block::Status::CommandFailed;
        }
        completed_blocks += chunk;
    }
    return block::Status::Ok;
}

block::Status flush_callback(void* context) {
    if (context != &g_controller || !g_controller.ready) {
        return block::Status::InvalidArgument;
    }
    if (!g_controller.geometry.flush_supported) {
        return block::Status::Unsupported;
    }
    return block_status(submit(
        protocol::Operation::Flush, 0U, 0U, nullptr));
}

} // namespace

Status initialize() {
    if (g_controller.ready) return Status::AlreadyInitialized;
    if (g_controller.cleanup_blocked) return g_controller.status;
    g_controller = {};
    g_controller.status = Status::NoDevice;

    const pci::Device* found = pci::find(
        drivers::virtio::pci_transport::kVirtioVendor,
        kVirtioBlockModernDevice);
    if (found == nullptr) {
        found = pci::find(
            drivers::virtio::pci_transport::kVirtioVendor,
            kVirtioBlockTransitionalDevice);
    }
    if (found == nullptr) return g_controller.status;
    g_controller.pci_device = *found;

    if (drivers::virtio::pci_transport::discover(
            g_controller.pci_device,
            &g_controller.transport) !=
            drivers::virtio::pci_transport::Status::Ok ||
        !drivers::virtio::pci_transport::region_valid(
            g_controller.transport.common, kMaximumCapabilityBytes) ||
        !drivers::virtio::pci_transport::region_valid(
            g_controller.transport.notify, kMaximumCapabilityBytes) ||
        !drivers::virtio::pci_transport::region_valid(
            g_controller.transport.device, kMaximumCapabilityBytes)) {
        g_controller.status = Status::UnsupportedTransport;
        return g_controller.status;
    }

    g_controller.original_command =
        pci::read16(g_controller.pci_device, 0x04U);
    g_controller.command_owned = true;
    pci::write16(
        g_controller.pci_device,
        0x04U,
        static_cast<uint16_t>(
            (g_controller.original_command | kCommandMemory) &
            ~kCommandBusMaster));
    if ((pci::read16(g_controller.pci_device, 0x04U) &
         kCommandMemory) == 0U) {
        return fail(Status::PciCommandFailed);
    }

    if (!map_capability(
            g_controller.transport.common,
            kCommonVirtualBase,
            &g_controller.common) ||
        !map_capability(
            g_controller.transport.notify,
            kNotifyVirtualBase,
            &g_controller.notify) ||
        !map_capability(
            g_controller.transport.device,
            kDeviceVirtualBase,
            &g_controller.device_config)) {
        return fail(Status::MappingFailed);
    }
    if (g_controller.common.length < kCommonConfigBytes) {
        return fail(Status::UnsupportedTransport);
    }
    if (!reset_device()) {
        return cleanup_after_reset(Status::DeviceResetFailed, false);
    }

    uint8_t device_status = kStatusAcknowledge;
    mmio_write8(g_controller.common, 20U, device_status);
    device_status = static_cast<uint8_t>(
        device_status | kStatusDriver);
    mmio_write8(g_controller.common, 20U, device_status);

    mmio_write32(g_controller.common, 0U, 0U);
    const uint32_t features_low =
        mmio_read32(g_controller.common, 4U);
    mmio_write32(g_controller.common, 0U, 1U);
    const uint32_t features_high =
        mmio_read32(g_controller.common, 4U);
    if ((features_high & kVersion1FeatureHigh) == 0U) {
        return fail(Status::FeatureNegotiationFailed);
    }

    const uint32_t optional_features =
        kFeatureReadOnly | kFeatureBlockSize | kFeatureFlush;
    const uint32_t driver_low = features_low & optional_features;
    mmio_write32(g_controller.common, 8U, 0U);
    mmio_write32(g_controller.common, 12U, driver_low);
    mmio_write32(g_controller.common, 8U, 1U);
    mmio_write32(g_controller.common, 12U, kVersion1FeatureHigh);

    device_status = static_cast<uint8_t>(
        device_status | kStatusFeaturesOk);
    mmio_write8(g_controller.common, 20U, device_status);
    if ((mmio_read8(g_controller.common, 20U) &
         kStatusFeaturesOk) == 0U) {
        return fail(Status::FeatureNegotiationFailed);
    }

    const bool read_only = (driver_low & kFeatureReadOnly) != 0U;
    const bool block_size_feature =
        (driver_low & kFeatureBlockSize) != 0U;
    const bool flush_supported =
        (driver_low & kFeatureFlush) != 0U;
    if (!read_geometry(
            block_size_feature,
            read_only,
            flush_supported,
            &g_controller.geometry)) {
        return fail(Status::MissingDeviceConfig);
    }
    if (g_controller.geometry.block_size >
        memory::virtual_memory::PAGE_SIZE) {
        return fail(Status::UnsupportedGeometry);
    }

    pci::write16(
        g_controller.pci_device,
        0x04U,
        static_cast<uint16_t>(
            pci::read16(g_controller.pci_device, 0x04U) |
            kCommandMemory | kCommandBusMaster));
    if ((pci::read16(g_controller.pci_device, 0x04U) &
         (kCommandMemory | kCommandBusMaster)) !=
        (kCommandMemory | kCommandBusMaster)) {
        return fail(Status::PciCommandFailed);
    }

    if (mmio_read16(g_controller.common, 18U) == 0U) {
        return fail(Status::QueueUnavailable);
    }
    if (!configure_queue(g_controller.transport.notify)) {
        return fail(
            g_controller.queue.descriptor_page.allocated
                ? Status::QueueConfigurationFailed
                : Status::DmaAllocationFailed);
    }

    device_status = static_cast<uint8_t>(
        device_status | kStatusDriverOk);
    mmio_write8(g_controller.common, 20U, device_status);
    memory_barrier();
    if ((mmio_read8(g_controller.common, 20U) &
         kStatusDriverOk) == 0U) {
        return fail(Status::DeviceFault);
    }

    g_controller.info = {
        g_controller.pci_device.address.bus,
        g_controller.pci_device.address.slot,
        g_controller.pci_device.address.function,
        g_controller.pci_device.vendor_id,
        g_controller.pci_device.device_id,
        g_controller.geometry.block_size,
        g_controller.geometry.block_count,
        g_controller.geometry.capacity_512_sectors,
        g_controller.geometry.read_only,
        g_controller.geometry.flush_supported,
    };
    g_controller.block = {
        &g_controller,
        g_controller.geometry.block_size,
        g_controller.geometry.block_count,
        read_blocks_callback,
        write_blocks_callback,
        flush_callback,
    };
    g_controller.ready = true;
    g_controller.status = Status::Ok;

    if (!device_registry::register_device(
            device_registry::Backend::VirtioBlock,
            0U,
            &g_controller.block,
            "VirtIO block device")) {
        g_controller.ready = false;
        return fail(Status::RegistryFailed);
    }
    return Status::Ok;
}

void shutdown() {
    if (g_controller.block.context != nullptr) {
        static_cast<void>(
            device_registry::unregister_device(&g_controller.block));
    }
    if (g_controller.common.base == nullptr &&
        !g_controller.command_owned) {
        g_controller = {};
        return;
    }
    const bool reset_complete = reset_device();
    static_cast<void>(cleanup_after_reset(
        reset_complete ? Status::Ok : Status::DeviceResetFailed,
        reset_complete));
    if (!g_controller.cleanup_blocked) g_controller = {};
}

bool initialized() { return g_controller.ready; }

const DeviceInfo* device_info() {
    return g_controller.ready ? &g_controller.info : nullptr;
}

const block::Device* block_device() {
    return g_controller.ready ? &g_controller.block : nullptr;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized:
            return "VirtIO block already initialized";
        case Status::NoDevice: return "VirtIO block PCI function not found";
        case Status::UnsupportedTransport:
            return "VirtIO modern PCI transport unavailable";
        case Status::PciCommandFailed:
            return "VirtIO block PCI command programming failed";
        case Status::MappingFailed:
            return "VirtIO block capability mapping failed";
        case Status::DeviceResetFailed:
            return "VirtIO block reset failed";
        case Status::FeatureNegotiationFailed:
            return "VirtIO block feature negotiation failed";
        case Status::MissingDeviceConfig:
            return "VirtIO block device configuration invalid";
        case Status::UnsupportedGeometry:
            return "VirtIO block geometry unsupported";
        case Status::QueueUnavailable:
            return "VirtIO block request queue unavailable";
        case Status::DmaAllocationFailed:
            return "VirtIO block queue DMA allocation failed";
        case Status::QueueConfigurationFailed:
            return "VirtIO block request queue configuration failed";
        case Status::DeviceFault:
            return "VirtIO block device fault";
        case Status::DeviceBusy:
            return "VirtIO block request already active";
        case Status::TimedOut:
            return "VirtIO block request timed out";
        case Status::IoError:
            return "VirtIO block I/O error";
        case Status::UnsupportedOperation:
            return "VirtIO block operation unsupported";
        case Status::RegistryFailed:
            return "VirtIO block registry publication failed";
        case Status::ResourceReleaseFailed:
            return "VirtIO block resource release failed";
    }
    return "unknown VirtIO block status";
}

} // namespace storage::virtio_block
