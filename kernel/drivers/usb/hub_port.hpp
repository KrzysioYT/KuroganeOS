#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hub_protocol.hpp"

namespace drivers::usb::hub {

// USB 2.0 hub-class requests for one downstream port. A full hub driver
// still needs configuration, xHCI endpoint ownership, timers, transaction
// translators and correct handling of disconnect/reset before issuing these.
struct ControlRequest {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
};

enum class PortRequest : uint8_t {
    GetStatus,
    PowerOn,
    Reset,
    ClearConnectionChange,
    ClearEnableChange,
    ClearSuspendChange,
    ClearOverCurrentChange,
    ClearResetChange,
    Disable,
};

inline bool make_port_request(
    uint8_t port_count, uint8_t port, PortRequest request,
    ControlRequest* output) {
    if (output == nullptr || port_count == 0U ||
        port_count > MAX_ROUTE_PORTS || port == 0U ||
        port > port_count) return false;

    ControlRequest result{};
    result.index = port; // High byte must be zero for these USB2 requests.
    switch (request) {
        case PortRequest::GetStatus:
            result.request_type = 0xA3U; // IN, class, other/port.
            result.request = 0U;         // GET_STATUS
            result.length = 4U;          // wPortStatus + wPortChange
            break;
        case PortRequest::PowerOn:
            result.request_type = 0x23U; // OUT, class, other/port.
            result.request = 3U;         // SET_FEATURE
            result.value = 8U;           // PORT_POWER
            break;
        case PortRequest::Reset:
            result.request_type = 0x23U;
            result.request = 3U;
            result.value = 4U;           // PORT_RESET
            break;
        case PortRequest::Disable:
            result.request_type = 0x23U;
            result.request = 1U;         // CLEAR_FEATURE
            result.value = 1U;           // PORT_ENABLE
            break;
        case PortRequest::ClearConnectionChange:
        case PortRequest::ClearEnableChange:
        case PortRequest::ClearSuspendChange:
        case PortRequest::ClearOverCurrentChange:
        case PortRequest::ClearResetChange:
            result.request_type = 0x23U;
            result.request = 1U;         // CLEAR_FEATURE, acknowledge W1C.
            result.value = static_cast<uint16_t>(16U +
                static_cast<uint8_t>(request) -
                static_cast<uint8_t>(PortRequest::ClearConnectionChange));
            break;
        default:
            return false;
    }
    *output = result;
    return true;
}

constexpr uint16_t PORT_CONNECTED = UINT16_C(1) << 0U;
constexpr uint16_t PORT_ENABLED = UINT16_C(1) << 1U;
constexpr uint16_t PORT_SUSPENDED = UINT16_C(1) << 2U;
constexpr uint16_t PORT_OVER_CURRENT = UINT16_C(1) << 3U;
constexpr uint16_t PORT_RESET = UINT16_C(1) << 4U;
constexpr uint16_t PORT_POWERED = UINT16_C(1) << 8U;
constexpr uint16_t PORT_LOW_SPEED = UINT16_C(1) << 9U;
constexpr uint16_t PORT_HIGH_SPEED = UINT16_C(1) << 10U;
constexpr uint16_t CHANGE_CONNECTION = UINT16_C(1) << 0U;
constexpr uint16_t CHANGE_ENABLE = UINT16_C(1) << 1U;
constexpr uint16_t CHANGE_SUSPEND = UINT16_C(1) << 2U;
constexpr uint16_t CHANGE_OVER_CURRENT = UINT16_C(1) << 3U;
constexpr uint16_t CHANGE_RESET = UINT16_C(1) << 4U;

struct PortStatus {
    uint16_t status;
    uint16_t changed;
};

enum class PortParse : uint8_t {
    Ok,
    InvalidArgument,
    Truncated,
    InvalidStatus,
};

inline PortParse parse_port_status(
    const uint8_t* data, size_t length, PortStatus* output) {
    if (data == nullptr || output == nullptr) return PortParse::InvalidArgument;
    if (length < 4U) return PortParse::Truncated;
    const uint16_t status = static_cast<uint16_t>(data[0U]) |
        static_cast<uint16_t>(data[1U]) << 8U;
    const uint16_t changed = static_cast<uint16_t>(data[2U]) |
        static_cast<uint16_t>(data[3U]) << 8U;

    // A USB2 port cannot report both speeds at once. Reject reserved change
    // bits as well, so an unread all-one DMA tail cannot look like status.
    if ((status & (PORT_LOW_SPEED | PORT_HIGH_SPEED)) ==
            (PORT_LOW_SPEED | PORT_HIGH_SPEED) ||
        (changed & UINT16_C(0xFFC0)) != 0U)
        return PortParse::InvalidStatus;

    *output = {status, changed};
    return PortParse::Ok;
}

enum class PortAction : uint8_t {
    OverCurrent,
    ClearOverCurrentChange,
    ClearConnectionChange,
    ClearResetChange,
    ClearEnableChange,
    ClearSuspendChange,
    PowerOn,
    Disconnected,
    WaitReset,
    Reset,
    Suspended,
    Ready,
};

inline PortAction next_port_action(const PortStatus& port) {
    if ((port.status & PORT_OVER_CURRENT) != 0U)
        return PortAction::OverCurrent;
    if ((port.changed & CHANGE_OVER_CURRENT) != 0U)
        return PortAction::ClearOverCurrentChange;
    if ((port.changed & CHANGE_CONNECTION) != 0U)
        return PortAction::ClearConnectionChange;
    if ((port.changed & CHANGE_RESET) != 0U)
        return PortAction::ClearResetChange;
    if ((port.changed & CHANGE_ENABLE) != 0U)
        return PortAction::ClearEnableChange;
    if ((port.changed & CHANGE_SUSPEND) != 0U)
        return PortAction::ClearSuspendChange;
    if ((port.status & PORT_POWERED) == 0U)
        return PortAction::PowerOn;
    if ((port.status & PORT_CONNECTED) == 0U)
        return PortAction::Disconnected;
    if ((port.status & PORT_RESET) != 0U)
        return PortAction::WaitReset;
    if ((port.status & PORT_ENABLED) == 0U)
        return PortAction::Reset;
    if ((port.status & PORT_SUSPENDED) != 0U)
        return PortAction::Suspended;
    return PortAction::Ready;
}

inline bool inactive_reset_candidate(const PortStatus& port) {
    constexpr uint16_t required = PORT_CONNECTED | PORT_POWERED;
    constexpr uint16_t blocked = PORT_ENABLED | PORT_SUSPENDED |
        PORT_OVER_CURRENT | PORT_RESET;
    constexpr uint16_t clearable = CHANGE_CONNECTION | CHANGE_RESET;
    return (port.status & required) == required && (port.status & blocked) == 0U &&
        (port.changed & ~clearable) == 0U;
}

// Probe one powered, inactive port during controller initialization. The
// temporary hub slot has no child to address; disable the port again before
// retirement. The caller owns the slot for this entire exchange.
template<typename Exchange, typename Wait>
bool probe_port_reset(PortStatus port, Exchange exchange, Wait wait_us) {
    if (!inactive_reset_candidate(port)) return false;
    if ((port.changed & CHANGE_CONNECTION) != 0U &&
        !exchange(PortRequest::ClearConnectionChange, nullptr)) return false;
    if ((port.changed & CHANGE_RESET) != 0U &&
        !exchange(PortRequest::ClearResetChange, nullptr)) return false;
    if (!wait_us(100000U) || !exchange(PortRequest::GetStatus, &port) ||
        !inactive_reset_candidate(port) || port.changed != 0U) return false;

    bool completed = false;
    if (exchange(PortRequest::Reset, nullptr)) {
        constexpr uint16_t required = PORT_CONNECTED | PORT_POWERED;
        for (unsigned attempt = 0U; attempt < 10U; ++attempt) {
            if (!wait_us(10000U) || !exchange(PortRequest::GetStatus, &port) ||
                (port.status & required) != required ||
                (port.status & (PORT_OVER_CURRENT | PORT_SUSPENDED)) != 0U ||
                (port.changed & (CHANGE_CONNECTION | CHANGE_OVER_CURRENT)) != 0U) break;
            if ((port.status & (PORT_RESET | PORT_ENABLED)) == PORT_ENABLED &&
                (port.changed & CHANGE_RESET) != 0U) {
                completed = exchange(PortRequest::ClearResetChange, nullptr);
                break;
            }
        }
    }
    // A failed transfer may still have reached the hub; always request
    // cleanup once a reset was attempted. Only verified cleanup earns PASS.
    const bool disabled = exchange(PortRequest::Disable, nullptr);
    if (!completed || !disabled || !exchange(PortRequest::GetStatus, &port)) return false;
    if (port.changed == CHANGE_ENABLE &&
        (!exchange(PortRequest::ClearEnableChange, nullptr) ||
         !exchange(PortRequest::GetStatus, &port))) return false;
    return inactive_reset_candidate(port) && port.changed == 0U;
}
} // namespace drivers::usb::hub
