#include "service.hpp"

#include "physical.hpp"
#include "dhcp.hpp"
#include "protocols.hpp"
#include "tcp_client.hpp"
#include "tls/client.hpp"
#include "../arch/x86_64/io.hpp"
#include "../core/log.hpp"
#include "../drivers/pit.hpp"
#include "../fs/root_volume.hpp"

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
constexpr char kHttpsTransportTag[] = "~tls~";

constexpr uint64_t kArpTimeoutMs = UINT64_C(3000);
constexpr uint64_t kPingTimeoutMs = UINT64_C(3000);
constexpr uint64_t kDnsSendTimeoutMs = UINT64_C(3000);
constexpr uint64_t kDnsReplyTimeoutMs = UINT64_C(5000);
constexpr uint64_t kTcpTimeoutMs = UINT64_C(7000);
constexpr uint64_t kHttpReceiveTimeoutMs = UINT64_C(12000);
// Host public-Web-PKI stores can exceed 512 KiB even after filtering to
// current TLS server-auth CA roots. Keep a hard 2 MiB upper bound so the
// complete generated bundle fits without silently truncating trust anchors.
// This buffer lives in kernel BSS; Mbed TLS uses the kernel heap separately
// for its parsed X.509 structures.
constexpr size_t kTrustStoreCapacity = 2U * 1024U * 1024U;
constexpr char kTrustStorePath[] = "/etc/ssl/certs.pem";

uint8_t g_trust_store[kTrustStoreCapacity]{};
size_t g_trust_store_size = 0U;

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

uint64_t timeout_ticks(uint64_t milliseconds) {
    if (!drivers::pit::initialized()) return 0U;
    const uint64_t frequency = drivers::pit::frequency_hz();
    if (frequency == 0U) return 0U;
    if (milliseconds > (UINT64_MAX - UINT64_C(999)) / frequency) {
        return UINT64_MAX;
    }
    const uint64_t product = milliseconds * frequency;
    const uint64_t ticks = (product + UINT64_C(999)) / UINT64_C(1000);
    return ticks == 0U ? 1U : ticks;
}

bool wait_window_open(
    uint64_t started,
    uint64_t timeout_ms,
    size_t attempt,
    size_t fallback_budget) {
    const uint64_t limit = timeout_ticks(timeout_ms);
    if (limit != 0U && limit != UINT64_MAX) {
        return drivers::pit::ticks() - started < limit;
    }
    return attempt < fallback_budget;
}

