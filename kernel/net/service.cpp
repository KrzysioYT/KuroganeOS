#include "service.hpp"

#include "physical.hpp"
#include "dhcp.hpp"
#include "protocols.hpp"

namespace net::service {

namespace {
LoopbackInterface g_loopback{};
NetworkStack g_stack{};
bool g_ready = false;
bool g_physical = false;
bool g_dhcp = false;
bool g_physical_detected = false;
Status g_physical_status = Status::NotInitialized;
dhcp::Lease g_lease{};
uint16_t g_dns_transaction = UINT16_C(0x4b55);
uint16_t g_ephemeral_port = UINT16_C(49152);

constexpr MacAddress kLoopbackMac = {
    {0x02, 0x4B, 0x55, 0x52, 0x4F, 0x01}
};
constexpr IPv4Address kLoopbackIp = {{127, 0, 0, 1}};
constexpr IPv4Address kLoopbackMask = {{255, 0, 0, 0}};
constexpr IPv4Address kNoGateway = {{0, 0, 0, 0}};
constexpr uint8_t kPingPayload[] = {'K', 'U', 'R', 'O'};

bool acceptable_poll_status(Status status) {
    return status == Status::Ok || status == Status::NotForUs ||
        status == Status::UnsupportedProtocol;
}

uint16_t next_ephemeral_port() {
    const uint16_t result = g_ephemeral_port;
    ++g_ephemeral_port;
    if (g_ephemeral_port < UINT16_C(49152)) {
        g_ephemeral_port = UINT16_C(49152);
    }
    return result;
}

Status initialize_loopback_fallback() {
    const Status loop_status = initialize_loopback(&g_loopback, kLoopbackMac);
    if (loop_status != Status::Ok) return loop_status;
    Status status = initialize_stack(&g_stack, &g_loopback.interface);
    if (status != Status::Ok) return status;
    status = configure_ipv4(
        &g_stack, IPv4Config{kLoopbackIp, kLoopbackMask, kNoGateway});
    if (status == Status::Ok) g_ready = true;
    return status;
}

Status wait_for_ping(
    const IPv4Address& destination,
    uint16_t sequence,
    PingReply* reply) {
    constexpr size_t poll_budget = 200000U;
    Status status = send_ping(
        &g_stack,
        destination,
        UINT16_C(0x4B4F),
        sequence,
        kPingPayload,
        sizeof(kPingPayload));
    if (status == Status::NeighborResolutionPending) {
        bool resolved = false;
        for (size_t attempt = 0U; attempt < poll_budget; ++attempt) {
            size_t processed = 0U;
            status = net::poll(&g_stack, 8U, &processed);
            if (status != Status::Ok) return status;
            NeighborEntry neighbor{};
            if (lookup_neighbor(&g_stack, destination, &neighbor) == Status::Ok) {
                resolved = true;
                break;
            }
            __asm__ volatile("pause");
        }
        if (!resolved) return Status::NeighborResolutionPending;
        status = send_ping(
            &g_stack,
            destination,
            UINT16_C(0x4B4F),
            sequence,
            kPingPayload,
            sizeof(kPingPayload));
    }
    if (status != Status::Ok) return status;
    for (size_t attempt = 0U; attempt < poll_budget; ++attempt) {
        size_t processed = 0U;
        status = net::poll(&g_stack, 8U, &processed);
        if (status != Status::Ok) return status;
        PingReply result{};
        status = get_last_ping_reply(&g_stack, &result);
        if (status == Status::Ok && result.valid &&
            ipv4_equal(result.source, destination) &&
            result.identifier == UINT16_C(0x4B4F) &&
            result.sequence == sequence &&
            result.payload_length == sizeof(kPingPayload)) {
            if (reply != nullptr) *reply = result;
            return Status::Ok;
        }
        __asm__ volatile("pause");
    }
    return Status::WouldBlock;
}

bool append_request_text(
    uint8_t* output,
    size_t capacity,
    size_t* cursor,
    const char* text) {
    if (output == nullptr || cursor == nullptr || text == nullptr) return false;
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (*cursor >= capacity) return false;
        output[(*cursor)++] = static_cast<uint8_t>(text[index]);
    }
    return true;
}

uint16_t parse_http_status(const uint8_t* bytes, size_t length) {
    if (bytes == nullptr || length < 12U) return 0U;
    if (bytes[0] != 'H' || bytes[1] != 'T' || bytes[2] != 'T' ||
        bytes[3] != 'P' || bytes[4] != '/' || bytes[8] != ' ') {
        return 0U;
    }
    if (bytes[9] < '0' || bytes[9] > '9' ||
        bytes[10] < '0' || bytes[10] > '9' ||
        bytes[11] < '0' || bytes[11] > '9') {
        return 0U;
    }
    return static_cast<uint16_t>(
        static_cast<uint16_t>(bytes[9] - '0') * 100U +
        static_cast<uint16_t>(bytes[10] - '0') * 10U +
        static_cast<uint16_t>(bytes[11] - '0'));
}
} // namespace

