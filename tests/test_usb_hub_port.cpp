#include "../kernel/drivers/usb/hub_port.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

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
    assert(hub::parse_port_status(ready, 3U, &port) ==
           hub::PortParse::Truncated);
    assert(hub::parse_port_status(nullptr, 4U, &port) ==
           hub::PortParse::InvalidArgument);
    assert(hub::parse_port_status(ready, 4U, nullptr) ==
           hub::PortParse::InvalidArgument);
    assert(port.status == previous.status && port.changed == previous.changed);
    return 0;
}
