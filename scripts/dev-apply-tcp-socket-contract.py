#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, got {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))


# Public kernel socket contract: bounded async connect and typed terminal states.
replace_once(
    "kernel/net/socket.hpp",
    "constexpr uint16_t EPHEMERAL_PORT_FIRST = UINT16_C(49152);\n",
    "constexpr uint16_t EPHEMERAL_PORT_FIRST = UINT16_C(49152);\n"
    "constexpr uint64_t TCP_CONNECT_TIMEOUT_MS = UINT64_C(7000);\n"
    "constexpr uint64_t TCP_SYN_RETRY_MS = UINT64_C(1750);\n"
    "constexpr uint8_t TCP_CONNECT_TRANSMISSION_LIMIT = 4U;\n",
)
replace_once(
    "kernel/net/socket.hpp",
    "    PayloadTooLarge,\n    TransportError,\n",
    "    PayloadTooLarge,\n"
    "    ConnectionRefused,\n"
    "    ConnectionReset,\n"
    "    TimedOut,\n"
    "    TransportError,\n",
)
replace_once(
    "kernel/net/socket.hpp",
    "    net::Status (*take_udp)(void* context, UdpDatagram* datagram);\n"
    "    net::Status (*tcp_begin_connect)(\n",
    "    net::Status (*take_udp)(void* context, UdpDatagram* datagram);\n"
    "    uint64_t (*monotonic_ms)(void* context);\n"
    "    net::Status (*tcp_begin_connect)(\n",
)

# Socket implementation: retain bounded connect metadata per TCP session.
replace_once(
    "kernel/net/socket.cpp",
    "struct TcpSession {\n"
    "    tcp_client::Client client;\n"
    "    bool active;\n"
    "};\n",
    "struct TcpSession {\n"
    "    tcp_client::Client client;\n"
    "    uint32_t initial_sequence;\n"
    "    uint64_t connect_started_ms;\n"
    "    uint64_t connect_last_transmit_ms;\n"
    "    Status terminal_status;\n"
    "    uint8_t connect_transmissions;\n"
    "    bool was_connected;\n"
    "    bool active;\n"
    "};\n",
)
replace_once(
    "kernel/net/socket.cpp",
    "Status transport_status(net::Status status) {\n",
    "uint64_t monotonic_ms() {\n"
    "    return g_backend.monotonic_ms != nullptr\n"
    "        ? g_backend.monotonic_ms(g_backend.context)\n"
    "        : 0U;\n"
    "}\n\n"
    "Status transport_status(net::Status status) {\n",
)
replace_once(
    "kernel/net/socket.cpp",
    "}\n\n"
    "bool slot_accepts_datagram(\n",
    "}\n\n"
    "Status tcp_progress_status(TcpSession& session, net::Status status) {\n"
    "    if (session.terminal_status != Status::Ok) return session.terminal_status;\n"
    "    if (session.client.state == tcp_client::State::Reset) {\n"
    "        session.terminal_status = session.was_connected\n"
    "            ? Status::ConnectionReset\n"
    "            : Status::ConnectionRefused;\n"
    "        return session.terminal_status;\n"
    "    }\n"
    "    if (session.client.state == tcp_client::State::Error &&\n"
    "        status != net::Status::WouldBlock) {\n"
    "        session.terminal_status = Status::TransportError;\n"
    "        return session.terminal_status;\n"
    "    }\n"
    "    return transport_status(status);\n"
    "}\n\n"
    "Status progress_tcp_connection(Slot& slot, TcpSession& session) {\n"
    "    if (session.terminal_status != Status::Ok) return session.terminal_status;\n"
    "    const net::Status backend_status =\n"
    "        g_backend.tcp_progress(g_backend.context, &session.client);\n"
    "    slot.connected = session.client.connected;\n"
    "    if (slot.connected) session.was_connected = true;\n"
    "    Status mapped = tcp_progress_status(session, backend_status);\n"
    "    if (mapped != Status::Ok && mapped != Status::WouldBlock) return mapped;\n"
    "    if (slot.connected) return Status::Ok;\n"
    "    if (session.client.state != tcp_client::State::SynSent) {\n"
    "        return mapped == Status::Ok ? Status::WouldBlock : mapped;\n"
    "    }\n\n"
    "    const uint64_t now = monotonic_ms();\n"
    "    if (now - session.connect_started_ms >= TCP_CONNECT_TIMEOUT_MS) {\n"
    "        session.client.connected = false;\n"
    "        session.client.state = tcp_client::State::Error;\n"
    "        session.terminal_status = Status::TimedOut;\n"
    "        return session.terminal_status;\n"
    "    }\n"
    "    if (session.connect_transmissions < TCP_CONNECT_TRANSMISSION_LIMIT &&\n"
    "        now - session.connect_last_transmit_ms >= TCP_SYN_RETRY_MS) {\n"
    "        const net::Status retry_status = g_backend.tcp_begin_connect(\n"
    "            g_backend.context,\n"
    "            &session.client,\n"
    "            slot.remote.address,\n"
    "            slot.local.port,\n"
    "            slot.remote.port,\n"
    "            session.initial_sequence);\n"
    "        ++session.connect_transmissions;\n"
    "        session.connect_last_transmit_ms = now;\n"
    "        slot.connected = session.client.connected;\n"
    "        if (slot.connected) session.was_connected = true;\n"
    "        mapped = tcp_progress_status(session, retry_status);\n"
    "        if (mapped != Status::Ok && mapped != Status::WouldBlock) return mapped;\n"
    "        if (slot.connected) return Status::Ok;\n"
    "    }\n"
    "    return Status::WouldBlock;\n"
    "}\n\n"
    "bool slot_accepts_datagram(\n",
)
replace_once(
    "kernel/net/socket.cpp",
    "    if (backend.send_udp == nullptr || backend.poll == nullptr ||\n"
    "        backend.take_udp == nullptr || backend.tcp_begin_connect == nullptr ||\n",
    "    if (backend.send_udp == nullptr || backend.poll == nullptr ||\n"
    "        backend.take_udp == nullptr || backend.monotonic_ms == nullptr ||\n"
    "        backend.tcp_begin_connect == nullptr ||\n",
)

