#include "../kernel/net/socket.hpp"

#include <cstdio>

namespace {

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("check failed at line %d: %s\n", __LINE__, #condition); \
            return 1;                                                           \
        }                                                                       \
    } while (false)

struct FakeTransport {
    net::UdpDatagram incoming[8]{};
    size_t incoming_head = 0U;
    size_t incoming_tail = 0U;
    size_t incoming_count = 0U;
    net::IPv4Address sent_destination{};
    uint16_t sent_source_port = 0U;
    uint16_t sent_destination_port = 0U;
    uint8_t sent_payload[net::UDP_MAX_PAYLOAD]{};
    size_t sent_size = 0U;
    bool tcp_establish = false;
    bool tcp_peer_close = false;
    bool tcp_error = false;
};

net::Status fake_send_udp(
    void* opaque,
    const net::IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    size_t payload_length) {
    auto* transport = static_cast<FakeTransport*>(opaque);
    if (transport == nullptr || payload == nullptr || payload_length == 0U ||
        payload_length > sizeof(transport->sent_payload)) {
        return net::Status::InvalidArgument;
    }
    transport->sent_destination = destination;
    transport->sent_source_port = source_port;
    transport->sent_destination_port = destination_port;
    transport->sent_size = payload_length;
    for (size_t index = 0U; index < payload_length; ++index) {
        transport->sent_payload[index] = payload[index];
    }
    return net::Status::Ok;
}

net::Status fake_poll(void* opaque, size_t budget, size_t* processed) {
    auto* transport = static_cast<FakeTransport*>(opaque);
    if (processed != nullptr) {
        *processed = transport != nullptr && transport->incoming_count != 0U && budget != 0U
            ? 1U : 0U;
    }
    return transport == nullptr ? net::Status::InvalidArgument : net::Status::Ok;
}

net::Status fake_take_udp(void* opaque, net::UdpDatagram* datagram) {
    auto* transport = static_cast<FakeTransport*>(opaque);
    if (transport == nullptr || datagram == nullptr) return net::Status::InvalidArgument;
    if (transport->incoming_count == 0U) return net::Status::WouldBlock;
    *datagram = transport->incoming[transport->incoming_tail];
    transport->incoming[transport->incoming_tail] = {};
    transport->incoming_tail = (transport->incoming_tail + 1U) % 8U;
    --transport->incoming_count;
    return net::Status::Ok;
}

net::Status fake_tcp_begin_connect(
    void* opaque,
    net::tcp_client::Client* client,
    const net::IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    uint32_t initial_sequence) {
    auto* transport = static_cast<FakeTransport*>(opaque);
    if (transport == nullptr || client == nullptr || source_port == 0U ||
        destination_port == 0U || initial_sequence == 0U) {
        return net::Status::InvalidArgument;
    }
    client->peer = destination;
    client->local_port = source_port;
    client->remote_port = destination_port;
    client->state = net::tcp_client::State::SynSent;
    client->connected = false;
    return net::Status::WouldBlock;
}

net::Status fake_tcp_progress(void* opaque, net::tcp_client::Client* client) {
    auto* transport = static_cast<FakeTransport*>(opaque);
    if (transport == nullptr || client == nullptr) return net::Status::InvalidArgument;
    if (transport->tcp_error) {
        client->state = net::tcp_client::State::Error;
        client->connected = false;
        return net::Status::InterfaceError;
    }
    if (transport->tcp_peer_close) {
        client->state = net::tcp_client::State::CloseWait;
        client->connected = true;
        client->peer_closed = true;
        return net::Status::Ok;
    }
    if (transport->tcp_establish) {
        client->state = net::tcp_client::State::Established;
        client->connected = true;
        return net::Status::Ok;
    }
    return net::Status::WouldBlock;
}

