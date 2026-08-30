from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: anchor count={count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    'sdk/include/kurogane/syscall.h',
    '    KU_SYS_SOCKET_RECEIVE = 61,\n    KU_SYS_SOCKET_CLOSE = 62\n};',
    '    KU_SYS_SOCKET_RECEIVE = 61,\n'
    '    KU_SYS_SOCKET_CLOSE = 62,\n'
    '    KU_SYS_SOCKET_POLL = 63\n};',
)

replace_once(
    'sdk/include/kurogane/network.h',
    '''enum ku_socket_protocol {
    KU_SOCKET_PROTOCOL_TCP = 6,
    KU_SOCKET_PROTOCOL_UDP = 17
};
''',
    '''enum ku_socket_protocol {
    KU_SOCKET_PROTOCOL_TCP = 6,
    KU_SOCKET_PROTOCOL_UDP = 17
};

enum ku_socket_ready_flags {
    KU_SOCKET_READY_NONE = 0,
    KU_SOCKET_READY_READ = UINT32_C(1) << 0,
    KU_SOCKET_READY_WRITE = UINT32_C(1) << 1,
    KU_SOCKET_READY_CONNECTED = UINT32_C(1) << 2,
    KU_SOCKET_READY_HANGUP = UINT32_C(1) << 3,
    KU_SOCKET_READY_ERROR = UINT32_C(1) << 4
};

#define KU_SOCKET_READY_ALL ( \
    KU_SOCKET_READY_READ | KU_SOCKET_READY_WRITE | \
    KU_SOCKET_READY_CONNECTED | KU_SOCKET_READY_HANGUP | KU_SOCKET_READY_ERROR)
''',
)

replace_once(
    'sdk/include/kurogane/network.h',
    '''static inline ku_status_t ku_socket_close(ku_socket_t socket) {
    return (ku_status_t)ku_syscall3(KU_SYS_SOCKET_CLOSE, socket, 0U, 0U);
}
''',
    '''static inline ku_status_t ku_socket_close(ku_socket_t socket) {
    return (ku_status_t)ku_syscall3(KU_SYS_SOCKET_CLOSE, socket, 0U, 0U);
}

static inline ku_status_t ku_socket_poll(
    ku_socket_t socket,
    uint32_t requested,
    uint32_t* ready) {
    if (socket == KU_SOCKET_INVALID || ready == NULL || requested == 0U ||
        (requested & ~KU_SOCKET_READY_ALL) != 0U) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    *ready = KU_SOCKET_READY_NONE;
    return (ku_status_t)ku_syscall3(
        KU_SYS_SOCKET_POLL,
        socket,
        requested,
        (uint64_t)(uintptr_t)ready);
}

/*
 * Scheduler-friendly readiness wait. Socket operations remain non-blocking;
 * callers that want to wait sleep between readiness probes instead of spinning.
 * timeout_ticks == 0 performs a single probe. UINT64_MAX waits indefinitely.
 */
static inline ku_status_t ku_socket_wait(
    ku_socket_t socket,
    uint32_t requested,
    uint64_t timeout_ticks,
    uint32_t* ready) {
    uint64_t elapsed = 0U;
    for (;;) {
        ku_status_t status = ku_socket_poll(socket, requested, ready);
        if (status != KU_STATUS_OK) return status;
        if ((*ready & requested) != 0U) return KU_STATUS_OK;
        if (timeout_ticks == 0U || elapsed >= timeout_ticks) {
            return KU_STATUS_TIMED_OUT;
        }
        status = ku_sleep(1U);
        if (status != KU_STATUS_OK) return status;
        if (elapsed != UINT64_MAX) ++elapsed;
    }
}
''',
)

replace_once(
    'kernel/net/socket.hpp',
    '''enum class Protocol : uint8_t {
    Udp = 17U,
    Tcp = 6U,
};
''',
    '''enum class Protocol : uint8_t {
    Udp = 17U,
    Tcp = 6U,
};

enum ReadyFlags : uint32_t {
    ReadyNone = 0U,
    ReadyRead = UINT32_C(1) << 0,
    ReadyWrite = UINT32_C(1) << 1,
    ReadyConnected = UINT32_C(1) << 2,
    ReadyHangup = UINT32_C(1) << 3,
    ReadyError = UINT32_C(1) << 4,
    ReadyAll = ReadyRead | ReadyWrite | ReadyConnected | ReadyHangup | ReadyError,
};
''',
)

replace_once(
    'kernel/net/socket.hpp',
    '''Status close(ProcessId owner, Handle handle);
void release_process(ProcessId owner);
Status pump(size_t budget, size_t* out_routed = nullptr);
''',
    '''Status close(ProcessId owner, Handle handle);
Status readiness(
    ProcessId owner,
    Handle handle,
    uint32_t requested,
    uint32_t* out_ready);
void release_process(ProcessId owner);
Status pump(size_t budget, size_t* out_routed = nullptr);
''',
)

socket_cpp = Path('kernel/net/socket.cpp')
text = socket_cpp.read_text()
anchor = '''Status close(ProcessId owner, Handle handle) {
'''
start = text.find(anchor)
if start < 0:
    raise SystemExit('socket close anchor missing')
