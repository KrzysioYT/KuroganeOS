#!/usr/bin/env python3
"""CI-only real VirtIO queue reset/retry qualification; no production trigger."""
from pathlib import Path
import argparse


def inject(root=Path('.'), require_msix=False, expected_routes=0):
    if expected_routes not in (0, 1, 2) or (expected_routes and not require_msix):
        raise ValueError('an expected MSI-X route count requires MSI-X qualification')
    driver = root / 'kernel/net/virtio_net.cpp'
    physical = root / 'kernel/net/physical.cpp'
    main = root / 'kernel/main.cpp'
    source = driver.read_text()
    caller = physical.read_text()
    marker = 'qualify_cleanup_runtime'
    if marker in source or marker in caller:
        raise ValueError('VirtIO cleanup qualification already injected')
    anchor = '} // namespace net::virtio_net'
    call_anchor = '    const virtio_net::Status virtio_status = virtio_net::initialize();'
    if source.count(anchor) != 1 or caller.count(call_anchor) != 1:
        raise ValueError('VirtIO cleanup qualification anchors changed')
    main_source = main.read_text() if require_msix else ''
    before_ping = '        if (net::service::ping_gateway(1) != net::Status::Ok) {'
    after_ping = '            terminal::println("[TEST] network_gateway_icmp: PASS");'
    if require_msix and (main_source.count(before_ping) != 1 or
                         main_source.count(after_ping) != 1 or
                         'virtio_irq_before' in main_source):
        raise ValueError('VirtIO MSI-X runtime qualification anchors changed')
    source = '#include "../terminal.hpp"\n#include "../memory/physical_memory.hpp"\n' + source
    source = source.replace(anchor, r'''
// Test-only transaction, injected into a clean CI checkout. All device,
// allocator, page-table and cleanup operations are the production code.
bool qualify_cleanup_runtime() {
    arch::x86_64::hardware_vectors::Lease previous[2]{};
    for (size_t cycle = 0U; cycle < 4U; ++cycle) {
        const size_t vectors_before = arch::x86_64::hardware_vectors::allocated_count();
        if (initialize() != Status::Ok || !g_receive_queue.configured ||
            !g_transmit_queue.configured ||
            (mmio_read8(g_common, 20U) & kStatusDriverOk) == 0U) {
            terminal::println("[TEST] virtio_reset_cleanup: FAIL (initialize)");
            return false;
        }
        const Queue retired[2] = {g_receive_queue, g_transmit_queue};
        const MappedRegion mappings[5] = {
            g_common, g_notify, g_device_config, g_msix_table, g_msix_pending};
        const auto retired_route = g_msix_route;
        for (size_t i = 0U; i < 2U; ++i) {
            if (previous[i].generation != 0U && arch::x86_64::hardware_vectors::owns(previous[i])) {
                terminal::println("[TEST] virtio_reset_cleanup: FAIL (stale vector reuse)");
                return false;
            }
            previous[i] = retired_route.vectors[i];
        }
        // REQUIRE_MSIX_RUNTIME
        const uint16_t original_command = g_original_command;
        terminal::println("[VIRTIO-TEST] active RX/TX queues; requesting failure cleanup");
        if (fail(Status::DeviceFault) != Status::DeviceFault || initialized() ||
            interface() != nullptr || g_cleanup_blocked || g_command_owned ||
            pci::read16(g_device, 0x04U) != original_command) {
            terminal::println("[TEST] virtio_reset_cleanup: FAIL (cleanup state)");
            return false;
        }
        bool retired_owned = false;
        for (size_t i = 0U; i < retired_route.count; ++i) {
            retired_owned |= arch::x86_64::hardware_vectors::owns(retired_route.vectors[i]);
        }
        if (arch::x86_64::hardware_vectors::allocated_count() != vectors_before || retired_owned) {
            terminal::println("[TEST] virtio_reset_cleanup: FAIL (vector ownership)");
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
    if require_msix:
        source = source.replace('        // REQUIRE_MSIX_RUNTIME', r'''
        constexpr size_t expected_routes = EXPECTED_MSIX_ROUTES;
        if (!retired_route.programmed.active ||
            interrupt_diagnostics().status != InterruptStatus::MsiXEnabled ||
            retired_route.count == 0U || retired_route.count > 2U ||
            (expected_routes != 0U && retired_route.count != expected_routes) ||
            arch::x86_64::hardware_vectors::allocated_count() != vectors_before + retired_route.count) {
            terminal::write("[VIRTIO-TEST] interrupt status: ");
            terminal::println(interrupt_status_name(interrupt_diagnostics().status));
            terminal::println("[TEST] virtio_msix_route: FAIL");
            return false;
        }
        for (size_t i = 0U; i < retired_route.count; ++i) {
            if (!arch::x86_64::hardware_vectors::owns(retired_route.vectors[i])) {
                terminal::println("[TEST] virtio_msix_route: FAIL (lease ownership)");
                return false;
            }
        }
        for (uint16_t queue = 0U; queue < 2U; ++queue) {
            mmio_write16(g_common, 22U, queue);
            const uint16_t entry = retired_route.count == 2U ? queue : 0U;
            if (mmio_read16(g_common, 26U) != entry || retired[queue].available[0] != 0U) {
                terminal::println("[TEST] virtio_msix_route: FAIL (queue vector)");
                return false;
            }
        }
''')
        source = source.replace('EXPECTED_MSIX_ROUTES', str(expected_routes) + 'U')
        source = source.replace('    terminal::println("[TEST] virtio_reset_cleanup: PASS");',
            '    terminal::println("[TEST] virtio_msix_route_cleanup: PASS");\n'
            '    terminal::println("[TEST] virtio_reset_cleanup: PASS");')
        main_source = '#include "net/virtio_net.hpp"\n' + main_source
        main_source = main_source.replace(before_ping,
            '        const auto virtio_irq_before = net::virtio_net::interrupt_diagnostics();\n' + before_ping)
        main_source = main_source.replace(after_ping, after_ping + r'''
            const auto virtio_irq_after = net::virtio_net::interrupt_diagnostics();
            constexpr uint8_t expected_routes = EXPECTED_MSIX_ROUTES;
            if (virtio_irq_before.status != net::virtio_net::InterruptStatus::MsiXEnabled ||
                virtio_irq_after.status != net::virtio_net::InterruptStatus::MsiXEnabled ||
                virtio_irq_after.vector == 0U ||
                virtio_irq_after.route_count != virtio_irq_before.route_count ||
                (expected_routes != 0U && virtio_irq_after.route_count != expected_routes) ||
                virtio_irq_after.delivered <= virtio_irq_before.delivered) {
                terminal::write("[VIRTIO-TEST] before IRQ=");
                terminal::write_u64(virtio_irq_before.delivered);
                terminal::write(" after IRQ=");
                terminal::write_u64(virtio_irq_after.delivered);
                terminal::println("");
                terminal::println("[TEST] virtio_msix_delivery: FAIL");
                boot_failure("NET", "VirtIO queue MSI-X did not progress during gateway traffic");
            }
            if (virtio_irq_after.route_count == 2U) {
                if (virtio_irq_after.receive_vector == virtio_irq_after.transmit_vector ||
                    virtio_irq_after.receive_delivered <= virtio_irq_before.receive_delivered ||
                    virtio_irq_after.transmit_delivered <= virtio_irq_before.transmit_delivered) {
                    terminal::println("[TEST] virtio_msix_delivery: FAIL (independent RX/TX)");
                    boot_failure("NET", "VirtIO RX and TX must each deliver their own IRQ");
                }
                terminal::write("[VIRTIO-TEST] RX vector=");
                terminal::write_u64(virtio_irq_after.receive_vector);
                terminal::write(" delta=");
                terminal::write_u64(virtio_irq_after.receive_delivered - virtio_irq_before.receive_delivered);
                terminal::write(" TX vector=");
                terminal::write_u64(virtio_irq_after.transmit_vector);
                terminal::write(" delta=");
                terminal::write_u64(virtio_irq_after.transmit_delivered - virtio_irq_before.transmit_delivered);
                terminal::println("");
                terminal::println("[TEST] virtio_msix_split_delivery: PASS");
            } else {
                if (virtio_irq_after.route_count != 1U ||
                    virtio_irq_after.receive_vector != virtio_irq_after.transmit_vector ||
                    virtio_irq_after.receive_delivered != 0U || virtio_irq_after.transmit_delivered != 0U) {
                    terminal::println("[TEST] virtio_msix_delivery: FAIL (shared source metadata)");
                    boot_failure("NET", "VirtIO shared IRQ cannot attribute a source");
                }
                terminal::println("[TEST] virtio_msix_shared_delivery: PASS");
            }
            terminal::write("[VIRTIO-TEST] gateway MSI-X deliveries=");
            terminal::write_u64(virtio_irq_after.delivered - virtio_irq_before.delivered);
            terminal::println("");
            terminal::println("[TEST] virtio_msix_delivery: PASS");
''')
        main_source = main_source.replace('EXPECTED_MSIX_ROUTES', str(expected_routes) + 'U')
    # Validate both before writing either file.
    driver.write_text(source)
    physical.write_text(caller)
    if require_msix:
        main.write_text(main_source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--require-msix', action='store_true')
    parser.add_argument('--msix-routes', type=int, choices=(1, 2), default=0)
    arguments = parser.parse_args()
    inject(require_msix=arguments.require_msix, expected_routes=arguments.msix_routes)
    print('[qualification] real VirtIO reset, DMA release and retry injected')
