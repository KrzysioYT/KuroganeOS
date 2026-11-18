#include "../kernel/drivers/usb/hub_port.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <cstdio>

namespace {
namespace hub = drivers::usb::hub;
using Request = hub::PortRequest;

void test_port_power() {
    hub::Descriptor descriptor{};
    descriptor.power_mode = hub::PowerMode::Individual;
    for (uint8_t delay : {uint8_t{0U}, uint8_t{1U}, uint8_t{255U}}) {
        descriptor.power_on_delay_2ms = delay;
        size_t requests = 0U;
        size_t waits = 0U;
        hub::PortStatus port{};
        const auto exchange = [&](Request request, hub::PortStatus* output) {
            assert(request == (requests == 0U ? Request::PowerOn : Request::GetStatus));
            assert((output != nullptr) == (requests == 1U));
            ++requests;
            if (output != nullptr) *output = {hub::PORT_POWERED | hub::PORT_CONNECTED,
                                             hub::CHANGE_CONNECTION};
            return true;
        };
        const auto wait = [&](uint32_t us) {
            assert(us == (delay == 255U ? 510000U : 100000U));
            ++waits;
            return true;
        };
        assert(hub::power_port_for_probe(descriptor, port, exchange, wait));
        assert(requests == 2U && waits == 1U);
        assert(hub::inactive_reset_candidate(port));
    }

    // Failures at SET_FEATURE, timer, GET_STATUS and readback must stop.
    for (unsigned failure = 0U; failure < 7U; ++failure) {
        size_t requests = 0U;
        size_t waits = 0U;
        hub::PortStatus port{};
        const auto exchange = [&](Request request, hub::PortStatus* output) {
            assert(request == (requests == 0U ? Request::PowerOn : Request::GetStatus));
            ++requests;
            if (output != nullptr) {
                *output = {hub::PORT_POWERED, 0U};
                if (failure == 3U) output->status = 0U;
                if (failure == 4U) output->status |= hub::PORT_OVER_CURRENT;
                if (failure == 5U) output->changed = hub::CHANGE_OVER_CURRENT;
                if (failure == 6U) output->status |= hub::PORT_ENABLED;
            }
            return failure != (request == Request::PowerOn ? 0U : 2U);
        };
        const auto wait = [&](uint32_t) { ++waits; return failure != 1U; };
        assert(!hub::power_port_for_probe(descriptor, port, exchange, wait));
        assert(requests == (failure < 2U ? 1U : 2U));
        assert(waits == (failure == 0U ? 0U : 1U));
    }

    const auto forbidden_exchange = [](Request, hub::PortStatus*) {
        assert(false); return false;
    };
    const auto forbidden_wait = [](uint32_t) { assert(false); return false; };
    for (auto mode : {hub::PowerMode::Ganged, hub::PowerMode::AlwaysOn}) {
        descriptor.power_mode = mode;
        hub::PortStatus port{};
        assert(!hub::power_port_for_probe(descriptor, port, forbidden_exchange, forbidden_wait));
        port.status = hub::PORT_POWERED;
        assert(hub::power_port_for_probe(descriptor, port, forbidden_exchange, forbidden_wait));
    }
    descriptor.power_mode = hub::PowerMode::Individual;
    for (auto state : {hub::PORT_ENABLED, hub::PORT_RESET, hub::PORT_SUSPENDED,
                       hub::PORT_OVER_CURRENT}) {
        hub::PortStatus port{state, 0U};
        assert(!hub::power_port_for_probe(descriptor, port, forbidden_exchange, forbidden_wait));
    }
    hub::PortStatus overcurrent{0U, hub::CHANGE_OVER_CURRENT};
    assert(!hub::power_port_for_probe(descriptor, overcurrent, forbidden_exchange, forbidden_wait));
}

struct ResetStep {
    Request request;
    hub::PortStatus reply{};
};

template<size_t N>
void check_reset_sequence(
    hub::PortStatus initial, const ResetStep (&steps)[N],
    size_t short_delays, hub::ResetResult expected, bool expect_callback,
    bool fail_short_delay = false, bool child_retired = true,
    size_t failed_exchange = SIZE_MAX) {
    size_t next = 0U;
    size_t waits = 0U;
    size_t callbacks = 0U;
    const auto exchange = [&](Request request, hub::PortStatus* status) {
        assert(next < N && request == steps[next].request);
        assert((status != nullptr) == (request == Request::GetStatus));
        if (status != nullptr) *status = steps[next].reply;
        return next++ != failed_exchange;
    };
    const auto wait = [&](uint32_t microseconds) {
        assert(microseconds == (waits == 0U ? 100000U : 10000U));
        ++waits;
        return waits == 1U || !fail_short_delay;
    };
    const auto after_reset = [&] {
        assert(next > 0U && steps[next - 1U].request == Request::ClearResetChange);
        if (child_retired) {
            assert(next < N && steps[next].request == Request::Disable);
        } else {
            assert(next == N);
        }
        ++callbacks;
        return child_retired;
    };
    const auto result = hub::probe_port_reset(initial, exchange, wait, after_reset);
    assert(result == expected);
    assert(next == N && waits == 1U + short_delays);
    assert(callbacks == (expect_callback ? 1U : 0U));
}

void test_downstream_reset() {
    constexpr uint16_t connected = hub::PORT_CONNECTED | hub::PORT_POWERED;
    constexpr uint16_t enabled = connected | hub::PORT_ENABLED;
    const ResetStep success[] = {
        {Request::ClearConnectionChange},
        {Request::ClearResetChange},
        {Request::GetStatus, {connected, 0U}},
        {Request::Reset},
        {Request::GetStatus, {connected | hub::PORT_RESET, 0U}},
        {Request::GetStatus, {enabled, hub::CHANGE_RESET | hub::CHANGE_ENABLE}},
        {Request::ClearResetChange},
        {Request::Disable},
        {Request::GetStatus, {connected, hub::CHANGE_ENABLE}},
        {Request::ClearEnableChange},
        {Request::GetStatus, {connected, 0U}},
    };
    check_reset_sequence({connected, hub::CHANGE_CONNECTION | hub::CHANGE_RESET},
        success, 2U, hub::ResetResult::Verified, true);

    const ResetStep replaced[] = {
        {Request::GetStatus, {connected, hub::CHANGE_CONNECTION}},
    };
    check_reset_sequence({connected, 0U}, replaced, 0U, hub::ResetResult::ProbeFailed, false);

    const ResetStep no_timer[] = {
        {Request::GetStatus, {connected, 0U}}, {Request::Reset}, {Request::Disable},
        {Request::GetStatus, {connected | hub::PORT_RESET, 0U}},
    };
    check_reset_sequence({connected, 0U}, no_timer, 1U, hub::ResetResult::UnsafePort, false, true);

    const ResetStep lost_connection[] = {
        {Request::GetStatus, {connected, 0U}},
        {Request::Reset},
        {Request::GetStatus, {hub::PORT_POWERED, hub::CHANGE_CONNECTION}},
        {Request::Disable},
        {Request::GetStatus, {hub::PORT_POWERED, hub::CHANGE_CONNECTION}},
    };
    check_reset_sequence({connected, 0U}, lost_connection, 1U, hub::ResetResult::ProbeFailed, false);

    const ResetStep disable_ignored[] = {
        {Request::GetStatus, {connected, 0U}},
        {Request::Reset},
        {Request::GetStatus, {enabled, hub::CHANGE_RESET}},
        {Request::ClearResetChange},
        {Request::Disable},
        {Request::GetStatus, {enabled, 0U}},
    };
    check_reset_sequence({connected, 0U}, disable_ignored, 1U, hub::ResetResult::UnsafePort, true);

    // A live child slot can still access DMA after a failed Disable Slot.
    // Preserve the port and stop the hub sequence for controller cleanup.
    const ResetStep child_cleanup_failed[] = {
        {Request::GetStatus, {connected, 0U}},
        {Request::Reset},
        {Request::GetStatus, {enabled, hub::CHANGE_RESET}},
        {Request::ClearResetChange},
    };
    check_reset_sequence({connected, 0U}, child_cleanup_failed,
        1U, hub::ResetResult::UnsafePort, true, false, false);

    const ResetStep disable_failed[] = {
        {Request::GetStatus, {connected, 0U}}, {Request::Reset},
        {Request::GetStatus, {enabled, hub::CHANGE_RESET}},
        {Request::ClearResetChange}, {Request::Disable},
    };
    check_reset_sequence({connected, 0U}, disable_failed,
        1U, hub::ResetResult::UnsafePort, true, false, true, 4U);
    check_reset_sequence({connected, 0U}, disable_ignored,
        1U, hub::ResetResult::UnsafePort, true, false, true, 5U);

    const ResetStep reenabled[] = {
        {Request::GetStatus, {connected, 0U}}, {Request::Reset},
        {Request::GetStatus, {enabled, hub::CHANGE_RESET}},
        {Request::ClearResetChange}, {Request::Disable},
        {Request::GetStatus, {connected, hub::CHANGE_ENABLE}},
        {Request::ClearEnableChange}, {Request::GetStatus, {enabled, 0U}},
    };
    check_reset_sequence({connected, 0U}, reenabled,
        1U, hub::ResetResult::UnsafePort, true);

    // Reset timeout is recoverable only after Disable and status readback.
    ResetStep timed_out[14U] = {
        {Request::GetStatus, {connected, 0U}}, {Request::Reset},
    };
    for (size_t i = 2U; i < 12U; ++i)
        timed_out[i] = {Request::GetStatus, {connected | hub::PORT_RESET, 0U}};
    timed_out[12U] = {Request::Disable};
    timed_out[13U] = {Request::GetStatus, {connected, 0U}};
    check_reset_sequence({connected, 0U}, timed_out,
        10U, hub::ResetResult::ProbeFailed, false);
    std::puts("USB2 hub reset cleanup verification and unsafe-port containment: PASS");

    assert(!hub::inactive_reset_candidate({enabled, 0U}));
    assert(!hub::inactive_reset_candidate({connected | hub::PORT_OVER_CURRENT, 0U}));
    assert(!hub::inactive_reset_candidate({hub::PORT_CONNECTED, 0U}));
}
} // namespace

