#include "../../runtime/user.h"

#include <kurogane/network.h>

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

__attribute__((noreturn)) void _start(void) {
    const ku_result_t result = ku_socket_create(
        KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);
    if (result <= 0) ku_exit(1);

    const ku_socket_t socket = (ku_socket_t)result;
    const ku_ipv4_endpoint endpoint = loopback_endpoint(SOCKET_EXIT_PORT);
    if (ku_socket_bind(socket, &endpoint) != KU_STATUS_OK) ku_exit(2);

    /* Deliberately do not close the socket. Process teardown owns cleanup. */
    (void)u_puts("[TEST] socket_exit_worker_open: PASS\n");
    ku_exit(0);
}
