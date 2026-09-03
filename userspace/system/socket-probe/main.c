#include "../../runtime/user.h"

#include <kurogane/network.h>

#define SOCKET_ROUNDTRIP_PORT UINT16_C(45350)
#define SOCKET_EXIT_PORT UINT16_C(45351)
#define SOCKET_WAKE_PORT UINT16_C(45352)

#define TCP_HOST_PORT UINT16_C(18080)
#define TCP_REFUSED_PORT UINT16_C(18081)
#define TCP_TIMEOUT_PORT UINT16_C(18082)
#define TCP_RESET_PORT UINT16_C(18083)
#define TCP_LOCAL_CLOSE_PORT UINT16_C(18084)
#define TCP_FAST_WAIT_TICKS UINT64_C(400)
#define TCP_TIMEOUT_WAIT_TICKS UINT64_C(1200)
#define TCP_NETWORK_WAIT_ATTEMPTS 1000U
#define TCP_CLOSE_ATTEMPTS 800U
#define TCP_CLEANUP_WORKERS 6U

static ku_ipv4_endpoint ipv4_endpoint(
    uint8_t a,
    uint8_t b,
    uint8_t c,
    uint8_t d,
    uint16_t port) {
    ku_ipv4_endpoint endpoint;
    endpoint.address[0] = a;
    endpoint.address[1] = b;
    endpoint.address[2] = c;
    endpoint.address[3] = d;
    endpoint.port = port;
    endpoint.reserved = 0U;
    return endpoint;
}

static ku_ipv4_endpoint loopback_endpoint(uint16_t port) {
    return ipv4_endpoint(127U, 0U, 0U, 1U, port);
}

static ku_ipv4_endpoint qemu_host_endpoint(uint16_t port) {
    return ipv4_endpoint(10U, 0U, 2U, 2U, port);
}

static int bytes_equal(const uint8_t* left, const uint8_t* right, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index) {
        if (left[index] != right[index]) return 0;
    }
    return 1;
}

static int is_loopback(const ku_ipv4_endpoint* endpoint) {
    return endpoint != (const ku_ipv4_endpoint*)0 &&
        endpoint->address[0] == 127U && endpoint->address[1] == 0U &&
        endpoint->address[2] == 0U && endpoint->address[3] == 1U;
}

static int wait_network_ready(void) {
    ku_network_status status;
    uint32_t attempt;

    status.structure_size = sizeof(status);
    for (attempt = 0U; attempt < TCP_NETWORK_WAIT_ATTEMPTS; ++attempt) {
        if (ku_network_get_status(&status) == KU_STATUS_OK &&
            status.ready != 0U && status.dhcp != 0U) {
            return 1;
        }
        (void)ku_sleep(1U);
    }
    return 0;
}

static int connect_stream(
    ku_socket_t socket,
    const ku_ipv4_endpoint* endpoint,
    uint64_t timeout_ticks) {
    uint32_t ready = KU_SOCKET_READY_NONE;
    ku_status_t status;

    status = ku_socket_connect(socket, endpoint);
    if (status == KU_STATUS_OK) return 1;
    if (status != KU_STATUS_WOULD_BLOCK) return 0;

    status = ku_socket_wait(
        socket,
        KU_SOCKET_READY_CONNECTED | KU_SOCKET_READY_WRITE |
            KU_SOCKET_READY_ERROR,
        timeout_ticks,
        &ready);
    if (status != KU_STATUS_OK || (ready & KU_SOCKET_READY_ERROR) != 0U) {
        return 0;
    }
    return (ready & (KU_SOCKET_READY_CONNECTED | KU_SOCKET_READY_WRITE)) ==
        (KU_SOCKET_READY_CONNECTED | KU_SOCKET_READY_WRITE);
}

static int send_stream_all(ku_socket_t socket, const uint8_t* data, size_t size) {
    size_t offset = 0U;
    uint32_t attempts = 0U;

    while (offset < size && attempts < 128U) {
        const ku_result_t result = ku_socket_send(socket, data + offset, size - offset);
        if (result > 0) {
            offset += (size_t)result;
            ++attempts;
            continue;
        }
        if (result != KU_STATUS_WOULD_BLOCK) return 0;
        {
            uint32_t ready = KU_SOCKET_READY_NONE;
            if (ku_socket_wait(
                    socket,
                    KU_SOCKET_READY_WRITE | KU_SOCKET_READY_ERROR,
                    TCP_FAST_WAIT_TICKS,
                    &ready) != KU_STATUS_OK ||
                (ready & KU_SOCKET_READY_ERROR) != 0U ||
                (ready & KU_SOCKET_READY_WRITE) == 0U) {
                return 0;
            }
        }
        ++attempts;
    }
    return offset == size;
}

