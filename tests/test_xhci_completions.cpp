#include <cassert>
#include <cstdio>

#include "../kernel/drivers/usb/xhci.cpp"

namespace {
size_t submitted = 0U;
drivers::keyboard::KeyEvent delivered[4]{};
}
namespace input {
bool submit_key(const drivers::keyboard::KeyEvent& event) {
    assert(submitted < 4U);
    delivered[submitted++] = event;
    return true;
}
}
namespace log {
void write(Level, const char*, const char*) {}
}
namespace terminal {
void println(const char*) {}
}

int main() {
    using namespace drivers::usb::xhci;
    alignas(4096) Trb transfers[RING_TRB_COUNT]{};
    alignas(4096) Trb events[RING_TRB_COUNT]{};
    alignas(4096) uint8_t report[4096]{};
    alignas(8) uint8_t runtime[0x40]{};
    uint32_t doorbells[256]{};
    g_controller = {};
    g_controller.initialized = true;
    g_controller.slot_id = 1U;
    g_controller.interrupt_dci = 3U;
    g_controller.interrupt_packet_size = 8U;
    g_controller.interrupt_ring.page = {transfers, 0x10000U, true};
    g_controller.interrupt_ring.cycle = true;
    g_controller.event_ring_page = {events, 0x20000U, true};
    g_controller.event_cycle = true;
    g_controller.data_page = {report, 0x30000U, true};
    g_controller.runtime = runtime;
    g_controller.doorbells = doorbells;
    const uint32_t control = static_cast<uint32_t>(TRB_TRANSFER_EVENT) << 10U |
        1U << 24U | 3U << 16U | 1U;
    const uint32_t success = static_cast<uint32_t>(COMPLETION_SUCCESS) << 24U;
    auto complete = [&](uint64_t pointer, uint32_t flags = 0U) {
        events[g_controller.event_dequeue] = {pointer, success, control | flags};
        assert(poll(1U) == 1U);
    };

    assert(queue_keyboard_report(g_controller));
    assert(g_controller.interrupt_ring.enqueue == 1U);
    report[2] = 4U; // Real decoder usage for A; not yet completed by hardware.
    complete(0x10010U); // A different descriptor must not retire this report.
    assert(submitted == 0U && g_controller.reports == 0U);
    assert(g_controller.report_queued && g_controller.interrupt_ring.enqueue == 1U);
    assert(report[2] == 4U);

    complete(0x10000U, 1U << 2U); // Event Data is not a TRB-pointer completion.
    complete(0x10001U); // Misaligned pointer must not be rounded into a match.
    assert(submitted == 0U && g_controller.interrupt_ring.enqueue == 1U);
    complete(0x10000U);
    assert(submitted == 1U && delivered[0].pressed);
    assert(g_controller.reports == 1U && g_controller.interrupt_ring.enqueue == 2U);

    complete(0x10000U); // Duplicate completion must not release A prematurely.
    assert(submitted == 1U && g_controller.reports == 1U);
    assert(g_controller.interrupt_ring.enqueue == 2U);
    complete(0x10010U); // The current empty report releases A exactly once.
    assert(submitted == 2U && !delivered[1].pressed);
    assert(delivered[1].key == delivered[0].key);
    assert(g_controller.reports == 2U && g_controller.interrupt_ring.enqueue == 3U);

    g_controller.report_queued = false;
    complete(0x10020U); // No outstanding transfer: consume event, no mutation.
    assert(submitted == 2U && g_controller.reports == 2U);
    assert(!g_controller.report_queued && g_controller.interrupt_ring.enqueue == 3U);
    std::puts("xHCI transfer completion ownership regression: PASS");
    return 0;
}