void wait_for_transport_progress() {
#if defined(KUROGANE_HOST_TEST)
    arch::pause();
#else
    const bool interrupts_enabled =
        (arch::read_flags() & (UINT64_C(1) << 9U)) != 0U;
    if (drivers::pit::initialized() && interrupts_enabled) {
        arch::halt();
    } else {
        arch::pause();
    }
#endif
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
    Status status = send_ping(
        &g_stack,
        destination,
        UINT16_C(0x4B4F),
        sequence,
        kPingPayload,
        sizeof(kPingPayload));
    if (status == Status::NeighborResolutionPending) {
        bool resolved = false;
        const uint64_t started = drivers::pit::ticks();
        for (size_t attempt = 0U;
             wait_window_open(started, kArpTimeoutMs, attempt, 200000U);
             ++attempt) {
            size_t processed = 0U;
            status = net::poll(&g_stack, 8U, &processed);
            if (!acceptable_poll_status(status)) return status;
            NeighborEntry neighbor{};
            if (lookup_neighbor(&g_stack, destination, &neighbor) == Status::Ok) {
                resolved = true;
                break;
            }
            wait_for_transport_progress();
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

    const uint64_t started = drivers::pit::ticks();
    for (size_t attempt = 0U;
         wait_window_open(started, kPingTimeoutMs, attempt, 200000U);
         ++attempt) {
        size_t processed = 0U;
        status = net::poll(&g_stack, 8U, &processed);
        if (!acceptable_poll_status(status)) return status;
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
        wait_for_transport_progress();
    }
    return Status::WouldBlock;
}

bool append_request_text(
    uint8_t* output,
    size_t capacity,
    size_t* cursor,
    const char* text) {
    if (output == nullptr || cursor == nullptr || text == nullptr ||
        *cursor > capacity) {
        return false;
    }
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (*cursor >= capacity) return false;
        output[(*cursor)++] = static_cast<uint8_t>(text[index]);
    }
    return true;
}

uint16_t parse_http_status(const uint8_t* bytes, size_t length) {
    if (bytes == nullptr || length < 12U || bytes[0] != 'H' || bytes[1] != 'T' ||
        bytes[2] != 'T' || bytes[3] != 'P' || bytes[4] != '/') {
        return 0U;
    }
    size_t index = 5U;
    while (index < length && bytes[index] != ' ') ++index;
    while (index < length && bytes[index] == ' ') ++index;
    if (index + 2U >= length || bytes[index] < '0' || bytes[index] > '9' ||
        bytes[index + 1U] < '0' || bytes[index + 1U] > '9' ||
        bytes[index + 2U] < '0' || bytes[index + 2U] > '9') {
        return 0U;
    }
    return static_cast<uint16_t>(
        static_cast<uint16_t>(bytes[index] - '0') * 100U +
        static_cast<uint16_t>(bytes[index + 1U] - '0') * 10U +
        static_cast<uint16_t>(bytes[index + 2U] - '0'));
}

bool valid_web_target(const char* host_name, const char* path) {
    if (host_name == nullptr || path == nullptr || path[0] != '/') return false;
    size_t host_length = 0U;
    while (host_length < 64U && host_name[host_length] != '\0') ++host_length;
    size_t path_length = 0U;
    while (path_length < 160U && path[path_length] != '\0') ++path_length;
    return host_length != 0U && host_length < 64U &&
        path_length != 0U && path_length < 160U;
}

bool has_https_transport_tag(const char* host_name) {
    if (host_name == nullptr) return false;
    for (size_t index = 0U; index < sizeof(kHttpsTransportTag) - 1U; ++index) {
        if (host_name[index] != kHttpsTransportTag[index]) return false;
    }
    return true;
}

Status map_tls_status(tls::Status status) {
    switch (status) {
        case tls::Status::Ok: return Status::Ok;
        case tls::Status::InvalidArgument: return Status::InvalidArgument;
        case tls::Status::ResponseTooLarge: return Status::BufferTooSmall;
        case tls::Status::Timeout: return Status::WouldBlock;
        case tls::Status::EntropyUnavailable:
        case tls::Status::TrustStoreInvalid: return Status::NotConfigured;
        case tls::Status::TcpFailure:
        case tls::Status::SetupFailure:
        case tls::Status::HandshakeFailure:
        case tls::Status::CertificateFailure:
        case tls::Status::CertificateTimeFailure:
        case tls::Status::IoFailure: return Status::InterfaceError;
    }
    return Status::InterfaceError;
}

Status load_trust_store() {
    if (g_trust_store_size != 0U) return Status::Ok;
    // Do not negatively cache a transient early-boot read failure. GUI
    // applications may reach the network service while the root volume is
    // still becoming available; a later HTTPS request must be allowed to retry.
    if (!fs::root_volume::mounted()) {
        log::write(log::Level::Warn, "TLS", "trust store unavailable: root volume not mounted");
        return Status::NotConfigured;
    }

    uint64_t file_size = 0U;
    size_t bytes_read = 0U;
    const fs::vfs::Status read_status = fs::root_volume::read_file(
        kTrustStorePath,
        g_trust_store,
        sizeof(g_trust_store) - 1U,
        &bytes_read,
        &file_size);
    if (read_status != fs::vfs::Status::Ok || bytes_read == 0U ||
        file_size != static_cast<uint64_t>(bytes_read) ||
        bytes_read >= sizeof(g_trust_store)) {
        log::write_u64(
            log::Level::Error,
            "TLS",
            "trust store VFS status=",
            static_cast<uint64_t>(read_status));
        log::write_u64(log::Level::Error, "TLS", "trust store bytes=", bytes_read);
        log::write_u64(log::Level::Error, "TLS", "trust store file size=", file_size);
        return Status::NotConfigured;
    }
    g_trust_store[bytes_read] = '\0';
    g_trust_store_size = bytes_read + 1U;
    log::write_u64(log::Level::Info, "TLS", "trust store loaded bytes=", bytes_read);
    return Status::Ok;
}

Status http_request_over_tcp(
    const IPv4Address& address,
    const char* host_name,
    const char* path,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length,
    uint16_t* out_http_status) {
    tcp_client::Client client{};
    const uint16_t source_port = next_ephemeral_port();
    const uint32_t initial_sequence = UINT32_C(0x4b570001) + source_port;
    Status status = tcp_client::connect(
        &client,
        &g_stack,
        address,
        source_port,
        80U,
        initial_sequence,
        kTcpTimeoutMs);
    if (status != Status::Ok) return status;

    uint8_t request[768]{};
    size_t request_length = 0U;
    if (!append_request_text(request, sizeof(request), &request_length, "GET ") ||
        !append_request_text(request, sizeof(request), &request_length, path) ||
        !append_request_text(request, sizeof(request), &request_length,
            " HTTP/1.1\r\nHost: ") ||
        !append_request_text(request, sizeof(request), &request_length, host_name) ||
        !append_request_text(request, sizeof(request), &request_length,
            "\r\nUser-Agent: KuroganeWeb/0.3\r\n"
            "Accept: text/html,text/plain,*/*\r\nConnection: close\r\n\r\n")) {
        static_cast<void>(tcp_client::close(&client));
        return Status::PayloadTooLarge;
    }

    status = tcp_client::send(&client, request, request_length, kTcpTimeoutMs);
    if (status != Status::Ok) {
        static_cast<void>(tcp_client::close(&client));
        return status;
    }

    size_t written = 0U;
    for (;;) {
        if (written == output_capacity) {
            static_cast<void>(tcp_client::close(&client));
            *out_length = written;
            return Status::BufferTooSmall;
        }
        size_t received = 0U;
        status = tcp_client::receive(
            &client,
            output + written,
            output_capacity - written,
            &received,
            kHttpReceiveTimeoutMs);
        if (status != Status::Ok) {
            const bool keep_partial_response =
                status == Status::WouldBlock && written != 0U &&
                *out_http_status != 0U;
            static_cast<void>(tcp_client::close(&client));
            *out_length = written;
            if (keep_partial_response) {
                log::write_u64(
                    log::Level::Warn,
                    "HTTP",
                    "peer close timed out; keeping received bytes=",
                    static_cast<uint64_t>(written));
                // The browser already treats BufferTooSmall as a bounded
                // partial-response success. Reuse that contract instead of
                // discarding a valid response solely because FIN was delayed.
                return Status::BufferTooSmall;
            }
            return status;
        }
        if (received == 0U) break;
        written += received;
        if (*out_http_status == 0U) {
            *out_http_status = parse_http_status(output, written);
        }
    }
    static_cast<void>(tcp_client::close(&client));
    *out_length = written;
    return written == 0U ? Status::WouldBlock : Status::Ok;
}

} // namespace

