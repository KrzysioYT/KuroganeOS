#include "../../runtime/user.h"

#include <kurogane/network.h>

#define SOCKET_ROUNDTRIP_PORT UINT16_C(45350)
#define SOCKET_EXIT_PORT UINT16_C(45351)

static ku_ipv4_endpoint loopback_endpoint(uint16_t port) {
    ku_ipv4_endpoint endpoint;
    endpoint.address[0] = 127U;
    endpoint.address[1] = 0U;
    endpoint.address[2] = 0U;
    endpoint.address[3] = 1U;
    endpoint.port = port;
    endpoint.reserved = 0U;
    return endpoint;
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

__attribute__((noreturn)) void _start(void) {
    if (!qualify_udp_roundtrip()) {
        (void)u_puts("[TEST] socket_udp_roundtrip: FAIL\n");
        ku_exit(1);
    }
    (void)u_puts("[TEST] socket_udp_roundtrip: PASS\n");

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
    ku_exit(0);
}