static int receive_stream_exact(
    ku_socket_t socket,
    uint8_t* output,
    size_t size) {
    size_t offset = 0U;
    uint32_t attempts = 0U;

    while (offset < size && attempts < 128U) {
        uint32_t ready = KU_SOCKET_READY_NONE;
        ku_result_t result;
        if (ku_socket_wait(
                socket,
                KU_SOCKET_READY_READ | KU_SOCKET_READY_HANGUP |
                    KU_SOCKET_READY_ERROR,
                TCP_FAST_WAIT_TICKS,
                &ready) != KU_STATUS_OK ||
            (ready & KU_SOCKET_READY_ERROR) != 0U) {
            return 0;
        }
        result = ku_socket_receive(
            socket, output + offset, size - offset, (ku_ipv4_endpoint*)0);
        if (result > 0) {
            offset += (size_t)result;
        } else if (result == 0) {
            return offset == size;
        } else if (result != KU_STATUS_WOULD_BLOCK) {
            return 0;
        }
        ++attempts;
    }
    return offset == size;
}

static int wait_peer_hangup(ku_socket_t socket) {
    uint32_t ready = KU_SOCKET_READY_NONE;
    if (ku_socket_wait(
            socket,
            KU_SOCKET_READY_HANGUP | KU_SOCKET_READY_ERROR,
            TCP_FAST_WAIT_TICKS,
            &ready) != KU_STATUS_OK) {
        return 0;
    }
    return (ready & KU_SOCKET_READY_HANGUP) != 0U &&
        (ready & KU_SOCKET_READY_ERROR) == 0U;
}

static int close_stream(ku_socket_t socket) {
    uint32_t attempt;
    for (attempt = 0U; attempt < TCP_CLOSE_ATTEMPTS; ++attempt) {
        const ku_status_t status = ku_socket_close(socket);
        if (status == KU_STATUS_OK) return 1;
        if (status != KU_STATUS_WOULD_BLOCK) return 0;
        (void)ku_sleep(1U);
    }
    return 0;
}

static int qualify_udp_roundtrip(void) {
    static const uint8_t payload[] = {'k', 'u', 'r', 'o', '-', '3', '.', '5'};
    uint8_t received[sizeof(payload)];
    ku_ipv4_endpoint source = {{0U, 0U, 0U, 0U}, 0U, 0U};
    uint32_t ready = KU_SOCKET_READY_NONE;
    ku_result_t result;
    ku_socket_t receiver;
    ku_socket_t sender;
    const ku_ipv4_endpoint destination = loopback_endpoint(SOCKET_ROUNDTRIP_PORT);

    result = ku_socket_create(KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);
    if (result <= 0) return 0;
    receiver = (ku_socket_t)result;
    if (ku_socket_bind(receiver, &destination) != KU_STATUS_OK) return 0;

    result = ku_socket_create(KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);
    if (result <= 0) return 0;
    sender = (ku_socket_t)result;
    if (ku_socket_connect(sender, &destination) != KU_STATUS_OK) return 0;

    result = ku_socket_send(sender, payload, sizeof(payload));
    if (result != (ku_result_t)sizeof(payload)) return 0;

    if (ku_socket_wait(receiver, KU_SOCKET_READY_READ, UINT64_C(64), &ready) !=
            KU_STATUS_OK ||
        (ready & KU_SOCKET_READY_READ) == 0U) {
        return 0;
    }

    result = ku_socket_receive(receiver, received, sizeof(received), &source);
    if (result != (ku_result_t)sizeof(payload) ||
        !bytes_equal(received, payload, sizeof(payload)) ||
        !is_loopback(&source) || source.port == 0U) {
        return 0;
    }

    if (ku_socket_close(sender) != KU_STATUS_OK ||
        ku_socket_close(receiver) != KU_STATUS_OK) {
        return 0;
    }
    return 1;
}

