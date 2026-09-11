#!/usr/bin/env python3
"""CI-only real VirtIO queue reset/retry qualification; no production trigger."""
from pathlib import Path


def inject(root=Path('.')):
    driver = root / 'kernel/net/virtio_net.cpp'
    physical = root / 'kernel/net/physical.cpp'
    source = driver.read_text()
    caller = physical.read_text()
    marker = 'qualify_cleanup_runtime'
    if marker in source or marker in caller:
        raise ValueError('VirtIO cleanup qualification already injected')
    anchor = '} // namespace net::virtio_net'
    call_anchor = '    const virtio_net::Status virtio_status = virtio_net::initialize();'
    if source.count(anchor) != 1 or caller.count(call_anchor) != 1:
        raise ValueError('VirtIO cleanup qualification anchors changed')
    source = '#include "../terminal.hpp"\n#include "../memory/physical_memory.hpp"\n' + source
    source = source.replace(anchor, r'''
// Test-only transaction, injected into a clean CI checkout. All device,
// allocator, page-table and cleanup operations are the production code.
bool qualify_cleanup_runtime() {
    for (size_t cycle = 0U; cycle < 4U; ++cycle) {
        if (initialize() != Status::Ok || !g_receive_queue.configured ||
            !g_transmit_queue.configured ||
            (mmio_read8(g_common, 20U) & kStatusDriverOk) == 0U) {
            terminal::println("[TEST] virtio_reset_cleanup: FAIL (initialize)");
            return false;
        }
        const Queue retired[2] = {g_receive_queue, g_transmit_queue};
        const MappedRegion mappings[3] = {g_common, g_notify, g_device_config};
        const uint16_t original_command = g_original_command;
        terminal::println("[VIRTIO-TEST] active RX/TX queues; requesting failure cleanup");
        if (fail(Status::DeviceFault) != Status::DeviceFault || initialized() ||
            interface() != nullptr || g_cleanup_blocked || g_command_owned ||
            pci::read16(g_device, 0x04U) != original_command) {
            terminal::println("[TEST] virtio_reset_cleanup: FAIL (cleanup state)");
            return false;
        }
        size_t released = 0U;
        for (const Queue& queue : retired) {
            const storage::dma::Page metadata[3] = {
                queue.descriptor_page, queue.available_page, queue.used_page};
            for (const auto& page : metadata) {
                if (!page.allocated || memory::is_frame_allocated(page.virtual_address)) {
                    terminal::println("[TEST] virtio_reset_cleanup: FAIL (metadata DMA)");
                    return false;
                }
                ++released;
            }
            for (uint16_t index = 0U; index < queue.size; ++index) {
                const auto& page = queue.buffers[index];
                if (!page.allocated || memory::is_frame_allocated(page.virtual_address)) {
                    terminal::println("[TEST] virtio_reset_cleanup: FAIL (packet DMA)");
                    return false;
                }
                ++released;
            }
        }
        for (const auto& region : mappings) {
            for (size_t page = 0U; page < region.mapped_pages; ++page) {
                memory::virtual_memory::Mapping mapping{};
                if (memory::virtual_memory::query_page(
                        memory::kernel_virtual_memory::address_space(),
                        region.virtual_base + page * memory::virtual_memory::PAGE_SIZE,
                        &mapping) != memory::virtual_memory::Status::NotMapped) {
                    terminal::println("[TEST] virtio_reset_cleanup: FAIL (MMIO mapping)");
                    return false;
                }
            }
        }
        terminal::write("[VIRTIO-TEST] released DMA frames=");
        terminal::write_u64(released);
        terminal::println("; MMIO removed; PCI command restored");
    }
    terminal::println("[TEST] virtio_reset_cleanup: PASS");
    if (initialize() != Status::Ok || interface() == nullptr) {
        terminal::println("[TEST] virtio_reset_retry: FAIL");
        return false;
    }
    terminal::println("[TEST] virtio_reset_retry: PASS");
    return true;
}
''' + anchor)
    caller = 'namespace net::virtio_net { bool qualify_cleanup_runtime(); }\n' + caller
    caller = caller.replace(call_anchor, '''    if (!virtio_net::qualify_cleanup_runtime()) {
        g_status = Status::DeviceUnavailable;
        return g_status;
    }
''' + call_anchor)
    # Validate both before writing either file.
    driver.write_text(source)
    physical.write_text(caller)


if __name__ == '__main__':
    inject()
    print('[qualification] real VirtIO reset, DMA release and retry injected')