old_existing_connect = '''        TcpSession& session = g_tcp_sessions[slot->tcp_session];
        const net::Status progress_status =
            g_backend.tcp_progress(g_backend.context, &session.client);
        slot->connected = session.client.connected;
        if (slot->connected) return Status::Ok;
        return transport_status(progress_status);
'''
new_existing_connect = '''        TcpSession& session = g_tcp_sessions[slot->tcp_session];
        return progress_tcp_connection(*slot, session);
'''
replace_once("kernel/net/socket.cpp", old_existing_connect, new_existing_connect)

old_begin = '''    slot->tcp_session = session_index;
    slot->remote = endpoint;
    const net::Status begin_status = g_backend.tcp_begin_connect(
        g_backend.context,
        &session->client,
        endpoint.address,
        slot->local.port,
        endpoint.port,
        next_tcp_initial_sequence());
    slot->connected = session->client.connected;
    const Status mapped = transport_status(begin_status);
    if (mapped != Status::Ok && mapped != Status::WouldBlock) {
        release_tcp_session(*slot);
        return mapped;
    }
    return slot->connected ? Status::Ok : Status::WouldBlock;
'''
new_begin = '''    slot->tcp_session = session_index;
    slot->remote = endpoint;
    session->initial_sequence = next_tcp_initial_sequence();
    session->connect_started_ms = monotonic_ms();
    session->connect_last_transmit_ms = session->connect_started_ms;
    session->connect_transmissions = 1U;
    session->terminal_status = Status::Ok;
    session->was_connected = false;
    const net::Status begin_status = g_backend.tcp_begin_connect(
        g_backend.context,
        &session->client,
        endpoint.address,
        slot->local.port,
        endpoint.port,
        session->initial_sequence);
    slot->connected = session->client.connected;
    if (slot->connected) session->was_connected = true;
    const Status mapped = tcp_progress_status(*session, begin_status);
    if (mapped != Status::Ok && mapped != Status::WouldBlock) {
        release_tcp_session(*slot);
        return mapped;
    }
    return slot->connected ? Status::Ok : Status::WouldBlock;
'''
replace_once("kernel/net/socket.cpp", old_begin, new_begin)

old_send_progress = '''    TcpSession& session = g_tcp_sessions[slot->tcp_session];
    if (!slot->connected) {
        const net::Status progress_status =
            g_backend.tcp_progress(g_backend.context, &session.client);
        slot->connected = session.client.connected;
        if (!slot->connected) {
            const Status mapped = transport_status(progress_status);
            return mapped == Status::Ok ? Status::WouldBlock : mapped;
        }
    }
'''
new_send_progress = '''    TcpSession& session = g_tcp_sessions[slot->tcp_session];
    if (!slot->connected) {
        const Status progress_status = progress_tcp_connection(*slot, session);
        if (progress_status != Status::Ok) return progress_status;
    }
'''
# This exact block appears in send and receive; replace both.
p = Path("kernel/net/socket.cpp")
text = p.read_text()
count = text.count(old_send_progress)
if count != 2:
    raise SystemExit(f"kernel/net/socket.cpp: expected two TCP progress blocks, got {count}")