release_anchor = '\nvoid release_process(ProcessId owner) {'
end = text.find(release_anchor, start)
if end < 0:
    raise SystemExit('socket release_process anchor missing')
readiness = r'''

Status readiness(
    ProcessId owner,
    Handle handle,
    uint32_t requested,
    uint32_t* out_ready) {
    if (out_ready != nullptr) *out_ready = ReadyNone;
    if (!g_initialized) return Status::NotInitialized;
    if (out_ready == nullptr || requested == 0U ||
        (requested & ~static_cast<uint32_t>(ReadyAll)) != 0U) {
        return Status::InvalidArgument;
    }
    Status failure = Status::Ok;
    Slot* slot = resolve(owner, handle, &failure);
    if (slot == nullptr) return failure;

    uint32_t ready = ReadyNone;
    if (slot->protocol == Protocol::Udp && slot->type == Type::Datagram) {
        if ((requested & ReadyRead) != 0U && slot->rx_count == 0U) {
            const Status pump_status = pump(8U, nullptr);
            if (pump_status != Status::Ok && pump_status != Status::WouldBlock) {
                return pump_status;
            }
        }
        if (slot->rx_count != 0U) ready |= ReadyRead;
        if (slot->bound && slot->connected) {
            ready |= ReadyWrite;
            ready |= ReadyConnected;
        }
        *out_ready = ready & requested;
        return Status::Ok;
    }

    if (slot->protocol != Protocol::Tcp || slot->type != Type::Stream) {
        return Status::NotSupported;
    }
    if (slot->tcp_session >= MAX_TCP_SESSIONS) {
        *out_ready = ReadyNone;
        return Status::Ok;
    }

    TcpSession& session = g_tcp_sessions[slot->tcp_session];
    const net::Status progress_status =
        g_backend.tcp_progress(g_backend.context, &session.client);
    slot->connected = session.client.connected;
    if (slot->connected) {
        ready |= ReadyConnected;
        if (session.client.state == tcp_client::State::Established) ready |= ReadyWrite;
    }
    if (session.client.pending_length != 0U || session.client.peer_closed) {
        ready |= ReadyRead;
    }
    if (session.client.peer_closed ||
        session.client.state == tcp_client::State::CloseWait ||
        session.client.state == tcp_client::State::Closed) {
        ready |= ReadyHangup;
    }
    if (session.client.state == tcp_client::State::Reset ||
        session.client.state == tcp_client::State::Error) {
        ready |= ReadyError;
    }
    const Status progress = transport_status(progress_status);
    if (progress != Status::Ok && progress != Status::WouldBlock &&
        (ready & ReadyError) == 0U) {
        return progress;
    }
    *out_ready = ready & requested;
    return Status::Ok;
}
'''
text = text[:end] + readiness + text[end:]
socket_cpp.write_text(text)

replace_once(
    'kernel/user/runtime.cpp',
    '        (number >= KU_SYS_SOCKET_CREATE && number <= KU_SYS_SOCKET_CLOSE);',
    '        (number >= KU_SYS_SOCKET_CREATE && number <= KU_SYS_SOCKET_POLL);',
)

runtime = Path('kernel/user/runtime.cpp')
text = runtime.read_text()
anchor = '''        case KU_SYS_SOCKET_CLOSE: {
            if (frame.rsi != 0U || frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            frame.rax = static_cast<uint64_t>(socket_status(net::socket::close(
                context->pid, static_cast<net::socket::Handle>(frame.rdi))));
            return;
        }
'''
addition = anchor + '''        case KU_SYS_SOCKET_POLL: {
            if (frame.rsi == 0U ||
                (frame.rsi & ~static_cast<uint64_t>(KU_SOCKET_READY_ALL)) != 0U ||
                !validate_user_buffer(*context, frame.rdx, sizeof(uint32_t), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* ready = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(frame.rdx));
            uint32_t ready_value = 0U;
            const net::socket::Status status = net::socket::readiness(
                context->pid,
                static_cast<net::socket::Handle>(frame.rdi),
                static_cast<uint32_t>(frame.rsi),
                &ready_value);
            if (status == net::socket::Status::Ok) *ready = ready_value;
            frame.rax = static_cast<uint64_t>(socket_status(status));
            return;
        }
'''
if text.count(anchor) != 1:
    raise SystemExit(f'runtime socket close anchor count={text.count(anchor)}')
runtime.write_text(text.replace(anchor, addition, 1))

replace_once(
    'TODO-DEFERRED-TESTS.md',
    '- Process-owned TCP socket pool regressions: session exhaustion, PID ownership, protocol-specific bind collisions, async connect retries, partial send accounting, receive EOF, graceful close retry, process-exit cleanup, stale handles.\n',
    '- Process-owned TCP socket pool regressions: session exhaustion, PID ownership, protocol-specific bind collisions, async connect retries, partial send accounting, receive EOF, graceful close retry, process-exit cleanup, stale handles.\n'
    '- Socket readiness regressions: UDP queue/read/write/connect flags, TCP connect/read/write/hangup/error transitions, stale/PID ownership, timeout sleeping behavior; replace tick-probe wait with direct scheduler object wake when waitable-I/O plumbing is available.\n',
)

print('socket readiness ABI applied')
