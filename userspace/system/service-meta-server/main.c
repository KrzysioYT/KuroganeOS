#include "../../runtime/user.h"

#include <kurogane/service.h>

#define QUAL_SERVICE_NAME "qualification.meta"
#define QUAL_CAPABILITIES UINT64_C(0x0000000000000025)

static int service_connection(ku_service_connection_t connection) {
    uint32_t attempts;
    for (attempts = 0U; attempts < 500U; ++attempts) {
        ku_service_message message;
        const ku_status_t status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK || message.data_size != 1U || message.data[0] != UINT8_C(0x5A)) {
            return 0;
        }
        {
            const uint64_t owner_pid = ku_getpid();
            return ku_service_send(connection, &owner_pid, sizeof(owner_pid)) == KU_STATUS_OK;
        }
    }
    return 0;
}

__attribute__((noreturn)) void _start(void) {
    const ku_service_descriptor descriptor = {
        sizeof(ku_service_descriptor),
        KU_SERVICE_DESCRIPTOR_ABI_VERSION,
        3U,
        2U,
        QUAL_CAPABILITIES,
        UINT64_C(0)
    };
    const ku_result_t registered = ku_service_register_versioned(
        QUAL_SERVICE_NAME,
        sizeof(QUAL_SERVICE_NAME) - 1U,
        &descriptor);
    if (registered <= 0) {
        (void)u_puts("[TEST] service_version_server: FAIL\n");
        ku_exit(1);
    }
    (void)u_puts("[TEST] service_version_server: PASS\n");

    for (;;) {
        const ku_result_t accepted = ku_service_accept((ku_service_endpoint_t)registered);
        if (accepted == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (accepted <= 0) {
            (void)u_puts("[TEST] service_version_server: FAIL\n");
            ku_exit(2);
        }
        if (!service_connection((ku_service_connection_t)accepted)) {
            (void)ku_service_close((ku_service_connection_t)accepted);
            (void)u_puts("[TEST] service_version_server: FAIL\n");
            ku_exit(3);
        }
        (void)ku_service_close((ku_service_connection_t)accepted);
    }
}
