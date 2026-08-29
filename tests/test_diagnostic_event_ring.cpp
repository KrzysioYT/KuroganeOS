#include "../kernel/diagnostics/event_ring.hpp"

#include <assert.h>
#include <stdint.h>
#include <string.h>

using diagnostics::events::Event;
using diagnostics::events::Severity;

int main() {
    diagnostics::events::reset_for_test();
    assert(diagnostics::events::total_recorded() == 0U);

    diagnostics::events::record(
        41U, "VFS", Severity::Info, 3U, 12U, 34U, 0x101U,
        "persistent root ready");
    diagnostics::events::record(
        42U, "IPC", Severity::Warn, 3U, 12U, 34U, 0x202U,
        "bounded retry");

    Event first[4]{};
    const size_t first_count = diagnostics::events::copy_recent(first, 4U);
    assert(first_count == 2U);
    assert(first[0].sequence == 0U);
    assert(first[0].monotonic_tick == 41U);
    assert(first[0].cpu == 3U);
    assert(first[0].pid == 12U);
    assert(first[0].tid == 34U);
    assert(first[0].code == 0x101U);
    assert(first[0].severity == Severity::Info);
    assert(strcmp(first[0].subsystem, "VFS") == 0);
    assert(strcmp(first[0].message, "persistent root ready") == 0);
    assert(first[1].sequence == 1U);

    for (uint64_t index = 0U;
         index < diagnostics::events::EVENT_CAPACITY + 8U;
         ++index) {
        diagnostics::events::record(
            100U + index,
            "RING-OVERFLOW-SUBSYSTEM-NAME-IS-TRUNCATED",
            Severity::Debug,
            diagnostics::events::UNKNOWN_CPU,
            index,
            index + 1U,
            static_cast<uint16_t>(index),
            "diagnostic message intentionally longer than the bounded ring record storage so truncation is deterministic and NUL terminated");
    }

    Event recent[diagnostics::events::EVENT_CAPACITY]{};
    const size_t recent_count = diagnostics::events::copy_recent(
        recent, diagnostics::events::EVENT_CAPACITY);
    assert(recent_count == diagnostics::events::EVENT_CAPACITY);
    assert(recent[0].sequence == 10U);
    assert(recent[recent_count - 1U].sequence == 73U);
    assert(recent[recent_count - 1U].subsystem[
        diagnostics::events::SUBSYSTEM_CAPACITY - 1U] == '\0');
    assert(recent[recent_count - 1U].message[
        diagnostics::events::MESSAGE_CAPACITY - 1U] == '\0');
    assert(diagnostics::events::total_recorded() == 74U);

    return 0;
}
