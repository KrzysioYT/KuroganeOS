#include <cassert>
#include <cstdio>
#include <initializer_list>

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
bool submit_absolute_pointer(const AbsolutePointerSample&) {
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
namespace arch::x86_64::hpet {
const Capabilities* capabilities() { return nullptr; }
uint64_t counter() { return 0U; }
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
    // A completion from the old attachment can alias the first HID TRB
    // of its replacement. It must not survive successful slot retirement.
    const Trb retired_report{0x10000U, success, control};
    const Trb companion_report{
        0x80000U, success, (control & UINT32_C(0x00FFFFFF)) | (2U << 24U)};
    const Trb port_change{UINT64_C(1) << 24U, success,
        static_cast<uint32_t>(TRB_PORT_STATUS_CHANGE) << 10U | 1U};
    assert(defer_event(g_controller, retired_report));
    assert(defer_event(g_controller, companion_report));
    events[g_controller.event_dequeue] = port_change;
    events[g_controller.event_dequeue + 1U] = retired_report;
    events[g_controller.event_dequeue + 2U] = {0x40000U, success,
        static_cast<uint32_t>(TRB_COMMAND_COMPLETION) << 10U | 1U};
    assert(!progress_hid_lifecycle(g_controller));
    assert(g_controller.deferred_event_count == 2U);
    assert(g_controller.deferred_events[0].parameter == companion_report.parameter);
    assert(g_controller.deferred_events[0].control == companion_report.control);
    assert(g_controller.deferred_events[1].parameter == port_change.parameter);
    assert(g_controller.deferred_events[1].control == port_change.control);
    assert(submitted == 10U && !delivered[8].pressed && !delivered[9].pressed);
    assert(g_controller.hid_lifecycle == HidLifecycle::WaitingForDevice);
    assert(g_controller.command_ring.enqueue == 1U && dcbaa[1] == 0U);
    assert(trb_type(commands[0]) == TRB_DISABLE_SLOT);
    assert((commands[0].control >> 24U) == 1U);
    assert(device::resolve(old_handle) == nullptr && device::active_count() == 0U);
    assert(!g_controller.report_queued && g_controller.slot_id == 0U);
    assert(runtime_status() == Status::NoDevice && initialized());
    assert(poll(2U) == 2U && submitted == 10U);
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

    // An identical TRB completion from the root slot must not satisfy a
    // downstream bulk transfer sharing the controller's event ring.
    g_controller.event_dequeue = 0U;
    g_controller.event_cycle = true;
    g_controller.deferred_event_count = 0U;
    const BulkTarget downstream{g_controller.companion.data_page, 2U, 3U, 4U};
    const uint32_t downstream_control = (control & UINT32_C(0x00FFFFFF)) | (2U << 24U);
    events[0U] = {0x10000U, success, control};
    events[1U] = {0x10000U, success, downstream_control};
    bulk::TransferCompletion received{};
    assert(wait_bulk_transfer(g_controller, downstream, 0x10000U, 3U, 36U, &received));
    assert(received.transferred == 36U && g_controller.event_dequeue == 2U);
    assert(g_controller.deferred_event_count == 1U);
    assert(g_controller.deferred_events[0U].control == control);
    events[2U] = {0x10000U, success, (downstream_control & ~(31U << 16U)) | (4U << 16U)};
    assert(!wait_bulk_transfer(g_controller, downstream, 0x10000U, 3U, 36U, &received));
    std::puts("xHCI downstream bulk slot and endpoint isolation: PASS");

    uint8_t sector[512U]{};
    constexpr size_t magic_bytes = sizeof(USB_TEST_MAGIC) - 1U;
    for (size_t index = 0U; index < magic_bytes; ++index) {
        sector[index] = static_cast<uint8_t>(USB_TEST_MAGIC[index]);
    }
    for (size_t index = magic_bytes; index < sizeof(sector); ++index) {
        sector[index] = static_cast<uint8_t>(((index - magic_bytes) * 37U + 11U) & 0xFFU);
    }
    assert(matches_usb_test_sector(sector, sizeof(sector)));
    assert(!matches_usb_test_sector(sector, sizeof(sector) - 1U));
    sector[sizeof(sector) - 1U] ^= 1U;
    assert(matches_usb_test_magic(sector));
    assert(!matches_usb_test_sector(sector, sizeof(sector)));
    std::puts("xHCI known-sector check rejects truncation and corrupt payload: PASS");

    // Reuse the same child slot only after each Disable Slot acknowledgement.
    // Retired completions and data from disk one must not reach disk two.
    alignas(4096) uint8_t input_context[4096]{};
    alignas(4096) uint8_t device_context[4096]{};
    g_controller.command_ring.enqueue = 0U;
    g_controller.event_dequeue = 0U;
    g_controller.event_cycle = true;
    g_controller.deferred_event_count = 0U;
    clear_bytes(events, sizeof(events));
    auto& companion = g_controller.companion;
    companion.allocated = true;
    companion.child_device = device::INVALID_DEVICE_ID;
    companion.input_context_page = {input_context, 0x90000U, true};
    companion.device_context_page = {device_context, 0xA0000U, true};
    companion.data_page = {report, 0x30000U, true};
    companion.ep0_ring.page = {transfers, 0x10000U, true};
    companion.interrupt_ring.page = {transfers, 0x10000U, true};
    for (size_t child = 0U; child < 2U; ++child) {
        companion.slot_id = 2U;
        companion.port_id = 1U;
        companion.report_queued = true;
        companion.report_trb = 0x10000U;
        companion.ep0_ring.enqueue = 3U;
        report[0] = input_context[0] = device_context[0] = 0xA5U;
        dcbaa[2] = companion.device_context_page.physical_address;
        assert(defer_event(g_controller, {0x10000U, success,
            (control & UINT32_C(0x00FFFFFF)) | (2U << 24U)}));
        events[child] = {0x40000U + child * sizeof(Trb), success,
            static_cast<uint32_t>(TRB_COMMAND_COMPLETION) << 10U | 1U};
        assert(retire_companion_slot(g_controller));
        assert(companion.slot_id == 0U && companion.port_id == 0U && dcbaa[2] == 0U);
        assert(!companion.report_queued && companion.report_trb == 0U);
        assert(companion.ep0_ring.enqueue == 0U && companion.ep0_ring.cycle);
        assert(report[0] == 0U && input_context[0] == 0U && device_context[0] == 0U);
        assert(g_controller.deferred_event_count == 0U);
        assert(companion.hid_lifecycle == HidLifecycle::WaitingForDevice);
    }
    std::puts("xHCI repeated child retirement clears stale DMA and events: PASS");

    // A companion Disable Slot timeout must contain the entire controller,
    // retaining slot/ring ownership and deferring all remaining input/events.
    alignas(4) uint8_t stopped_mmio[0x40]{};
    for (bool halted : {false, true}) {
        clear_bytes(events, sizeof(events));
        g_controller.initialized = true;
        g_controller.cleanup_pending = false;
        g_controller.dma_published = true;
        g_controller.operational = stopped_mmio;
        write32(stopped_mmio, OP_USBCMD, CMD_RUN);
        write32(stopped_mmio, OP_USBSTS, halted ? STS_HALTED : 0U);
        g_controller.hid_kind = HidKind::None;
        g_controller.hid_lifecycle = HidLifecycle::Active;
        g_controller.port_id = 0U;
        g_controller.event_dequeue = 0U;
        g_controller.event_cycle = true;
        g_controller.deferred_event_count = 0U;
        g_controller.command_ring.enqueue = 0U;
        companion.child_device = device::INVALID_DEVICE_ID;
        companion.hid_kind = HidKind::Keyboard;
        assert(register_companion_hid(g_controller));
        const auto retained_child = device::handle_for(companion.child_device);
        companion.hid_kind = HidKind::None;
        companion.hid_lifecycle = HidLifecycle::ReleaseInput;
        companion.slot_id = 2U;
        companion.ep0_ring.enqueue = 3U;
        dcbaa[2U] = 0xA0000U;
        report[0U] = 0xA5U;
        assert(defer_event(g_controller, companion_report));
        assert(poll(1U) == 0U);
        assert(!initialized() && g_controller.cleanup_pending);
        assert(g_controller.hid_lifecycle == HidLifecycle::Failed);
        assert(companion.hid_lifecycle == HidLifecycle::Failed);
        assert(runtime_status() == (halted ? Status::CommandFailed : Status::ControllerHaltTimeout));
        assert((read32(stopped_mmio, OP_USBCMD) & CMD_RUN) == 0U);
        assert(companion.slot_id == 2U && dcbaa[2U] == 0xA0000U);
        assert(companion.data_page.allocated && report[0U] == 0xA5U);
        assert(companion.ep0_ring.enqueue == 3U);
        assert(g_controller.deferred_event_count == 1U);
        assert(g_controller.command_ring.enqueue == 1U);
        assert(poll(1U) == 0U && g_controller.command_ring.enqueue == 1U);
        const auto* failed_child = device::resolve(retained_child);
        assert(failed_child != nullptr && failed_child->status == device::Status::Failed);
        assert(remove_companion_device(&g_controller) == Status::Ok);
    }
    std::puts("xHCI companion retirement timeout halts polling and retains DMA: PASS");

    // An unsafe hub probe must quarantine the slot even if Disable Slot
    // could have succeeded. Only a safely probed hub can be deferred.
    for (bool probe_safe : {true, false}) {
        clear_bytes(events, sizeof(events));
        g_controller.initialized = true;
        g_controller.cleanup_pending = false;
        g_controller.dma_published = true;
        g_controller.hid_lifecycle = HidLifecycle::Active;
        g_controller.event_dequeue = 0U;
        g_controller.event_cycle = true;
        g_controller.deferred_event_count = 0U;
        g_controller.command_ring.enqueue = 0U;
        g_controller.deferred_hub_ports[1U] = false;
        write32(stopped_mmio, OP_USBCMD, CMD_RUN);
        write32(stopped_mmio, OP_USBSTS, STS_HALTED);
        companion.hid_lifecycle = HidLifecycle::WaitingForDevice;
        companion.slot_id = 2U;
        companion.port_id = 1U;
        companion.ep0_ring.enqueue = 3U;
        dcbaa[2U] = 0xA0000U;
        report[0U] = 0xA5U;
        events[0U] = {0x40000U, success,
            static_cast<uint32_t>(TRB_COMMAND_COMPLETION) << 10U | 1U};

        finish_unsupported_companion_probe(g_controller, true, probe_safe);
        if (probe_safe) {
            assert(initialized() && g_controller.deferred_hub_ports[1U]);
            assert(g_controller.command_ring.enqueue == 1U && g_controller.event_dequeue == 1U);
            assert(trb_type(commands[0]) == TRB_DISABLE_SLOT);
            assert(companion.slot_id == 0U && dcbaa[2U] == 0U);
        } else {
            assert(!initialized() && g_controller.cleanup_pending);
            assert(!g_controller.deferred_hub_ports[1U]);
            assert(runtime_status() == Status::CommandFailed);
            assert(g_controller.hid_lifecycle == HidLifecycle::Failed);
            assert(companion.hid_lifecycle == HidLifecycle::Failed);
            assert((read32(stopped_mmio, OP_USBCMD) & CMD_RUN) == 0U);
            assert(companion.slot_id == 2U && dcbaa[2U] == 0xA0000U);
            assert(companion.data_page.allocated && report[0U] == 0xA5U);
            assert(companion.ep0_ring.enqueue == 3U);
            assert(poll(1U) == 0U);
            assert(g_controller.command_ring.enqueue == 0U && g_controller.event_dequeue == 0U);
        }
    }
    std::puts("xHCI unsafe companion hub probe quarantines its active slot: PASS");

    std::puts("xHCI transfer completion ownership regression: PASS");
    std::puts("xHCI bounded input backpressure and exact-once retry: PASS");
    std::puts("xHCI disconnect releases, slot retirement and failure quarantine: PASS");
    std::puts("xHCI empty controller event drain and stale transfer rejection: PASS");
    return 0;
}