static int qualify_blocking_readiness(void) {
    static const uint8_t payload[] = {'w', 'a', 'k', 'e'};
    uint8_t received[sizeof(payload)];
    ku_ipv4_endpoint source = {{0U, 0U, 0U, 0U}, 0U, 0U};
    uint32_t ready = KU_SOCKET_READY_NONE;
    int32_t worker_exit = -1;
    uint32_t attempts;
    ku_result_t result;
    ku_result_t worker;
    ku_socket_t receiver;
    const ku_ipv4_endpoint endpoint = loopback_endpoint(SOCKET_WAKE_PORT);

    result = ku_socket_create(KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);
    if (result <= 0) return 0;
    receiver = (ku_socket_t)result;
    if (ku_socket_bind(receiver, &endpoint) != KU_STATUS_OK) return 0;
    if (ku_socket_poll(receiver, KU_SOCKET_READY_READ, &ready) != KU_STATUS_OK ||
        ready != KU_SOCKET_READY_NONE) return 0;

    worker = u_spawn("/system/sockwake");
    if (worker <= 0) return 0;
    ready = KU_SOCKET_READY_NONE;
    if (ku_socket_wait(receiver, KU_SOCKET_READY_READ, UINT64_C(64), &ready) !=
            KU_STATUS_OK || (ready & KU_SOCKET_READY_READ) == 0U) {
        return 0;
    }
    result = ku_socket_receive(receiver, received, sizeof(received), &source);
    if (result != (ku_result_t)sizeof(payload) ||
        !bytes_equal(received, payload, sizeof(payload)) || !is_loopback(&source)) {
        return 0;
    }
    for (attempts = 0U; attempts < 64U; ++attempts) {
        const ku_status_t status = ku_wait((uint64_t)worker, &worker_exit);
        if (status == KU_STATUS_OK) break;
        if (status != KU_STATUS_WOULD_BLOCK) return 0;
        (void)ku_yield();
    }
    if (worker_exit != 0) return 0;

    ready = KU_SOCKET_READY_NONE;
    if (ku_socket_wait(receiver, KU_SOCKET_READY_READ, UINT64_C(2), &ready) !=
            KU_STATUS_TIMED_OUT || ready != KU_SOCKET_READY_NONE) {
        return 0;
    }
    return ku_socket_close(receiver) == KU_STATUS_OK;
}

static int qualify_handle_generation(void) {
    ku_result_t result;
    ku_socket_t stale;
    ku_socket_t replacement;

    result = ku_socket_create(KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);
    if (result <= 0) return 0;
    stale = (ku_socket_t)result;
    if (ku_socket_close(stale) != KU_STATUS_OK) return 0;

    result = ku_socket_create(KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);
    if (result <= 0) return 0;
    replacement = (ku_socket_t)result;
    if (replacement == stale) return 0;
    if (ku_socket_close(stale) != KU_STATUS_INVALID_ARGUMENT) return 0;
    if (ku_socket_close(replacement) != KU_STATUS_OK) return 0;
    return 1;
}

static int qualify_exit_cleanup(void) {
    int32_t exit_code = -1;
    ku_result_t result;
    ku_socket_t rebound;
    const ku_ipv4_endpoint endpoint = loopback_endpoint(SOCKET_EXIT_PORT);

    if (!u_spawn_wait("/system/sockexit", &exit_code) || exit_code != 0) return 0;

    result = ku_socket_create(KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);
    if (result <= 0) return 0;
    rebound = (ku_socket_t)result;
    if (ku_socket_bind(rebound, &endpoint) != KU_STATUS_OK) return 0;
    if (ku_socket_close(rebound) != KU_STATUS_OK) return 0;
    return 1;
}

