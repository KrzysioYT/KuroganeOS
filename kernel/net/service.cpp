#include "service.hpp"

namespace net::service {

namespace {
LoopbackInterface g_loopback{};
NetworkStack g_stack{};
bool g_ready = false;

constexpr MacAddress kLoopbackMac = {
    {0x02, 0x4B, 0x55, 0x52, 0x4F, 0x01}
};
constexpr IPv4Address kLoopbackIp = {{127, 0, 0, 1}};
constexpr IPv4Address kLoopbackMask = {{255, 0, 0, 0}};
constexpr IPv4Address kNoGateway = {{0, 0, 0, 0}};
constexpr uint8_t kPingPayload[] = {'K', 'U', 'R', 'O'};
} // namespace

Status initialize() {
    g_ready = false;
    Status status = initialize_loopback(&g_loopback, kLoopbackMac);
    if (status != Status::Ok) {
        return status;
    }
    status = initialize_stack(&g_stack, &g_loopback.interface);
    if (status != Status::Ok) {
        return status;
    }
    const IPv4Config config = {kLoopbackIp, kLoopbackMask, kNoGateway};
    status = configure_ipv4(&g_stack, config);
    if (status == Status::Ok) {
        g_ready = true;
    }
    return status;
}

bool ready() {
    return g_ready;
}

Status poll(size_t budget, size_t* processed) {
    if (!g_ready) {
        if (processed) {
            *processed = 0;
        }
        return Status::NotInitialized;
    }
    return net::poll(&g_stack, budget, processed);
}

Status ping_loopback(uint16_t sequence, PingReply* reply) {
    if (!g_ready) {
        return Status::NotInitialized;
    }
    Status status = send_ping(
        &g_stack, kLoopbackIp, UINT16_C(0x4B4F), sequence,
        kPingPayload, sizeof(kPingPayload));
    if (status != Status::Ok) {
        return status;
    }
    size_t processed = 0;
    status = net::poll(&g_stack, 4, &processed);
    if (status != Status::Ok) {
        return status;
    }
    PingReply result{};
    status = get_last_ping_reply(&g_stack, &result);
    if (status != Status::Ok || !result.valid ||
        !ipv4_equal(result.source, kLoopbackIp) ||
        result.identifier != UINT16_C(0x4B4F) ||
        result.sequence != sequence ||
        result.payload_length != sizeof(kPingPayload)) {
        return status == Status::Ok ? Status::MalformedPacket : status;
    }
    if (reply) {
        *reply = result;
    }
    return Status::Ok;
}

Status stats(NetworkStats* output) {
    return get_stats(&g_stack, output);
}

const IPv4Config* configuration() {
    return g_ready ? &g_stack.config : nullptr;
}

} // namespace net::service