p.write_text(text.replace(old_send_progress, new_send_progress))

# Map terminal reset/error from direct TCP data operations too.
replace_once(
    "kernel/net/socket.cpp",
    "    const Status status = transport_status(g_backend.tcp_try_send(\n"
    "        g_backend.context,\n"
    "        &session.client,\n"
    "        static_cast<const uint8_t*>(data),\n"
    "        size,\n"
    "        &sent));\n",
    "    const Status status = tcp_progress_status(session, g_backend.tcp_try_send(\n"
    "        g_backend.context,\n"
    "        &session.client,\n"
    "        static_cast<const uint8_t*>(data),\n"
    "        size,\n"
    "        &sent));\n",
)
replace_once(
    "kernel/net/socket.cpp",
    "        const Status status = transport_status(g_backend.tcp_try_receive(\n"
    "            g_backend.context,\n"
    "            &session.client,\n"
    "            static_cast<uint8_t*>(output),\n"
    "            capacity,\n"
    "            out_size));\n",
    "        const Status status = tcp_progress_status(session, g_backend.tcp_try_receive(\n"
    "            g_backend.context,\n"
    "            &session.client,\n"
    "            static_cast<uint8_t*>(output),\n"
    "            capacity,\n"
    "            out_size));\n",
)

# Failed/timeout connection handles remain explicitly closable and reclaimable.
replace_once(
    "kernel/net/socket.cpp",
    "    TcpSession& session = g_tcp_sessions[slot->tcp_session];\n"
    "    if (session.client.state == tcp_client::State::Closed) {\n",
    "    TcpSession& session = g_tcp_sessions[slot->tcp_session];\n"
    "    if (session.terminal_status != Status::Ok) {\n"
    "        clear_slot(*slot);\n"
    "        return Status::Ok;\n"
    "    }\n"
    "    if (session.client.state == tcp_client::State::Closed) {\n",
)
replace_once(
    "kernel/net/socket.cpp",
    "    const Status begin_status = transport_status(\n"
    "        g_backend.tcp_begin_close(g_backend.context, &session.client));\n",
    "    const Status begin_status = tcp_progress_status(\n"
    "        session, g_backend.tcp_begin_close(g_backend.context, &session.client));\n",
)
replace_once(
    "kernel/net/socket.cpp",
    "        const Status progress_status = transport_status(\n"
    "            g_backend.tcp_progress(g_backend.context, &session.client));\n",
    "        const Status progress_status = tcp_progress_status(\n"
    "            session, g_backend.tcp_progress(g_backend.context, &session.client));\n",
)

old_ready_progress = '''    TcpSession& session = g_tcp_sessions[slot->tcp_session];
    const net::Status progress_status =
        g_backend.tcp_progress(g_backend.context, &session.client);
    slot->connected = session.client.connected;
'''
new_ready_progress = '''    TcpSession& session = g_tcp_sessions[slot->tcp_session];
    const Status progress = progress_tcp_connection(*slot, session);
'''
replace_once("kernel/net/socket.cpp", old_ready_progress, new_ready_progress)
replace_once(
    "kernel/net/socket.cpp",
    "    const Status progress = transport_status(progress_status);\n"
    "    if (progress != Status::Ok && progress != Status::WouldBlock &&\n",
    "    if (progress != Status::Ok && progress != Status::WouldBlock &&\n",
)
replace_once(
    "kernel/net/socket.cpp",
    "        case Status::PayloadTooLarge: return \"socket payload too large\";\n"
    "        case Status::TransportError: return \"network transport error\";\n",
    "        case Status::PayloadTooLarge: return \"socket payload too large\";\n"
    "        case Status::ConnectionRefused: return \"TCP connection refused\";\n"
    "        case Status::ConnectionReset: return \"TCP connection reset\";\n"
    "        case Status::TimedOut: return \"TCP operation timed out\";\n"
    "        case Status::TransportError: return \"network transport error\";\n",
)

