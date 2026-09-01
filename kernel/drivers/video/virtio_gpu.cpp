#include "virtio_gpu.hpp"

#include "../pci.hpp"
#include "../../memory/kernel_virtual_memory.hpp"
#include "../../memory/virtual_memory.hpp"
#include "../../storage/dma.hpp"

#include <stddef.h>

namespace drivers::video::virtio_gpu {
namespace {

constexpr uint16_t kVirtioVendor = UINT16_C(0x1AF4);
constexpr uint16_t kVirtioGpuTransitionalDevice = UINT16_C(0x1010);
constexpr uint16_t kVirtioGpuModernDevice = UINT16_C(0x1050);
constexpr uint8_t kVendorCapabilityId = UINT8_C(0x09);
constexpr uint8_t kCommonConfigType = 1U;
constexpr uint8_t kNotifyConfigType = 2U;
constexpr uint8_t kCapabilityIterations = 48U;
constexpr uint8_t kMaxBarIndex = 5U;
constexpr uint16_t kNoMsixVector = UINT16_C(0xFFFF);
constexpr uint16_t kVirtqDescNext = UINT16_C(1);
constexpr uint16_t kVirtqDescWrite = UINT16_C(2);
constexpr uint16_t kAvailNoInterrupt = UINT16_C(1);
constexpr uint8_t kStatusAcknowledge = UINT8_C(1);
constexpr uint8_t kStatusDriver = UINT8_C(2);
constexpr uint8_t kStatusDriverOk = UINT8_C(4);
constexpr uint8_t kStatusFeaturesOk = UINT8_C(8);
constexpr uint8_t kStatusFailed = UINT8_C(128);
constexpr uint32_t kVersion1FeatureHigh = UINT32_C(1);
constexpr uint32_t kGetDisplayInfo = UINT32_C(0x0100);
constexpr uint32_t kResponseOkDisplayInfo = UINT32_C(0x1101);
constexpr uint32_t kMaximumScanouts = 16U;
constexpr size_t kQueueCapacity = 8U;
constexpr size_t kCommandSpinLimit = 8000000U;
constexpr uintptr_t kCommonVirtualBase = UINT64_C(0xFFFFB30000000000);
constexpr uintptr_t kNotifyVirtualBase = UINT64_C(0xFFFFB30000010000);

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

struct [[gnu::packed]] ControlHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t context_id;
    uint32_t padding;
};

struct [[gnu::packed]] Rectangle {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct [[gnu::packed]] ScanoutMode {
    Rectangle rectangle;
    uint32_t enabled;
    uint32_t flags;
};

struct [[gnu::packed]] DisplayInfoResponse {
    ControlHeader header;
    ScanoutMode modes[kMaximumScanouts];
};

static_assert(sizeof(Descriptor) == 16U, "VirtIO descriptor ABI mismatch");
static_assert(sizeof(UsedElement) == 8U, "VirtIO used element ABI mismatch");
static_assert(sizeof(ControlHeader) == 24U, "VirtIO-GPU control header ABI mismatch");
static_assert(sizeof(ScanoutMode) == 24U, "VirtIO-GPU scanout ABI mismatch");
static_assert(
    sizeof(DisplayInfoResponse) <= memory::virtual_memory::PAGE_SIZE,
    "VirtIO-GPU display response must fit one DMA page");

struct Queue {
    uint16_t index;
    uint16_t size;
    storage::dma::Page descriptor_page;
    storage::dma::Page available_page;
    storage::dma::Page used_page;
    storage::dma::Page request_page;
    storage::dma::Page response_page;
    Descriptor* descriptors;
    volatile uint16_t* available;
    volatile uint16_t* used_header;
    volatile UsedElement* used_elements;
    volatile uint16_t* notify;
    uint16_t available_index;
    uint16_t last_used_index;
    bool configured;
};

pci::Device g_device{};
bool g_detected = false;
bool g_initialized = false;
Status g_status = Status::NotInitialized;
DisplayInfo g_display{};
MappedRegion g_common{};
MappedRegion g_notify{};
Queue g_control_queue{};

uint8_t pci_read8(const pci::Device& device, uint8_t offset) {
    const uint8_t aligned = static_cast<uint8_t>(offset & UINT8_C(0xFC));
    const uint32_t value = pci::read32(device, aligned);
    const unsigned shift = static_cast<unsigned>((offset & UINT8_C(3)) * 8U);
    return static_cast<uint8_t>((value >> shift) & UINT32_C(0xFF));
}

uint8_t mmio_read8(const MappedRegion& region, size_t offset) {
    if (region.base == nullptr || offset >= region.length) return 0U;
    return *(region.base + offset);
}

uint16_t mmio_read16(const MappedRegion& region, size_t offset) {
    if (region.base == nullptr || offset + sizeof(uint16_t) > region.length) return 0U;
    return *reinterpret_cast<volatile uint16_t*>(region.base + offset);
}

uint32_t mmio_read32(const MappedRegion& region, size_t offset) {
    if (region.base == nullptr || offset + sizeof(uint32_t) > region.length) return 0U;
    return *reinterpret_cast<volatile uint32_t*>(region.base + offset);
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
    VirtioCapability* notify) {
    if (common == nullptr || notify == nullptr) return false;
    *common = {};
    *notify = {};

    uint8_t pointer = static_cast<uint8_t>(
        pci_read8(device, UINT8_C(0x34)) & UINT8_C(0xFC));
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
                address_space, target, &existing) !=
                memory::virtual_memory::Status::NotMapped ||
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
    if (queue->request_page.allocated) {
        static_cast<void>(storage::dma::release_page(&queue->request_page));
    }
    if (queue->response_page.allocated) {
        static_cast<void>(storage::dma::release_page(&queue->response_page));
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
    while (size > maximum && size > 2U) size = static_cast<uint16_t>(size >> 1U);
    return size >= 2U && size <= maximum ? size : 0U;
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

bool configure_control_queue(
    const VirtioCapability& notify_capability) {
    Queue& queue = g_control_queue;
    if (g_common.length < 56U || g_notify.base == nullptr ||
        notify_capability.notify_multiplier == 0U) {
        return false;
    }

    mmio_write16(g_common, 22U, 0U);
    const uint16_t maximum = mmio_read16(g_common, 24U);
    const uint16_t size = choose_queue_size(maximum);
    if (size == 0U || mmio_read16(g_common, 28U) != 0U) return false;

    if (storage::dma::allocate_page(true, &queue.descriptor_page) !=
            storage::dma::Status::Ok ||
        storage::dma::allocate_page(true, &queue.available_page) !=
            storage::dma::Status::Ok ||
        storage::dma::allocate_page(true, &queue.used_page) !=
            storage::dma::Status::Ok ||
        storage::dma::allocate_page(true, &queue.request_page) !=
            storage::dma::Status::Ok ||
        storage::dma::allocate_page(true, &queue.response_page) !=
            storage::dma::Status::Ok) {
        release_queue(&queue);
        return false;
    }

    clear_bytes(queue.descriptor_page.virtual_address, memory::virtual_memory::PAGE_SIZE);
    clear_bytes(queue.available_page.virtual_address, memory::virtual_memory::PAGE_SIZE);
    clear_bytes(queue.used_page.virtual_address, memory::virtual_memory::PAGE_SIZE);
    clear_bytes(queue.request_page.virtual_address, memory::virtual_memory::PAGE_SIZE);
    clear_bytes(queue.response_page.virtual_address, memory::virtual_memory::PAGE_SIZE);

    queue.index = 0U;
    queue.size = size;
    queue.descriptors = static_cast<Descriptor*>(queue.descriptor_page.virtual_address);
    queue.available = static_cast<volatile uint16_t*>(queue.available_page.virtual_address);
    queue.used_header = static_cast<volatile uint16_t*>(queue.used_page.virtual_address);
    queue.used_elements = reinterpret_cast<volatile UsedElement*>(
        static_cast<uint8_t*>(queue.used_page.virtual_address) + 4U);
    queue.available[0] = kAvailNoInterrupt;
    queue.available[1] = 0U;
    queue.used_header[0] = 0U;
    queue.used_header[1] = 0U;

    mmio_write16(g_common, 24U, size);
    mmio_write16(g_common, 26U, kNoMsixVector);
    if (!write_queue_address(32U, queue.descriptor_page.physical_address) ||
        !write_queue_address(40U, queue.available_page.physical_address) ||
        !write_queue_address(48U, queue.used_page.physical_address)) {
        release_queue(&queue);
        return false;
    }

    const uint16_t notify_offset = mmio_read16(g_common, 30U);
    const uint64_t byte_offset =
        static_cast<uint64_t>(notify_offset) * notify_capability.notify_multiplier;
    if (byte_offset > SIZE_MAX ||
        static_cast<size_t>(byte_offset) + sizeof(uint16_t) > g_notify.length) {
        release_queue(&queue);
        return false;
    }
    queue.notify = reinterpret_cast<volatile uint16_t*>(
        g_notify.base + static_cast<size_t>(byte_offset));

    memory_barrier();
    mmio_write16(g_common, 28U, 1U);
    if (mmio_read16(g_common, 28U) != 1U) {
        release_queue(&queue);
        return false;
    }
    queue.configured = true;
    return true;
}

void notify_control_queue() {
    memory_barrier();
    if (g_control_queue.notify != nullptr) {
        *g_control_queue.notify = g_control_queue.index;
    }
}

bool submit_display_info() {
    Queue& queue = g_control_queue;
    if (!queue.configured || queue.size < 2U) return false;

    clear_bytes(queue.request_page.virtual_address, memory::virtual_memory::PAGE_SIZE);
    clear_bytes(queue.response_page.virtual_address, memory::virtual_memory::PAGE_SIZE);
    auto* request = static_cast<ControlHeader*>(queue.request_page.virtual_address);
    request->type = kGetDisplayInfo;

    queue.descriptors[0].address = queue.request_page.physical_address;
    queue.descriptors[0].length = sizeof(ControlHeader);
    queue.descriptors[0].flags = kVirtqDescNext;
    queue.descriptors[0].next = 1U;
    queue.descriptors[1].address = queue.response_page.physical_address;
    queue.descriptors[1].length = sizeof(DisplayInfoResponse);
    queue.descriptors[1].flags = kVirtqDescWrite;
    queue.descriptors[1].next = 0U;

    const uint16_t slot = static_cast<uint16_t>(queue.available_index % queue.size);
    queue.available[2U + slot] = 0U;
    memory_barrier();
    ++queue.available_index;
    queue.available[1] = queue.available_index;
    notify_control_queue();

    for (size_t spin = 0U; spin < kCommandSpinLimit; ++spin) {
        memory_barrier();
        if (queue.used_header[1] != queue.last_used_index) {
            const uint16_t used_slot = static_cast<uint16_t>(
                queue.last_used_index % queue.size);
            const uint32_t descriptor_id = queue.used_elements[used_slot].id;
            ++queue.last_used_index;
            return descriptor_id == 0U;
        }
        __asm__ volatile("pause");
    }
    return false;
}

bool parse_display_info() {
    const auto* response = static_cast<const DisplayInfoResponse*>(
        g_control_queue.response_page.virtual_address);
    if (response == nullptr || response->header.type != kResponseOkDisplayInfo) {
        return false;
    }

    uint32_t enabled = 0U;
    uint32_t first_width = 0U;
    uint32_t first_height = 0U;
    uint32_t first_id = 0U;
    for (uint32_t index = 0U; index < kMaximumScanouts; ++index) {
        const ScanoutMode& mode = response->modes[index];
        if (mode.enabled == 0U || mode.rectangle.width == 0U ||
            mode.rectangle.height == 0U) {
            continue;
        }
        if (enabled == 0U) {
            first_width = mode.rectangle.width;
            first_height = mode.rectangle.height;
            first_id = index;
        }
        ++enabled;
    }
    if (enabled == 0U) return false;

    g_display.width = first_width;
    g_display.height = first_height;
    g_display.scanout_id = first_id;
    g_display.enabled_scanouts = enabled;
    return true;
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
    release_queue(&g_control_queue);
    g_initialized = false;
    g_display.initialized = false;
    g_status = status;
    return status;
}

} // namespace

Status initialize() {
    if (g_initialized) return Status::AlreadyInitialized;

    g_status = Status::NotInitialized;
    g_detected = false;
    g_display = {};
    g_common = {};
    g_notify = {};
    g_control_queue = {};

    const pci::Device* found = pci::find(kVirtioVendor, kVirtioGpuModernDevice);
    if (found == nullptr) {
        found = pci::find(kVirtioVendor, kVirtioGpuTransitionalDevice);
    }
    if (found == nullptr) {
        g_status = Status::NoDevice;
        return g_status;
    }

    g_device = *found;
    g_detected = true;
    g_display.detected = true;
    g_display.vendor_id = g_device.vendor_id;
    g_display.device_id = g_device.device_id;

    VirtioCapability common_capability{};
    VirtioCapability notify_capability{};
    if (!scan_capabilities(g_device, &common_capability, &notify_capability)) {
        g_status = Status::UnsupportedTransport;
        return g_status;
    }

    const uint32_t command = pci::read32(g_device, UINT8_C(0x04));
    pci::write32(
        g_device,
        UINT8_C(0x04),
        (command & UINT32_C(0x0000FFFF)) | UINT32_C(0x00000006));

    if (!map_capability(
            g_device, common_capability, kCommonVirtualBase, &g_common) ||
        !map_capability(
            g_device, notify_capability, kNotifyVirtualBase, &g_notify)) {
        return fail(Status::MappingFailed);
    }
    if (g_common.length < 56U) return fail(Status::UnsupportedTransport);

    mmio_write8(g_common, 20U, 0U);
    memory_barrier();
    if (mmio_read8(g_common, 20U) != 0U) return fail(Status::DeviceFault);

    mmio_write8(g_common, 20U, kStatusAcknowledge);
    mmio_write8(
        g_common,
        20U,
        static_cast<uint8_t>(kStatusAcknowledge | kStatusDriver));

    mmio_write32(g_common, 0U, 1U);
    const uint32_t features_high = mmio_read32(g_common, 4U);
    if ((features_high & kVersion1FeatureHigh) == 0U) {
        return fail(Status::FeatureNegotiationFailed);
    }

    mmio_write32(g_common, 8U, 0U);
    mmio_write32(g_common, 12U, 0U);
    mmio_write32(g_common, 8U, 1U);
    mmio_write32(g_common, 12U, kVersion1FeatureHigh);

    uint8_t device_status = static_cast<uint8_t>(
        kStatusAcknowledge | kStatusDriver | kStatusFeaturesOk);
    mmio_write8(g_common, 20U, device_status);
    if ((mmio_read8(g_common, 20U) & kStatusFeaturesOk) == 0U) {
        return fail(Status::FeatureNegotiationFailed);
    }

    if (mmio_read16(g_common, 18U) < 1U) {
        return fail(Status::QueueUnavailable);
    }
    if (!configure_control_queue(notify_capability)) {
        return fail(Status::QueueConfigurationFailed);
    }

    device_status = static_cast<uint8_t>(device_status | kStatusDriverOk);
    mmio_write8(g_common, 20U, device_status);
    memory_barrier();
    if ((mmio_read8(g_common, 20U) & kStatusDriverOk) == 0U) {
        return fail(Status::DeviceFault);
    }

    if (!submit_display_info()) return fail(Status::CommandTimeout);
    if (!parse_display_info()) return fail(Status::InvalidResponse);

    g_initialized = true;
    g_display.initialized = true;
    g_status = Status::Ok;
    return g_status;
}

bool detected() { return g_detected; }
bool initialized() { return g_initialized; }
Status last_status() { return g_status; }
const DisplayInfo& display_info() { return g_display; }

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "VirtIO-GPU not initialized";
        case Status::AlreadyInitialized: return "VirtIO-GPU already initialized";
        case Status::NoDevice: return "VirtIO-GPU PCI function not found";
        case Status::UnsupportedTransport:
            return "VirtIO-GPU modern PCI capabilities missing";
        case Status::MappingFailed: return "VirtIO-GPU MMIO capability mapping failed";
        case Status::FeatureNegotiationFailed:
            return "VirtIO-GPU feature negotiation failed";
        case Status::QueueUnavailable: return "VirtIO-GPU control queue unavailable";
        case Status::QueueAllocationFailed: return "VirtIO-GPU DMA allocation failed";
        case Status::QueueConfigurationFailed:
            return "VirtIO-GPU control queue configuration failed";
        case Status::CommandTimeout: return "VirtIO-GPU command timed out";
        case Status::InvalidResponse: return "VirtIO-GPU returned invalid display info";
        case Status::DeviceFault: return "VirtIO-GPU device fault";
    }
    return "unknown VirtIO-GPU status";
}

} // namespace drivers::video::virtio_gpu