static int qualify_tcp_progression(void) {
    static const uint8_t request[] = {
        'K', 'U', 'R', 'O', '-', 'T', 'C', 'P', '-', 'P', 'I', 'N', 'G'};
    static const uint8_t response[] = {
        'K', 'U', 'R', 'O', '-', 'T', 'C', 'P', '-', 'P', 'O', 'N', 'G'};
    uint8_t received[sizeof(response)];
    uint32_t ready = KU_SOCKET_READY_NONE;
    ku_result_t result;
    ku_status_t status;
    ku_socket_t socket;
    ku_ipv4_endpoint endpoint;

    if (!wait_network_ready()) return 0;

    (void)u_puts("[TEST] tcp_stage: echo\n");
    endpoint = qemu_host_endpoint(TCP_HOST_PORT);
    result = ku_socket_create(KU_SOCKET_STREAM, KU_SOCKET_PROTOCOL_TCP);
    if (result <= 0) return 0;
    socket = (ku_socket_t)result;
    if (!connect_stream(socket, &endpoint, TCP_FAST_WAIT_TICKS) ||
        !send_stream_all(socket, request, sizeof(request)) ||
        !receive_stream_exact(socket, received, sizeof(received)) ||
        !bytes_equal(received, response, sizeof(response)) ||
        !wait_peer_hangup(socket) || !close_stream(socket)) {
        return 0;
    }

    (void)u_puts("[TEST] tcp_stage: local-close\n");
    endpoint = qemu_host_endpoint(TCP_LOCAL_CLOSE_PORT);
    result = ku_socket_create(KU_SOCKET_STREAM, KU_SOCKET_PROTOCOL_TCP);
    if (result <= 0) return 0;
    socket = (ku_socket_t)result;
    if (!connect_stream(socket, &endpoint, TCP_FAST_WAIT_TICKS) ||
        !close_stream(socket)) {
        return 0;
    }

    (void)u_puts("[TEST] tcp_stage: refused\n");
    endpoint = qemu_host_endpoint(TCP_REFUSED_PORT);
    result = ku_socket_create(KU_SOCKET_STREAM, KU_SOCKET_PROTOCOL_TCP);
    if (result <= 0) return 0;
    socket = (ku_socket_t)result;
    status = ku_socket_connect(socket, &endpoint);
    if (status != KU_STATUS_WOULD_BLOCK && status != KU_STATUS_CONNECTION_REFUSED) {
        return 0;
    }
    if (status == KU_STATUS_WOULD_BLOCK) {
        ready = KU_SOCKET_READY_NONE;
        if (ku_socket_wait(
                socket, KU_SOCKET_READY_ERROR, TCP_FAST_WAIT_TICKS, &ready) !=
                KU_STATUS_OK || (ready & KU_SOCKET_READY_ERROR) == 0U ||
            ku_socket_connect(socket, &endpoint) != KU_STATUS_CONNECTION_REFUSED) {
            return 0;
        }
    }
    if (ku_socket_close(socket) != KU_STATUS_OK) return 0;

    (void)u_puts("[TEST] tcp_stage: timeout\n");
    endpoint = qemu_host_endpoint(TCP_TIMEOUT_PORT);
    result = ku_socket_create(KU_SOCKET_STREAM, KU_SOCKET_PROTOCOL_TCP);
    if (result <= 0) return 0;
    socket = (ku_socket_t)result;
    status = ku_socket_connect(socket, &endpoint);
    if (status != KU_STATUS_WOULD_BLOCK) return 0;
    ready = KU_SOCKET_READY_NONE;
    if (ku_socket_wait(
            socket, KU_SOCKET_READY_ERROR, TCP_TIMEOUT_WAIT_TICKS, &ready) !=
            KU_STATUS_OK ||
        (ready & KU_SOCKET_READY_ERROR) == 0U ||
        ku_socket_connect(socket, &endpoint) != KU_STATUS_TIMED_OUT ||
        ku_socket_close(socket) != KU_STATUS_OK) {
        return 0;
    }

    (void)u_puts("[TEST] tcp_stage: reset\n");
    endpoint = qemu_host_endpoint(TCP_RESET_PORT);
    result = ku_socket_create(KU_SOCKET_STREAM, KU_SOCKET_PROTOCOL_TCP);
    if (result <= 0) return 0;
    socket = (ku_socket_t)result;
    if (!connect_stream(socket, &endpoint, TCP_FAST_WAIT_TICKS)) return 0;
    ready = KU_SOCKET_READY_NONE;
    if (ku_socket_wait(
            socket, KU_SOCKET_READY_ERROR, TCP_FAST_WAIT_TICKS, &ready) !=
            KU_STATUS_OK ||
        (ready & KU_SOCKET_READY_ERROR) == 0U ||
        ku_socket_send(socket, request, sizeof(request)) != KU_STATUS_CONNECTION_RESET ||
        ku_socket_close(socket) != KU_STATUS_OK) {
        return 0;
    }

    return 1;
}

static int qualify_tcp_cleanup(void) {
    uint32_t worker;
    for (worker = 0U; worker < TCP_CLEANUP_WORKERS; ++worker) {
        int32_t exit_code = -1;
        if (!u_spawn_wait("/system/tcpexit", &exit_code) || exit_code != 0) {
            return 0;
        }
    }
    return 1;
}

__attribute__((noreturn)) void _start(void) {
    if (!qualify_udp_roundtrip()) {
        (void)u_puts("[TEST] socket_udp_roundtrip: FAIL\n");
        ku_exit(1);
    }
    (void)u_puts("[TEST] socket_udp_roundtrip: PASS\n");

    if (!qualify_blocking_readiness()) {
        (void)u_puts("[TEST] socket_readiness: FAIL\n");
        ku_exit(4);
    }
    (void)u_puts("[TEST] socket_readiness: PASS\n");

    if (!qualify_handle_generation()) {
        (void)u_puts("[TEST] socket_handle_generation: FAIL\n");
        ku_exit(2);
    }
    (void)u_puts("[TEST] socket_handle_generation: PASS\n");

    if (!qualify_exit_cleanup()) {
        (void)u_puts("[TEST] socket_exit_cleanup: FAIL\n");
        ku_exit(3);
    }
    (void)u_puts("[TEST] socket_exit_cleanup: PASS\n");

    if (!qualify_tcp_progression()) {
        (void)u_puts("[TEST] tcp_progression: FAIL\n");
        ku_exit(5);
    }
    (void)u_puts("[TEST] tcp_progression: PASS\n");

    if (!qualify_tcp_cleanup()) {
        (void)u_puts("[TEST] tcp_cleanup: FAIL\n");
        ku_exit(6);
    }
    (void)u_puts("[TEST] tcp_cleanup: PASS\n");
    ku_exit(0);
}
