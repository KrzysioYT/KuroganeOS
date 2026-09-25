#include <cassert>
#include <cstdio>

#include "../kernel/drivers/usb/xhci.cpp"

namespace {
size_t submitted = 0U;
drivers::keyboard::KeyEvent delivered[24]{};
size_t capacity = 24U;
}
namespace input {
bool submit_key(const drivers::keyboard::KeyEvent& event) {
    if (submitted == capacity) return false;
    assert(submitted < 24U);
    delivered[submitted++] = event;
    return true;
}
bool submit_mouse(const drivers::mouse::Sample&) {
    return true;
}
}
namespace log {
void write(Level, const char*, const char*) {}
void write_u64(Level, const char*, const char*, uint64_t) {}
}
namespace pci {
uint16_t read16(Address, uint8_t) { return 0U; }
void write16(Address, uint8_t, uint16_t) {}
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
    g_controller.hid_kind = HidKind::Keyboard;
    g_controller.hid_lifecycle = HidLifecycle::Active;
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

    assert(queue_hid_report(g_controller));
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

    assert(queue_hid_report(g_controller));
    report[0] = 2U; // Shift+A creates two ordered events.
    report[2] = 4U;
    capacity = submitted + 1U;
    complete(0x10030U);
    assert(submitted == 3U && delivered[2].key == drivers::keyboard::KeyCode::LeftShift);
    assert(g_controller.pending_key_count == 2U && g_controller.pending_key_index == 1U);
    assert(!g_controller.report_queued && g_controller.interrupt_ring.enqueue == 4U);
    for (size_t retry = 0U; retry < 4U; ++retry) assert(poll(1U) == 0U);
    assert(submitted == 3U && g_controller.interrupt_ring.enqueue == 4U);
    capacity = 24U;
    assert(poll(1U) == 0U);
    assert(submitted == 4U && delivered[3].pressed && delivered[3].shift);
    assert(g_controller.pending_key_count == 0U && g_controller.report_queued);
    assert(g_controller.interrupt_ring.enqueue == 5U);
    // Block both releases, then verify retry publishes each exactly once.
    capacity = submitted;
    complete(0x10040U);
    assert(submitted == 4U && !g_controller.report_queued);
    assert(g_controller.pending_key_count == 2U);
    capacity = 24U;
    assert(poll(1U) == 0U);
    assert(submitted == 6U && !delivered[4].pressed && !delivered[5].pressed);
    assert(g_controller.interrupt_ring.enqueue == 6U);

    // Disconnect while Shift+A are down and input can accept only one release.
    namespace device = drivers::device;
    assert(device::initialize() == KStatus::Ok);
    g_controller.parent_device = device::INVALID_DEVICE_ID;
    g_controller.owner_driver = 7U;
    assert(register_keyboard(g_controller));
    const auto old_handle = device::handle_for(g_controller.keyboard_device);
    alignas(4) uint8_t operational[OP_PORTS + PORT_STRIDE]{};
    alignas(4096) Trb commands[RING_TRB_COUNT]{};
    alignas(4096) uint64_t dcbaa[512]{};
    g_controller.operational = operational;
    g_controller.port_id = 1U;
    g_controller.maximum_ports = 1U;
    g_controller.command_ring.page = {commands, 0x40000U, true};
    g_controller.command_ring.cycle = true;
    g_controller.dcbaa_page = {dcbaa, 0x50000U, true};
    dcbaa[1] = 0x60000U;
    write32(operational, OP_PORTS, PORT_CONNECTED);
    report[0] = 2U;
    report[2] = 4U;
    complete(0x10050U);
    assert(submitted == 8U && keyboard_ready());
    capacity = submitted + 1U;
    write32(operational, OP_PORTS, 0U);
    assert(poll(1U) == 0U);
    assert(!keyboard_ready() && submitted == 9U);
    assert(g_controller.hid_lifecycle == HidLifecycle::ReleaseInput);
    assert(g_controller.command_ring.enqueue == 0U && dcbaa[1] == 0x60000U);
    assert(device::resolve(old_handle) != nullptr);
    capacity = 24U;
    events[g_controller.event_dequeue] = {0x40000U, success,
        static_cast<uint32_t>(TRB_COMMAND_COMPLETION) << 10U | 1U};
    assert(poll(1U) == 0U);
    assert(submitted == 10U && !delivered[8].pressed && !delivered[9].pressed);
    assert(g_controller.hid_lifecycle == HidLifecycle::WaitingForDevice);
    assert(g_controller.command_ring.enqueue == 1U && dcbaa[1] == 0U);
    assert(trb_type(commands[0]) == TRB_DISABLE_SLOT);
    assert((commands[0].control >> 24U) == 1U);
    assert(device::resolve(old_handle) == nullptr && device::active_count() == 0U);
    assert(!g_controller.report_queued && g_controller.slot_id == 0U);
    assert(runtime_status() == Status::NoDevice && initialized());
    assert(poll(1U) == 0U && submitted == 10U);

    // Empty controllers must drain status events, including ring wrap, while
    // retaining their ownership for a first/replacement attachment. A stale
    // transfer cannot revive the retired report or publish keyboard input.
    const size_t saved_enqueue = g_controller.interrupt_ring.enqueue;
    for (size_t index = 0U; index < RING_TRB_COUNT * 3U; ++index) {
        const uint32_t cycle = g_controller.event_cycle ? 1U : 0U;
        events[g_controller.event_dequeue] = {UINT64_C(1) << 24U, success,
            static_cast<uint32_t>(TRB_PORT_STATUS_CHANGE) << 10U | cycle};
        assert(poll(1U) == 1U);
        assert(initialized() && !keyboard_ready());
        assert(runtime_status() == Status::NoDevice);
    }
    events[g_controller.event_dequeue] = {0x10060U, success,
        (control & ~UINT32_C(1)) | (g_controller.event_cycle ? 1U : 0U)};
    assert(poll(1U) == 1U);
    assert(submitted == 10U && !g_controller.report_queued);
    assert(g_controller.interrupt_ring.enqueue == saved_enqueue);
    assert(g_controller.command_ring.enqueue == 1U);

    // A missing Disable Slot completion must retain DMA and stop polling.
    g_controller.hid_lifecycle = HidLifecycle::ReleaseInput;
    g_controller.slot_id = 1U;
    dcbaa[1] = 0x60000U;
    assert(poll(1U) == 0U);
    assert(!initialized() && g_controller.cleanup_pending);
    assert(runtime_status() == Status::CommandFailed);
    assert(g_controller.data_page.allocated && dcbaa[1] == 0x60000U);
    std::puts("xHCI transfer completion ownership regression: PASS");
    std::puts("xHCI bounded input backpressure and exact-once retry: PASS");
    std::puts("xHCI disconnect releases, slot retirement and failure quarantine: PASS");
    std::puts("xHCI empty controller event drain and stale transfer rejection: PASS");
    return 0;
}