int main() {
    namespace hub = drivers::usb::hub;
    hub::ControlRequest request{};
    assert(hub::make_port_request(4U, 3U, hub::PortRequest::GetStatus,
                                  &request));
    assert(request.request_type == 0xA3U && request.request == 0U);
    assert(request.value == 0U && request.index == 3U &&
           request.length == 4U);
    assert(hub::make_port_request(4U, 1U, hub::PortRequest::PowerOn,
                                  &request));
    assert(request.request_type == 0x23U && request.request == 3U);
    assert(request.value == 8U && request.index == 1U &&
           request.length == 0U);
    assert(hub::make_port_request(4U, 4U, hub::PortRequest::Reset,
                                  &request));
    assert(request.value == 4U && request.index == 4U);
    assert(hub::make_port_request(4U, 2U, hub::PortRequest::Disable, &request));
    assert(request.request_type == 0x23U && request.request == 1U &&
           request.value == 1U && request.index == 2U && request.length == 0U);
    const hub::PortRequest clear_commands[] = {
        hub::PortRequest::ClearConnectionChange,
        hub::PortRequest::ClearEnableChange,
        hub::PortRequest::ClearSuspendChange,
        hub::PortRequest::ClearOverCurrentChange,
        hub::PortRequest::ClearResetChange,
    };
    for (size_t i = 0U; i < 5U; ++i) {
        assert(hub::make_port_request(15U, 15U, clear_commands[i],
                                      &request));
        assert(request.request_type == 0x23U && request.request == 1U);
        assert(request.value == 16U + i && request.index == 15U);
        assert(request.length == 0U);
    }
    const auto preserved = request;
    assert(!hub::make_port_request(0U, 1U, hub::PortRequest::GetStatus,
                                   &request));
    assert(!hub::make_port_request(16U, 1U, hub::PortRequest::GetStatus,
                                   &request));
    assert(!hub::make_port_request(4U, 0U, hub::PortRequest::PowerOn,
                                   &request));
    assert(!hub::make_port_request(4U, 5U, hub::PortRequest::Reset,
                                   &request));
    assert(!hub::make_port_request(4U, 3U, hub::PortRequest::Reset,
                                   nullptr));
    assert(!hub::make_port_request(
        4U, 1U, static_cast<hub::PortRequest>(UINT8_C(255)), &request));
    assert(request.value == preserved.value &&
           request.index == preserved.index &&
           request.request == preserved.request);

    hub::PortStatus port{};
    const uint8_t ready[] = {0x03U, 0x01U, 0x00U, 0x00U};
    assert(hub::parse_port_status(ready, sizeof(ready), &port) ==
           hub::PortParse::Ok);
    assert(port.status == (hub::PORT_CONNECTED | hub::PORT_ENABLED |
                           hub::PORT_POWERED));
    assert(port.changed == 0U);
    assert(hub::next_port_action(port) == hub::PortAction::Ready);

    const uint8_t disconnect[] = {0x00U, 0x01U, 0x01U, 0x00U};
    assert(hub::parse_port_status(disconnect, sizeof(disconnect), &port) ==
           hub::PortParse::Ok);
    assert(hub::next_port_action(port) ==
           hub::PortAction::ClearConnectionChange);
    port.changed = 0U;
    assert(hub::next_port_action(port) == hub::PortAction::Disconnected);

    port = {hub::PORT_CONNECTED, 0U};
    assert(hub::next_port_action(port) == hub::PortAction::PowerOn);
    port = {static_cast<uint16_t>(hub::PORT_CONNECTED |
                                  hub::PORT_POWERED), 0U};
    assert(hub::next_port_action(port) == hub::PortAction::Reset);
    port.status |= hub::PORT_RESET;
    assert(hub::next_port_action(port) == hub::PortAction::WaitReset);
    port.status &= static_cast<uint16_t>(~hub::PORT_RESET);
    port.changed = hub::CHANGE_RESET;
    assert(hub::next_port_action(port) ==
           hub::PortAction::ClearResetChange);
    port.status |= hub::PORT_ENABLED;
    port.changed = 0U;
    assert(hub::next_port_action(port) == hub::PortAction::Ready);
    port.status |= hub::PORT_SUSPENDED;
    assert(hub::next_port_action(port) == hub::PortAction::Suspended);

    port.status |= hub::PORT_OVER_CURRENT;
    port.changed = hub::CHANGE_CONNECTION | hub::CHANGE_OVER_CURRENT;
    assert(hub::next_port_action(port) == hub::PortAction::OverCurrent);
    port.status &= static_cast<uint16_t>(~hub::PORT_OVER_CURRENT);
    assert(hub::next_port_action(port) ==
           hub::PortAction::ClearOverCurrentChange);
    port.changed = hub::CHANGE_ENABLE;
    assert(hub::next_port_action(port) == hub::PortAction::ClearEnableChange);
    port.changed = hub::CHANGE_SUSPEND;
    assert(hub::next_port_action(port) == hub::PortAction::ClearSuspendChange);

    const hub::PortStatus previous = port;
    const uint8_t too_many_speeds[] = {0U, 0x06U, 0U, 0U};
    assert(hub::parse_port_status(too_many_speeds, 4U, &port) ==
           hub::PortParse::InvalidStatus);
    const uint8_t all_ones[] = {0xFFU, 0xFFU, 0xFFU, 0xFFU};
    assert(hub::parse_port_status(all_ones, 4U, &port) ==
           hub::PortParse::InvalidStatus);
    const uint8_t unread_tail[] = {0x01U, 0x01U, 0x01U, 0xFFU};
    assert(hub::parse_port_status(unread_tail, sizeof(unread_tail), &port) ==
           hub::PortParse::InvalidStatus);
    assert(hub::parse_port_status(ready, 3U, &port) ==
           hub::PortParse::Truncated);
    assert(hub::parse_port_status(nullptr, 4U, &port) ==
           hub::PortParse::InvalidArgument);
    assert(hub::parse_port_status(ready, 4U, nullptr) ==
           hub::PortParse::InvalidArgument);
    assert(port.status == previous.status && port.changed == previous.changed);
    test_downstream_reset();
    test_port_power();
    return 0;
}
