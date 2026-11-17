#include "../kernel/net/rtl81xx.cpp"

#include <cassert>
#include <cstdio>

namespace {
storage::dma::Page* failed_page = nullptr;
uint64_t failed_mapping = 0U;
size_t released = 0U;
size_t unmapped = 0U;
bool missing_space = false;
memory::virtual_memory::AddressSpace test_space{};
uint8_t mmio[4096]{};
}

namespace storage::dma {
Status release_page(Page* page) {
    assert(page != nullptr && page->allocated);
    if (page == failed_page) return Status::ReleaseFailed;
    ++released;
    *page = {};
    return Status::Ok;
}
}

namespace memory::kernel_virtual_memory {
virtual_memory::AddressSpace* address_space() {
    return missing_space ? nullptr : &test_space;
}
}

namespace memory::virtual_memory {
Status unmap_page(AddressSpace* space, uint64_t address, Mapping*) {
    assert(space == &test_space);
    if (address == failed_mapping) return Status::InvalidArgument;
    ++unmapped;
    return Status::Ok;
}
}

int main() {
    using namespace net::rtl81xx;
    constexpr size_t page_count = DESCRIPTOR_COUNT * 2U + 2U;
    auto prepare = [] {
        failed_page = nullptr;
        failed_mapping = 0U;
        released = unmapped = 0U;
        missing_space = false;
        g_device = {};
        g_device.registers = mmio;
        g_device.mapped_pages = 2U;
        g_device.rx_ring_page = {mmio, 0x1000U, true};
        g_device.tx_ring_page = {mmio, 0x2000U, true};
        for (size_t i = 0U; i < DESCRIPTOR_COUNT; ++i) {
            g_device.rx_buffers[i] = {mmio, 0x3000U + i * 0x2000U, true};
            g_device.tx_buffers[i] = {mmio, 0x4000U + i * 0x2000U, true};
        }
    };

    assert(!cleanup_before_start(nullptr));
    prepare();
    g_device.started = true;
    assert(!cleanup_before_start(&g_device));
    assert(released == 0U && unmapped == 0U);

    // Fail each allocation's release in turn, then retry without freeing
    // successfully released pages again or losing the failed page's owner.
    for (size_t index = 0U; index < page_count; ++index) {
        prepare();
        if (index < DESCRIPTOR_COUNT * 2U) {
            failed_page = index % 2U == 0U
                ? &g_device.rx_buffers[index / 2U]
                : &g_device.tx_buffers[index / 2U];
        } else {
            failed_page = index == DESCRIPTOR_COUNT * 2U
                ? &g_device.rx_ring_page : &g_device.tx_ring_page;
        }
        const uint64_t physical = failed_page->physical_address;
        assert(fail_before_start(Status::DmaAllocationFailed) == Status::ResourceReleaseFailed);
        assert(last_status() == Status::ResourceReleaseFailed);
        assert(failed_page->allocated && failed_page->physical_address == physical);
        assert(released == index && unmapped == 0U);
        assert(g_device.registers == mmio && g_device.mapped_pages == 2U);
        failed_page = nullptr;
        assert(cleanup_before_start(&g_device));
        assert(released == page_count && unmapped == 2U);
        assert(g_device.registers == nullptr && g_device.mapped_pages == 0U);
        assert(cleanup_before_start(&g_device));
        assert(released == page_count && unmapped == 2U);
    }

    for (size_t remaining = 1U; remaining <= 2U; ++remaining) {
        prepare();
        failed_mapping = MMIO_VIRTUAL_BASE +
            (remaining - 1U) * memory::virtual_memory::PAGE_SIZE;
        assert(!cleanup_before_start(&g_device));
        assert(released == page_count && unmapped == 2U - remaining);
        assert(g_device.mapped_pages == remaining && g_device.registers == mmio);
        failed_mapping = 0U;
        assert(cleanup_before_start(&g_device));
        assert(released == page_count && unmapped == 2U);
    }

    prepare();
    missing_space = true;
    assert(!cleanup_before_start(&g_device));
    assert(g_device.mapped_pages == 2U && g_device.registers == mmio);
    assert(released == page_count && unmapped == 0U);
    missing_space = false;
    assert(cleanup_before_start(&g_device));
    assert(released == page_count && unmapped == 2U);
    prepare();
    assert(fail_before_start(Status::InvalidMac) == Status::InvalidMac);
    assert(last_status() == Status::InvalidMac);
    std::puts("RTL81xx stopped-device cleanup ownership, failure and retry: PASS");
}