Status initialize() {
    g_ready = false;
    g_physical = false;
    g_dhcp = false;
    g_physical_detected = false;
    g_physical_status = Status::NotInitialized;
    g_lease = {};
#ifdef KUROGANE_HOST_TEST
    return initialize_loopback_fallback();
#else
    const physical::Status device_status = physical::initialize();
    NetworkInterface* const network_interface = physical::interface();
    g_physical_detected = physical::detected();
    if ((device_status == physical::Status::Ok ||
         device_status == physical::Status::AlreadyInitialized) &&
        network_interface != nullptr) {
        const Status dhcp_status = dhcp::acquire(network_interface, &g_lease);
        if (dhcp_status == Status::Ok) {
            Status status = initialize_stack(&g_stack, network_interface);
            if (status != Status::Ok) {
                g_physical_status = status;
                return initialize_loopback_fallback();
            }
            status = configure_ipv4(&g_stack, g_lease.configuration);
            if (status == Status::Ok) {
                g_ready = true;
                g_physical = true;
                g_dhcp = true;
                g_physical_status = Status::Ok;
                return Status::Ok;
            }
            g_physical_status = status;
            return initialize_loopback_fallback();
        }
        // Missing cable, hypervisor NAT startup races or an unavailable DHCP
        // server must degrade networking instead of making KuroganeOS unbootable.
        g_physical_status = dhcp_status;
        return initialize_loopback_fallback();
    }
    g_physical_status = g_physical_detected
        ? Status::InterfaceError
        : Status::NotConfigured;
    return initialize_loopback_fallback();
#endif
}

bool ready() { return g_ready; }

Status poll(size_t budget, size_t* processed) {
    if (!g_ready) {
        if (processed) *processed = 0;
        return Status::NotInitialized;
    }
    return net::poll(&g_stack, budget, processed);
}

Status ping_loopback(uint16_t sequence, PingReply* reply) {
    if (!g_ready) return Status::NotInitialized;
    Status status = send_ping(
        &g_stack, kLoopbackIp, UINT16_C(0x4B4F), sequence,
        kPingPayload, sizeof(kPingPayload));
    if (status != Status::Ok) return status;
    size_t processed = 0;
    status = net::poll(&g_stack, 4, &processed);
    if (status != Status::Ok) return status;
    PingReply result{};
    status = get_last_ping_reply(&g_stack, &result);
    if (status != Status::Ok || !result.valid ||
        !ipv4_equal(result.source, kLoopbackIp) ||
        result.identifier != UINT16_C(0x4B4F) ||
        result.sequence != sequence ||
        result.payload_length != sizeof(kPingPayload)) {
        return status == Status::Ok ? Status::MalformedPacket : status;
    }
    if (reply) *reply = result;
    return Status::Ok;
}

Status ping_gateway(uint16_t sequence, PingReply* reply) {
    if (!g_ready) return Status::NotInitialized;
    return g_physical
        ? wait_for_ping(g_stack.config.gateway, sequence, reply)
        : ping_loopback(sequence, reply);
}

Status ping_address(
    const IPv4Address& address,
    uint16_t sequence,
    PingReply* reply) {
    if (!g_ready) return Status::NotInitialized;
    return wait_for_ping(address, sequence, reply);
}