net::Status fake_tcp_try_send(
    void* opaque,
    net::tcp_client::Client* client,
    const uint8_t* data,
    size_t length,
    size_t* out_sent) {
    auto* transport = static_cast<FakeTransport*>(opaque);
    if (out_sent != nullptr) *out_sent = 0U;
    if (transport == nullptr || client == nullptr || data == nullptr ||
        length == 0U || out_sent == nullptr) return net::Status::InvalidArgument;
    if (!client->connected) return net::Status::WouldBlock;
    *out_sent = length;
    return net::Status::Ok;
}

net::Status fake_tcp_try_receive(
    void* opaque,
    net::tcp_client::Client* client,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    auto* transport = static_cast<FakeTransport*>(opaque);
    if (out_length != nullptr) *out_length = 0U;
    if (transport == nullptr || client == nullptr || output == nullptr ||
        output_capacity == 0U || out_length == nullptr) {
        return net::Status::InvalidArgument;
    }
    return client->peer_closed ? net::Status::Ok : net::Status::WouldBlock;
}

net::Status fake_tcp_begin_close(void* opaque, net::tcp_client::Client* client) {
    if (opaque == nullptr || client == nullptr) return net::Status::InvalidArgument;
    client->state = net::tcp_client::State::Closed;
    client->connected = false;
    return net::Status::Ok;
}

void queue_datagram(
    FakeTransport& transport,
    const net::IPv4Address& source,
    uint16_t source_port,
    const net::IPv4Address& destination,
    uint16_t destination_port,
    const char* payload,
    size_t size) {
    net::UdpDatagram& datagram = transport.incoming[transport.incoming_head];
    datagram = {};
    datagram.valid = true;
    datagram.source = source;
    datagram.destination = destination;
    datagram.source_port = source_port;
    datagram.destination_port = destination_port;
    datagram.payload_length = size;
    for (size_t index = 0U; index < size; ++index) {
        datagram.payload[index] = static_cast<uint8_t>(payload[index]);
    }
    transport.incoming_head = (transport.incoming_head + 1U) % 8U;
    ++transport.incoming_count;
}