# Stable SDK error codes for applications that need to distinguish TCP outcomes.
replace_once(
    "sdk/include/kurogane/status.h",
    "    KU_STATUS_END_OF_STREAM = -15\n",
    "    KU_STATUS_END_OF_STREAM = -15,\n"
    "    KU_STATUS_CONNECTION_REFUSED = -16,\n"
    "    KU_STATUS_CONNECTION_RESET = -17\n",
)
replace_once(
    "tests/test_sdk_abi.cpp",
    "    static_assert(KU_STATUS_END_OF_STREAM == -15);\n",
    "    static_assert(KU_STATUS_END_OF_STREAM == -15);\n"
    "    static_assert(KU_STATUS_CONNECTION_REFUSED == -16);\n"
    "    static_assert(KU_STATUS_CONNECTION_RESET == -17);\n",
)

# Runtime bridge supplies a monotonic millisecond clock and maps typed statuses.
replace_once(
    "kernel/user/runtime.cpp",
    "#include \"../drivers/core/device_manager.hpp\"\n",
    "#include \"../drivers/core/device_manager.hpp\"\n"
    "#include \"../drivers/pit.hpp\"\n",
)
replace_once(
    "kernel/user/runtime.cpp",
    "        case SocketStatus::PayloadTooLarge: return KU_STATUS_OUT_OF_RANGE;\n"
    "        case SocketStatus::NotInitialized:\n",
    "        case SocketStatus::PayloadTooLarge: return KU_STATUS_OUT_OF_RANGE;\n"
    "        case SocketStatus::ConnectionRefused: return KU_STATUS_CONNECTION_REFUSED;\n"
    "        case SocketStatus::ConnectionReset: return KU_STATUS_CONNECTION_RESET;\n"
    "        case SocketStatus::TimedOut: return KU_STATUS_TIMED_OUT;\n"
    "        case SocketStatus::NotInitialized:\n",
)
replace_once(
    "kernel/user/runtime.cpp",
    "net::Status socket_backend_take_udp(void* context, net::UdpDatagram* datagram) {",
    "net::Status socket_backend_take_udp(void* context, net::UdpDatagram* datagram) {",
) if False else None
replace_once(
    "kernel/user/runtime.cpp",
    "net::Status socket_backend_take_udp(void*, net::UdpDatagram* datagram) {\n"
    "    return net::service::socket_take_udp(datagram);\n"
    "}\n\n"
    "net::Status socket_backend_tcp_begin_connect(\n",
    "net::Status socket_backend_take_udp(void*, net::UdpDatagram* datagram) {\n"
    "    return net::service::socket_take_udp(datagram);\n"
    "}\n\n"
    "uint64_t socket_backend_monotonic_ms(void*) {\n"
    "    if (!drivers::pit::initialized()) return 0U;\n"
    "    const uint64_t frequency = drivers::pit::frequency_hz();\n"
    "    if (frequency == 0U) return 0U;\n"
    "    const uint64_t ticks = drivers::pit::ticks();\n"
    "    const uint64_t seconds = ticks / frequency;\n"
    "    const uint64_t remainder = ticks % frequency;\n"
    "    if (seconds > UINT64_MAX / UINT64_C(1000)) return UINT64_MAX;\n"
    "    return seconds * UINT64_C(1000) +\n"
    "        (remainder * UINT64_C(1000)) / frequency;\n"
    "}\n\n"
    "net::Status socket_backend_tcp_begin_connect(\n",
)
replace_once(
    "kernel/user/runtime.cpp",
    "        socket_backend_take_udp,\n"
    "        socket_backend_tcp_begin_connect,\n",
    "        socket_backend_take_udp,\n"
    "        socket_backend_monotonic_ms,\n"
    "        socket_backend_tcp_begin_connect,\n",
)

# The readiness waiter lives in the base runtime and must preserve typed terminal errors.
replace_once(
    "kernel/user/runtime_base.inc",
    "        case SocketStatus::PayloadTooLarge: return KU_STATUS_OUT_OF_RANGE;\n"
    "        case SocketStatus::NotInitialized:\n",
    "        case SocketStatus::PayloadTooLarge: return KU_STATUS_OUT_OF_RANGE;\n"
    "        case SocketStatus::ConnectionRefused: return KU_STATUS_CONNECTION_REFUSED;\n"
    "        case SocketStatus::ConnectionReset: return KU_STATUS_CONNECTION_RESET;\n"
    "        case SocketStatus::TimedOut: return KU_STATUS_TIMED_OUT;\n"
    "        case SocketStatus::NotInitialized:\n",
)

