#include "../../runtime/user.h"

#include <kurogane/service.h>

#define QUAL_SERVICE_NAME "qualification.meta"
#define QUAL_CAPABILITIES UINT64_C(0x0000000000000025)

static int metadata_valid(const ku_service_info* info) {
    return info != (const ku_service_info*)0 &&
        info->service_version == 3U &&
        info->minimum_client_version == 2U &&
        info->capabilities == QUAL_CAPABILITIES &&
        info->owner_pid != 0U && info->owner_pid != ku_getpid();
}

static int connect_after_lookup(uint64_t expected_owner) {
    ku_service_negotiation negotiation = {
        sizeof(ku_service_negotiation),
        KU_SERVICE_NEGOTIATION_ABI_VERSION,
        2U, 3U, 0U, 0U, 0U, 0U, UINT64_C(0), UINT64_C(0)
    };
    const ku_result_t connected = ku_service_connect_versioned(
        QUAL_SERVICE_NAME,
        sizeof(QUAL_SERVICE_NAME) - 1U,
        &negotiation);
    const uint8_t ping = UINT8_C(0x5A);
    uint32_t attempts;
    if (connected <= 0 || negotiation.owner_pid != expected_owner ||
        negotiation.selected_version != 3U) {
        if (connected > 0) (void)ku_service_close((ku_service_connection_t)connected);
        return 0;
    }
    if (ku_service_send((ku_service_connection_t)connected, &ping, sizeof(ping)) != KU_STATUS_OK) {
        (void)ku_service_close((ku_service_connection_t)connected);
        return 0;
    }
    for (attempts = 0U; attempts < 500U; ++attempts) {
        ku_service_message message;
        const ku_status_t status = ku_service_receive((ku_service_connection_t)connected, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK || message.data_size != sizeof(uint64_t) ||
            message.sender_pid != expected_owner ||
            *(const uint64_t*)(const void*)message.data != expected_owner) {
            (void)ku_service_close((ku_service_connection_t)connected);
            return 0;
        }
        (void)ku_service_close((ku_service_connection_t)connected);
        return 1;
    }
    (void)ku_service_close((ku_service_connection_t)connected);
    return 0;
}

__attribute__((noreturn)) void _start(void) {
    uint32_t attempts;
    ku_service_info info;
    ku_status_t status = KU_STATUS_NOT_FOUND;

    for (attempts = 0U; attempts < 500U; ++attempts) {
        status = ku_service_query(
            QUAL_SERVICE_NAME,
            sizeof(QUAL_SERVICE_NAME) - 1U,
            &info);
        if (status == KU_STATUS_NOT_FOUND || status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        break;
    }
    if (status != KU_STATUS_OK || !metadata_valid(&info)) goto fail;

    /* Lookup must be side-effect free: exceed the pending-channel capacity. */
    for (attempts = 0U; attempts < 64U; ++attempts) {
        ku_service_info repeated;
        if (ku_service_query(
                QUAL_SERVICE_NAME,
                sizeof(QUAL_SERVICE_NAME) - 1U,
                &repeated) != KU_STATUS_OK ||
            !metadata_valid(&repeated) || repeated.owner_pid != info.owner_pid) {
            goto fail;
        }
    }
    if (!connect_after_lookup(info.owner_pid)) goto fail;

    {
        ku_service_info missing;
        if (ku_service_query("qualification.missing", 21U, &missing) != KU_STATUS_NOT_FOUND) goto fail;
    }
    {
        ku_service_info invalid;
        if (ku_service_query(QUAL_SERVICE_NAME, 0U, &invalid) != KU_STATUS_INVALID_ARGUMENT) goto fail;
    }
    {
        ku_service_info wrong_abi = {
            sizeof(ku_service_info),
            KU_SERVICE_INFO_ABI_VERSION + 1U,
            0U, 0U, UINT64_C(0), UINT64_C(0)
        };
        if ((ku_status_t)ku_syscall3(
                KU_SYS_IPC_QUERY,
                (uint64_t)(uintptr_t)QUAL_SERVICE_NAME,
                sizeof(QUAL_SERVICE_NAME) - 1U,
                (uint64_t)(uintptr_t)&wrong_abi) != KU_STATUS_VERSION_MISMATCH) {
            goto fail;
        }
    }

    (void)u_puts("[TEST] service_lookup: PASS\n");
    ku_exit(0);

fail:
    (void)u_puts("[TEST] service_lookup: FAIL\n");
    ku_exit(1);
}