Status initialize() {
    g_ready = false;
    g_physical = false;
    g_dhcp = false;
    g_physical_detected = false;
    g_physical_status = Status::NotInitialized;
    g_lease = {};
    g_trust_store_size = 0U;
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
        if (processed != nullptr) *processed = 0U;
        return Status::NotInitialized;
    }
    return net::poll(&g_stack, budget, processed);
}

Status socket_send_udp(
    const IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    size_t payload_length) {
    if (!g_ready) return Status::NotInitialized;
    return net::send_udp(
        &g_stack, destination, source_port, destination_port, payload, payload_length);
}

Status socket_take_udp(UdpDatagram* datagram) {
    if (!g_ready) return Status::NotInitialized;
    return net::take_udp_datagram(&g_stack, datagram);
}

Status ping_loopback(uint16_t sequence, PingReply* reply) {
    if (!g_ready) return Status::NotInitialized;
    Status status = send_ping(
        &g_stack, kLoopbackIp, UINT16_C(0x4B4F), sequence,
        kPingPayload, sizeof(kPingPayload));
    if (status != Status::Ok) return status;
    size_t processed = 0U;
    status = net::poll(&g_stack, 4U, &processed);
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
    if (reply != nullptr) *reply = result;
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
    if (name == nullptr || out_address == nullptr || name[0] == '\0') {
        return Status::InvalidArgument;
    }
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
    bool sent = false;
    const uint64_t send_started = drivers::pit::ticks();
    for (size_t attempt = 0U;
         wait_window_open(send_started, kDnsSendTimeoutMs, attempt, 200000U);
         ++attempt) {
        status = send_udp(
            &g_stack,
            g_lease.dns_server,
            source_port,
            53U,
            query,
            query_length);
        if (status == Status::Ok) {
            sent = true;
            break;
        }
        if (status != Status::NeighborResolutionPending) return status;
        size_t processed = 0U;
        status = net::poll(&g_stack, 8U, &processed);
        if (!acceptable_poll_status(status)) return status;
        wait_for_transport_progress();
    }
    if (!sent) return Status::WouldBlock;

    const uint64_t reply_started = drivers::pit::ticks();
    for (size_t attempt = 0U;
         wait_window_open(reply_started, kDnsReplyTimeoutMs, attempt, 400000U);
         ++attempt) {
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
                datagram.payload,
                datagram.payload_length,
                transaction,
                &answer);
            if (status == Status::Ok) {
                *out_address = answer.address;
                return Status::Ok;
            }
            if (status != Status::NotForUs) return status;
        }
        wait_for_transport_progress();
    }
    return Status::WouldBlock;
}