Status resolve_a(const char* name, IPv4Address* out_address) {
    if (!g_ready) return Status::NotInitialized;
    if (name == nullptr || out_address == nullptr) return Status::InvalidArgument;
    if (!g_dhcp || ipv4_is_zero(g_lease.dns_server)) return Status::NotConfigured;

    uint8_t query[512]{};
    size_t query_length = 0U;
    const uint16_t transaction = g_dns_transaction++;
    Status status = serialize_dns_a_query(
        transaction, name, query, sizeof(query), &query_length);
    if (status != Status::Ok) return status;
    UdpDatagram stale{};
    while (take_udp_datagram(&g_stack, &stale) == Status::Ok) {}
    const uint16_t source_port = next_ephemeral_port();
    constexpr size_t send_budget = 200000U;
    bool sent = false;
    for (size_t attempt = 0U; attempt < send_budget; ++attempt) {
        status = send_udp(
            &g_stack, g_lease.dns_server,
            source_port, 53U, query, query_length);
        if (status == Status::Ok) {
            sent = true;
            break;
        }
        if (status != Status::NeighborResolutionPending) return status;
        size_t processed = 0U;
        status = net::poll(&g_stack, 8U, &processed);
        if (!acceptable_poll_status(status)) return status;
        __asm__ volatile("pause");
    }
    if (!sent) return Status::WouldBlock;

    constexpr size_t reply_budget = 400000U;
    for (size_t attempt = 0U; attempt < reply_budget; ++attempt) {
        size_t processed = 0U;
        status = net::poll(&g_stack, 8U, &processed);
        if (!acceptable_poll_status(status)) return status;
        UdpDatagram datagram{};
        if (take_udp_datagram(&g_stack, &datagram) == Status::Ok &&
            ipv4_equal(datagram.source, g_lease.dns_server) &&
            datagram.source_port == 53U &&
            datagram.destination_port == source_port) {
            DnsAnswer answer{};
            status = parse_dns_a_response(
                datagram.payload, datagram.payload_length,
                transaction, &answer);
            if (status == Status::Ok) {
                *out_address = answer.address;
                return Status::Ok;
            }
            if (status != Status::NotForUs) return status;
        }
        __asm__ volatile("pause");
    }
    return Status::WouldBlock;
}

Status tcp_connect_probe(
    const IPv4Address& address,
    uint16_t port,
    const char* host_name) {
    if (!g_ready) return Status::NotInitialized;
    if (port == 0U || host_name == nullptr) return Status::InvalidArgument;
    const uint16_t source_port = next_ephemeral_port();
    const uint32_t initial_sequence = UINT32_C(0x4b550001);
    TcpSegment stale{};
    while (take_tcp_segment(&g_stack, &stale) == Status::Ok) {}
    bool sent = false;
    Status status = Status::WouldBlock;
    for (size_t attempt = 0U; attempt < 200000U; ++attempt) {
        status = send_tcp(
            &g_stack, address, source_port, port,
            initial_sequence, 0U, TcpSyn, UINT16_C(32768), nullptr, 0U);
        if (status == Status::Ok) {
            sent = true;
            break;
        }
        if (status != Status::NeighborResolutionPending) return status;
        size_t processed = 0U;
        status = net::poll(&g_stack, 8U, &processed);
        if (!acceptable_poll_status(status)) return status;
    }
    if (!sent) return Status::WouldBlock;
    TcpSegment reply{};
    bool established = false;
    for (size_t attempt = 0U; attempt < 500000U; ++attempt) {
        size_t processed = 0U;
        status = net::poll(&g_stack, 8U, &processed);
        if (!acceptable_poll_status(status)) return status;
        if (take_tcp_segment(&g_stack, &reply) == Status::Ok &&
            ipv4_equal(reply.source, address) && reply.source_port == port &&
            reply.destination_port == source_port) {
            if ((reply.flags & TcpRst) != 0U) return Status::InterfaceError;
            if ((reply.flags & (TcpSyn | TcpAck)) == (TcpSyn | TcpAck) &&
                reply.acknowledgement == initial_sequence + 1U) {
                established = true;
                break;
            }
        }
        __asm__ volatile("pause");
    }
    if (!established) return Status::WouldBlock;
    status = send_tcp(
        &g_stack, address, source_port, port,
        initial_sequence + 1U, reply.sequence + 1U,
        TcpAck, UINT16_C(32768), nullptr, 0U);
    if (status != Status::Ok) return status;

    uint8_t request[192]{};
    const char prefix[] = "HEAD / HTTP/1.0\r\nHost: ";
    const char suffix[] = "\r\nConnection: close\r\n\r\n";
    size_t cursor = 0U;
    if (!append_request_text(request, sizeof(request), &cursor, prefix) ||
        !append_request_text(request, sizeof(request), &cursor, host_name) ||
        !append_request_text(request, sizeof(request), &cursor, suffix)) {
        return Status::PayloadTooLarge;
    }
    status = send_tcp(
        &g_stack, address, source_port, port,
        initial_sequence + 1U, reply.sequence + 1U,
        TcpPsh | TcpAck, UINT16_C(32768), request, cursor);
    if (status != Status::Ok) return status;
    const uint32_t expected_ack = initial_sequence + 1U +
        static_cast<uint32_t>(cursor);
    for (size_t attempt = 0U; attempt < 800000U; ++attempt) {
        size_t processed = 0U;
        status = net::poll(&g_stack, 8U, &processed);
        if (!acceptable_poll_status(status)) return status;
        TcpSegment segment{};
        if (take_tcp_segment(&g_stack, &segment) == Status::Ok &&
            ipv4_equal(segment.source, address) &&
            segment.source_port == port && segment.destination_port == source_port) {
            if ((segment.flags & TcpRst) != 0U) return Status::InterfaceError;
            if ((segment.flags & TcpAck) != 0U &&
                segment.acknowledgement == expected_ack) {
                const uint32_t receive_next = segment.sequence +
                    static_cast<uint32_t>(segment.payload_length) +
                    (((segment.flags & TcpFin) != 0U) ? 1U : 0U);
                static_cast<void>(send_tcp(
                    &g_stack, address, source_port, port,
                    expected_ack, receive_next, TcpAck,
                    UINT16_C(32768), nullptr, 0U));
                return Status::Ok;
            }
        }
        __asm__ volatile("pause");
    }
    return Status::WouldBlock;
}