# Host socket regression: deterministic clock, retries, refusal, timeout, reset.
replace_once(
    "tests/test_socket.cpp",
    "    size_t sent_size = 0U;\n"
    "    bool tcp_establish = false;\n",
    "    size_t sent_size = 0U;\n"
    "    uint64_t now_ms = 1U;\n"
    "    size_t tcp_begin_count = 0U;\n"
    "    bool tcp_establish = false;\n",
)
replace_once(
    "tests/test_socket.cpp",
    "    bool tcp_peer_close = false;\n"
    "    bool tcp_error = false;\n",
    "    bool tcp_peer_close = false;\n"
    "    bool tcp_reset = false;\n"
    "    bool tcp_error = false;\n",
)
replace_once(
    "tests/test_socket.cpp",
    "    client->peer = destination;\n",
    "    ++transport->tcp_begin_count;\n"
    "    client->peer = destination;\n",
)
replace_once(
    "tests/test_socket.cpp",
    "    if (transport->tcp_error) {\n",
    "    if (transport->tcp_reset) {\n"
    "        client->state = net::tcp_client::State::Reset;\n"
    "        client->connected = false;\n"
    "        return net::Status::InterfaceError;\n"
    "    }\n"
    "    if (transport->tcp_error) {\n",
)
replace_once(
    "tests/test_socket.cpp",
    "net::Status fake_tcp_begin_connect(\n",
    "uint64_t fake_monotonic_ms(void* opaque) {\n"
    "    auto* transport = static_cast<FakeTransport*>(opaque);\n"
    "    return transport != nullptr ? transport->now_ms : 0U;\n"
    "}\n\n"
    "net::Status fake_tcp_begin_connect(\n",
)
replace_once(
    "tests/test_socket.cpp",
    "        fake_take_udp,\n"
    "        fake_tcp_begin_connect,\n",
    "        fake_take_udp,\n"
    "        fake_monotonic_ms,\n"
    "        fake_tcp_begin_connect,\n",
)

old_tcp_block = '''    Handle tcp = INVALID_HANDLE;
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
'''
new_tcp_block = '''    Handle tcp = INVALID_HANDLE;
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
    CHECK(close(owner, tcp) == Status::Ok);
    transport.tcp_establish = false;

    Handle refused = INVALID_HANDLE;
    CHECK(create(owner, Type::Stream, Protocol::Tcp, &refused) == Status::Ok);
    CHECK(connect(owner, refused, remote) == Status::WouldBlock);
    transport.tcp_reset = true;
    CHECK(connect(owner, refused, remote) == Status::ConnectionRefused);
    CHECK(readiness(owner, refused, ReadyError, &tcp_ready) == Status::Ok);
    CHECK((tcp_ready & ReadyError) != 0U);
    transport.tcp_reset = false;
    CHECK(close(owner, refused) == Status::Ok);

    Handle timed_out = INVALID_HANDLE;
    CHECK(create(owner, Type::Stream, Protocol::Tcp, &timed_out) == Status::Ok);
    const size_t begins_before_timeout = transport.tcp_begin_count;
    CHECK(connect(owner, timed_out, remote) == Status::WouldBlock);
    transport.now_ms += TCP_SYN_RETRY_MS;
    CHECK(connect(owner, timed_out, remote) == Status::WouldBlock);
    CHECK(transport.tcp_begin_count > begins_before_timeout + 1U);
    transport.now_ms += TCP_CONNECT_TIMEOUT_MS;
    CHECK(connect(owner, timed_out, remote) == Status::TimedOut);
    CHECK(close(owner, timed_out) == Status::Ok);

    Handle reset = INVALID_HANDLE;
    CHECK(create(owner, Type::Stream, Protocol::Tcp, &reset) == Status::Ok);
    CHECK(connect(owner, reset, remote) == Status::WouldBlock);
    transport.tcp_establish = true;
    CHECK(connect(owner, reset, remote) == Status::Ok);
    transport.tcp_establish = false;
    transport.tcp_reset = true;
    CHECK(readiness(owner, reset, ReadyError, &tcp_ready) == Status::Ok);
    CHECK((tcp_ready & ReadyError) != 0U);
    CHECK(send(owner, reset, request, sizeof(request) - 1U) == Status::ConnectionReset);
    transport.tcp_reset = false;
    CHECK(close(owner, reset) == Status::Ok);

    CHECK(active_count(owner) == 2U);
'''
replace_once("tests/test_socket.cpp", old_tcp_block, new_tcp_block)

print("bounded TCP socket contract patch applied")
