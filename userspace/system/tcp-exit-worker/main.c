#include "../../runtime/user.h"

#include <kurogane/network.h>

#define TCP_HOST_PORT UINT16_C(18080)
#define TCP_CONNECT_WAIT_TICKS UINT64_C(400)

static ku_ipv4_endpoint qemu_host_endpoint(void) {
    ku_ipv4_endpoint endpoint;
    endpoint.address[0] = 10U;
    endpoint.address[1] = 0U;
    endpoint.address[2] = 2U;
    endpoint.address[3] = 2U;
    endpoint.port = TCP_HOST_PORT;
    endpoint.reserved = 0U;
    return endpoint;
}

__attribute__((noreturn)) void _start(void) {
    const ku_ipv4_endpoint endpoint = qemu_host_endpoint();
    const ku_result_t result = ku_socket_create(KU_SOCKET_STREAM, KU_SOCKET_PROTOCOL_TCP);
    ku_status_t status;
    ku_socket_t socket;
    uint32_t ready = KU_SOCKET_READY_NONE;

    if (result <= 0) ku_exit(1);
    socket = (ku_socket_t)result;
    status = ku_socket_connect(socket, &endpoint);
    if (status == KU_STATUS_WOULD_BLOCK) {
        status = ku_socket_wait(
            socket,
            KU_SOCKET_READY_CONNECTED | KU_SOCKET_READY_WRITE |
                KU_SOCKET_READY_ERROR,
            TCP_CONNECT_WAIT_TICKS,
            &ready);
        if (status != KU_STATUS_OK ||
            (ready & KU_SOCKET_READY_ERROR) != 0U ||
            (ready & KU_SOCKET_READY_CONNECTED) == 0U) {
            ku_exit(2);
        }
    } else if (status != KU_STATUS_OK) {
        ku_exit(3);
    }

    /* Intentionally do not close. Process teardown must reclaim the socket and
       bounded TCP session slot. */
    ku_exit(0);
}