Status tcp_connect_probe(
    const IPv4Address& address,
    uint16_t port,
    const char* host_name) {
    if (!g_ready) return Status::NotInitialized;
    if (!g_physical || !g_dhcp) return Status::NotConfigured;
    if (port == 0U || host_name == nullptr || host_name[0] == '\0') {
        return Status::InvalidArgument;
    }

    tcp_client::Client client{};
    const uint16_t source_port = next_ephemeral_port();
    Status status = tcp_client::connect(
        &client,
        &g_stack,
        address,
        source_port,
        port,
        UINT32_C(0x4b550001) + source_port,
        kTcpTimeoutMs);
    if (status != Status::Ok) return status;

    uint8_t request[256]{};
    size_t request_length = 0U;
    if (!append_request_text(request, sizeof(request), &request_length,
            "HEAD / HTTP/1.1\r\nHost: ") ||
        !append_request_text(request, sizeof(request), &request_length, host_name) ||
        !append_request_text(request, sizeof(request), &request_length,
            "\r\nConnection: close\r\n\r\n")) {
        static_cast<void>(tcp_client::close(&client));
        return Status::PayloadTooLarge;
    }
    status = tcp_client::send(&client, request, request_length, kTcpTimeoutMs);
    if (status != Status::Ok) {
        static_cast<void>(tcp_client::close(&client));
        return status;
    }
    uint8_t response[64]{};
    size_t received = 0U;
    status = tcp_client::receive(
        &client, response, sizeof(response), &received, kTcpTimeoutMs);
    static_cast<void>(tcp_client::close(&client));
    return status == Status::Ok && received != 0U ? Status::Ok : status;
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
    if (host_name == nullptr) return Status::InvalidArgument;

    const bool secure = has_https_transport_tag(host_name);
    const char* effective_host = secure
        ? host_name + (sizeof(kHttpsTransportTag) - 1U)
        : host_name;
    if (!valid_web_target(effective_host, path) || output == nullptr ||
        output_capacity == 0U || out_length == nullptr ||
        out_http_status == nullptr) {
        return Status::InvalidArgument;
    }
    if (secure) {
        return https_get(
            effective_host,
            path,
            output,
            output_capacity,
            out_length,
            out_http_status);
    }

    IPv4Address address{};
    const Status resolve_status = resolve_a(effective_host, &address);
    if (resolve_status != Status::Ok) {
        log::write_u64(
            log::Level::Error,
            "HTTP",
            "DNS A resolution failed status=",
            static_cast<uint64_t>(resolve_status));
        return resolve_status;
    }
    const Status request_status = http_request_over_tcp(
        address,
        effective_host,
        path,
        output,
        output_capacity,
        out_length,
        out_http_status);
    if (request_status != Status::Ok && request_status != Status::BufferTooSmall) {
        log::write_u64(
            log::Level::Error,
            "HTTP",
            "TCP/HTTP request failed status=",
            static_cast<uint64_t>(request_status));
    }
    return request_status;
}

Status https_get(
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
    if (!valid_web_target(host_name, path) || output == nullptr ||
        output_capacity == 0U || out_length == nullptr ||
        out_http_status == nullptr) {
        return Status::InvalidArgument;
    }

    const Status trust_status = load_trust_store();
    if (trust_status != Status::Ok) {
        log::write(log::Level::Error, "TLS", "HTTPS blocked: trust store unavailable");
        return trust_status;
    }

    IPv4Address address{};
    const Status resolve_status = resolve_a(host_name, &address);
    if (resolve_status != Status::Ok) {
        log::write_u64(
            log::Level::Error,
            "TLS",
            "HTTPS DNS A resolution failed status=",
            static_cast<uint64_t>(resolve_status));
        return resolve_status;
    }
    const uint16_t source_port = next_ephemeral_port();
    const uint32_t initial_sequence = UINT32_C(0x4b580001) + source_port;
    const tls::Status tls_status = tls::https_get(
        &g_stack,
        address,
        source_port,
        initial_sequence,
        host_name,
        path,
        g_trust_store,
        g_trust_store_size,
        output,
        output_capacity,
        out_length,
        out_http_status);
    if (tls_status != tls::Status::Ok && tls_status != tls::Status::ResponseTooLarge) {
        log::write(log::Level::Error, "TLS", tls::status_message(tls_status));
        log::write_u64(
            log::Level::Error,
            "TLS",
            "TLS status=",
            static_cast<uint64_t>(tls_status));
    }
    return map_tls_status(tls_status);
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