Status http_get(
    const char* host_name,
    const char* path,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length,
    uint16_t* out_http_status) {
    if (out_length != nullptr) *out_length = 0U;
    if (out_http_status != nullptr) *out_http_status = 0U;
    if (!g_ready) return Status::NotInitialized;
    if (!g_physical || !g_dhcp) return Status::NotConfigured;
    if (host_name == nullptr || path == nullptr || output == nullptr ||
        out_length == nullptr || out_http_status == nullptr ||
        output_capacity == 0U || path[0] != '/') {
        return Status::InvalidArgument;
    }

    size_t host_length = 0U;
    while (host_length < 64U && host_name[host_length] != '\0') ++host_length;
    size_t path_length = 0U;
    while (path_length < 160U && path[path_length] != '\0') ++path_length;
    if (host_length == 0U || host_length == 64U ||
        path_length == 0U || path_length == 160U) {
        return Status::InvalidArgument;
    }

    IPv4Address address{};
    Status status = resolve_a(host_name, &address);
    if (status != Status::Ok) return status;

    const uint16_t source_port = next_ephemeral_port();
    const uint32_t initial_sequence = UINT32_C(0x4b570001) + source_port;
    TcpSegment stale{};
    while (take_tcp_segment(&g_stack, &stale) == Status::Ok) {}

    bool syn_sent = false;
    for (size_t attempt = 0U; attempt < 200000U; ++attempt) {
        status = send_tcp(
            &g_stack, address, source_port, 80U,
            initial_sequence, 0U, TcpSyn, UINT16_C(32768), nullptr, 0U);
        if (status == Status::Ok) {
            syn_sent = true;
            break;
        }
        if (status != Status::NeighborResolutionPending) return status;
        size_t processed = 0U;
        status = net::poll(&g_stack, 1U, &processed);
        if (!acceptable_poll_status(status)) return status;
        __asm__ volatile("pause");
    }
    if (!syn_sent) return Status::WouldBlock;

    TcpSegment handshake{};
    bool established = false;
    for (size_t attempt = 0U; attempt < 500000U; ++attempt) {
        size_t processed = 0U;
        status = net::poll(&g_stack, 1U, &processed);
        if (!acceptable_poll_status(status)) return status;
        if (take_tcp_segment(&g_stack, &handshake) == Status::Ok &&
            ipv4_equal(handshake.source, address) && handshake.source_port == 80U &&
            handshake.destination_port == source_port) {
            if ((handshake.flags & TcpRst) != 0U) return Status::InterfaceError;
            if ((handshake.flags & (TcpSyn | TcpAck)) == (TcpSyn | TcpAck) &&
                handshake.acknowledgement == initial_sequence + 1U) {
                established = true;
                break;
            }
        }
        __asm__ volatile("pause");
    }
    if (!established) return Status::WouldBlock;

    const uint32_t client_sequence = initial_sequence + 1U;
    uint32_t receive_next = handshake.sequence + 1U;
    status = send_tcp(
        &g_stack, address, source_port, 80U,
        client_sequence, receive_next, TcpAck,
        UINT16_C(32768), nullptr, 0U);
    if (status != Status::Ok) return status;

    uint8_t request[512]{};
    size_t request_length = 0U;
    if (!append_request_text(request, sizeof(request), &request_length, "GET ") ||
        !append_request_text(request, sizeof(request), &request_length, path) ||
        !append_request_text(request, sizeof(request), &request_length,
            " HTTP/1.0\r\nHost: ") ||
        !append_request_text(request, sizeof(request), &request_length, host_name) ||
        !append_request_text(request, sizeof(request), &request_length,
            "\r\nUser-Agent: KuroganeWeb/0.1\r\n"
            "Accept: text/html,text/plain,*/*\r\nConnection: close\r\n\r\n")) {
        return Status::PayloadTooLarge;
    }

    status = send_tcp(
        &g_stack, address, source_port, 80U,
        client_sequence, receive_next, TcpPsh | TcpAck,
        UINT16_C(32768), request, request_length);
    if (status != Status::Ok) return status;
    const uint32_t request_end = client_sequence +
        static_cast<uint32_t>(request_length);

    size_t written = 0U;
    bool received_any = false;
    for (size_t attempt = 0U; attempt < 1200000U; ++attempt) {
        size_t processed = 0U;
        status = net::poll(&g_stack, 1U, &processed);
        if (!acceptable_poll_status(status)) return status;

        TcpSegment segment{};
        if (take_tcp_segment(&g_stack, &segment) != Status::Ok ||
            !ipv4_equal(segment.source, address) || segment.source_port != 80U ||
            segment.destination_port != source_port) {
            __asm__ volatile("pause");
            continue;
        }
        if ((segment.flags & TcpRst) != 0U) return Status::InterfaceError;
        if ((segment.flags & TcpAck) != 0U &&
            segment.acknowledgement < request_end) {
            continue;
        }

        if (segment.payload_length != 0U) {
            if (segment.sequence != receive_next) {
                // Duplicate data is harmless; genuinely out-of-order delivery
                // is outside the intentionally small 3.3.3 HTTP transport.
                if (segment.sequence < receive_next) continue;
                return Status::WouldBlock;
            }
            received_any = true;
            const size_t remaining = output_capacity - written;
            const size_t copy_length = segment.payload_length < remaining
                ? segment.payload_length : remaining;
            for (size_t index = 0U; index < copy_length; ++index) {
                output[written + index] = segment.payload[index];
            }
            written += copy_length;
            receive_next += static_cast<uint32_t>(segment.payload_length);
            if (*out_http_status == 0U) {
                *out_http_status = parse_http_status(output, written);
            }
        }
        if ((segment.flags & TcpFin) != 0U) ++receive_next;

        static_cast<void>(send_tcp(
            &g_stack, address, source_port, 80U,
            request_end, receive_next, TcpAck,
            UINT16_C(32768), nullptr, 0U));

        if ((segment.flags & TcpFin) != 0U || written == output_capacity) {
            *out_length = written;
            return received_any ? Status::Ok : Status::WouldBlock;
        }
        __asm__ volatile("pause");
    }

    *out_length = written;
    return received_any ? Status::Ok : Status::WouldBlock;
}

Status stats(NetworkStats* output) { return get_stats(&g_stack, output); }

const IPv4Config* configuration() { return g_ready ? &g_stack.config : nullptr; }

const IPv4Address* dns_server() {
    return g_ready && g_dhcp ? &g_lease.dns_server : nullptr;
}

uint32_t lease_seconds() { return g_ready && g_dhcp ? g_lease.lease_seconds : 0U; }

Status list_neighbors(NeighborCallback callback, void* context) {
    if (!g_ready) return Status::NotInitialized;
    return net::list_neighbors(&g_stack, callback, context);
}

bool physical_interface() { return g_ready && g_physical; }
bool physical_device_detected() { return g_physical_detected; }
Status physical_status() { return g_physical_status; }
bool dhcp_configured() { return g_ready && g_dhcp; }
const char* interface_name() { return g_physical ? physical::name() : "loopback"; }

} // namespace net::service
