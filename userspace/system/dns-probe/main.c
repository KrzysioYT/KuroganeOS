#include "../../runtime/user.h"

#include <kurogane/network.h>

static int nonzero_address(const uint8_t address[4]) {
    return address[0] != 0U || address[1] != 0U ||
        address[2] != 0U || address[3] != 0U;
}

static void fail(uint32_t code) {
    (void)u_puts("[TEST] public_dns_resolve: FAIL code=");
    (void)u_put_u64(code);
    (void)u_puts("\n");
    ku_exit((int32_t)code);
}

__attribute__((noreturn)) void _start(void) {
    ku_network_status network = {0};
    ku_dns_a_request request = {0};
    uint32_t attempts;

    network.structure_size = sizeof(network);
    for (attempts = 0U; attempts < 800U; ++attempts) {
        const ku_status_t status = ku_network_get_status(&network);
        if (status == KU_STATUS_OK && network.ready != 0U &&
            network.address[0] != 0U) {
            break;
        }
        (void)ku_sleep(1U);
    }
    if (attempts == 800U) fail(1U);

    request.structure_size = sizeof(request);
    request.flags = KU_DNS_FLAG_NONE;
    request.host[0] = 'e'; request.host[1] = 'x'; request.host[2] = 'a';
    request.host[3] = 'm'; request.host[4] = 'p'; request.host[5] = 'l';
    request.host[6] = 'e'; request.host[7] = '.'; request.host[8] = 'c';
    request.host[9] = 'o'; request.host[10] = 'm'; request.host[11] = '\0';

    if (ku_dns_resolve_a(&request) != KU_STATUS_OK ||
        !nonzero_address(request.address)) {
        fail(2U);
    }

    {
        ku_dns_a_request invalid = {0};
        invalid.structure_size = sizeof(invalid) - 1U;
        invalid.host[0] = 'x';
        invalid.host[1] = '\0';
        if ((ku_status_t)ku_syscall3(
                KU_SYS_DNS_RESOLVE_A,
                (uint64_t)(uintptr_t)&invalid,
                sizeof(invalid),
                0U) != KU_STATUS_VERSION_MISMATCH) {
            fail(3U);
        }
    }

    {
        ku_dns_a_request empty = {0};
        empty.structure_size = sizeof(empty);
        if (ku_dns_resolve_a(&empty) != KU_STATUS_INVALID_ARGUMENT) {
            fail(4U);
        }
    }

    (void)u_puts("[TEST] public_dns_resolve: PASS\n");
    ku_exit(0);
}