bool bytes_equal(const uint8_t* bytes, const char* text, size_t size) {
    for (size_t index = 0U; index < size; ++index) {
        if (bytes[index] != static_cast<uint8_t>(text[index])) return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace net::socket;

    FakeTransport transport{};
    const Backend backend{
        &transport,
        fake_send_udp,
        fake_poll,
        fake_take_udp,
        fake_tcp_begin_connect,
        fake_tcp_progress,
        fake_tcp_try_send,
        fake_tcp_try_receive,
        fake_tcp_begin_close,
    };
    CHECK(initialize(backend) == Status::Ok);
    CHECK(initialize(backend) == Status::AlreadyInitialized);

    constexpr ProcessId owner = 100U;
    constexpr ProcessId other = 200U;
    Handle socket = INVALID_HANDLE;
    CHECK(create(owner, Type::Datagram, Protocol::Udp, &socket) == Status::Ok);
    CHECK(socket != INVALID_HANDLE);
    CHECK(active_count(owner) == 1U);

    Handle unsupported = INVALID_HANDLE;
    CHECK(create(owner, Type::Stream, Protocol::Udp, &unsupported) == Status::NotSupported);
    CHECK(unsupported == INVALID_HANDLE);

    const Endpoint local{{{0U, 0U, 0U, 0U}}, UINT16_C(4242)};
    CHECK(bind(other, socket, local) == Status::AccessDenied);
    CHECK(bind(owner, socket, local) == Status::Ok);

    Handle collision = INVALID_HANDLE;
    CHECK(create(other, Type::Datagram, Protocol::Udp, &collision) == Status::Ok);
    CHECK(bind(other, collision, local) == Status::AddressInUse);

    const net::IPv4Address remote_ip{{10U, 0U, 2U, 2U}};
    const Endpoint remote{remote_ip, UINT16_C(9000)};
    CHECK(connect(owner, socket, remote) == Status::Ok);

    constexpr char request[] = "ping";
    CHECK(send(other, socket, request, sizeof(request) - 1U) == Status::AccessDenied);
    CHECK(send(owner, socket, request, sizeof(request) - 1U) == Status::Ok);
    CHECK(transport.sent_source_port == local.port);
    CHECK(transport.sent_destination_port == remote.port);
    CHECK(transport.sent_size == sizeof(request) - 1U);
    CHECK(bytes_equal(transport.sent_payload, request, transport.sent_size));

    const net::IPv4Address local_ip{{10U, 0U, 2U, 15U}};
    constexpr char response[] = "pong";
    queue_datagram(
        transport, remote_ip, remote.port, local_ip, local.port,
        response, sizeof(response) - 1U);
    uint8_t small[2]{};
    size_t received = 0U;
    Endpoint source{};
    CHECK(receive(owner, socket, small, sizeof(small), &received, &source) ==
        Status::BufferTooSmall);
    CHECK(received == sizeof(response) - 1U);

    uint8_t buffer[16]{};
    received = 0U;
    CHECK(receive(owner, socket, buffer, sizeof(buffer), &received, &source) == Status::Ok);
    CHECK(received == sizeof(response) - 1U);
    CHECK(bytes_equal(buffer, response, received));
    CHECK(source.port == remote.port);
    CHECK(receive(owner, socket, buffer, sizeof(buffer), &received, nullptr) == Status::WouldBlock);

    const Handle stale = socket;
    CHECK(close(owner, socket) == Status::Ok);
    CHECK(close(owner, stale) == Status::StaleHandle);
    Handle replacement = INVALID_HANDLE;
    CHECK(create(owner, Type::Datagram, Protocol::Udp, &replacement) == Status::Ok);
    CHECK(replacement != stale);
    CHECK(close(owner, stale) == Status::StaleHandle);

    Handle auto_bound = INVALID_HANDLE;
    CHECK(create(owner, Type::Datagram, Protocol::Udp, &auto_bound) == Status::Ok);
    CHECK(connect(owner, auto_bound, remote) == Status::Ok);
    CHECK(send(owner, auto_bound, request, sizeof(request) - 1U) == Status::Ok);
    CHECK(transport.sent_source_port >= EPHEMERAL_PORT_FIRST);

    Handle tcp = INVALID_HANDLE;
    CHECK(create(owner, Type::Stream, Protocol::Tcp, &tcp) == Status::Ok);
    CHECK(connect(owner, tcp, remote) == Status::WouldBlock);
    uint32_t tcp_ready = ReadyNone;
    CHECK(readiness(owner, tcp, ReadyWrite | ReadyConnected, &tcp_ready) == Status::Ok);
    CHECK(tcp_ready == ReadyNone);
    transport.tcp_establish = true;
    CHECK(readiness(owner, tcp, ReadyWrite | ReadyConnected, &tcp_ready) == Status::Ok);
    CHECK((tcp_ready & (ReadyWrite | ReadyConnected)) ==
        (ReadyWrite | ReadyConnected));
    transport.tcp_peer_close = true;
    CHECK(readiness(owner, tcp, ReadyRead | ReadyHangup, &tcp_ready) == Status::Ok);
    CHECK((tcp_ready & (ReadyRead | ReadyHangup)) == (ReadyRead | ReadyHangup));
    transport.tcp_peer_close = false;
    transport.tcp_error = true;
    CHECK(readiness(owner, tcp, ReadyError, &tcp_ready) == Status::Ok);
    CHECK((tcp_ready & ReadyError) != 0U);
    transport.tcp_error = false;

    CHECK(active_count(owner) == 3U);
    release_process(owner);
    CHECK(active_count(owner) == 0U);
    CHECK(send(owner, replacement, request, sizeof(request) - 1U) == Status::StaleHandle);
    CHECK(send(owner, auto_bound, request, sizeof(request) - 1U) == Status::StaleHandle);
    CHECK(active_count(other) == 1U);
    CHECK(close(other, collision) == Status::Ok);

    std::puts("socket ownership/core tests passed");
    return 0;
}
