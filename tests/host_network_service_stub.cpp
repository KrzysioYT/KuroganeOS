#include "../kernel/net/service.hpp"

namespace net::service {
namespace {

bool g_ready = false;
NetworkStats g_stats{};

} // namespace

Status initialize() {
    g_ready = true;
    g_stats = {};
    return Status::Ok;
}

bool ready() {
    return g_ready;
}

Status ping_loopback(uint16_t sequence, PingReply* reply) {
    if (!g_ready) return Status::NotInitialized;

    // The real loopback path emits an ICMP request and response, with both
    // Ethernet frames observed by the stack. Preserve that profiler-facing
    // accounting contract without pulling DHCP/TCP/TLS/PIT/rootfs into this
    // focused host test.
    g_stats.frames_transmitted += 2U;
    g_stats.frames_received += 2U;

    if (reply != nullptr) {
        *reply = {};
        reply->valid = true;
        reply->identifier = UINT16_C(0x4B4F);
        reply->sequence = sequence;
    }
    return Status::Ok;
}

Status stats(NetworkStats* output) {
    if (!g_ready) return Status::NotInitialized;
    if (output == nullptr) return Status::InvalidArgument;
    *output = g_stats;
    return Status::Ok;
}

} // namespace net::service
