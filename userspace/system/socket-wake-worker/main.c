#include "../../runtime/user.h"

#include <kurogane/network.h>

#define SOCKET_WAKE_PORT UINT16_C(45352)

static ku_ipv4_endpoint wake_endpoint(void) {
    ku_ipv4_endpoint endpoint = {{127U, 0U, 0U, 1U}, SOCKET_WAKE_PORT, 0U};
    return endpoint;
}

__attribute__((noreturn)) void _start(void) {
    static const uint8_t payload[] = {'w', 'a', 'k', 'e'};
    ku_result_t result;
    ku_socket_t socket;
    const ku_ipv4_endpoint endpoint = wake_endpoint();
    if (ku_sleep(3U) != KU_STATUS_OK) ku_exit(1);
    result = ku_socket_create(KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);
    if (result <= 0) ku_exit(2);
    socket = (ku_socket_t)result;
    if (ku_socket_connect(socket, &endpoint) != KU_STATUS_OK) ku_exit(3);
    if (ku_socket_send(socket, payload, sizeof(payload)) != (ku_result_t)sizeof(payload)) {
        ku_exit(4);
    }
    if (ku_socket_close(socket) != KU_STATUS_OK) ku_exit(5);
    ku_exit(0);
}
