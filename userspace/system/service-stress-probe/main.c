#include "../../runtime/user.h"

#include <kurogane/service.h>

#define QUAL_SERVICE_NAME "qualification.meta"
#define QUAL_CAPABILITIES UINT64_C(0x0000000000000025)
#define STRESS_ITERATIONS 256U
#define RECEIVE_ATTEMPTS 500U

static int query_service(ku_service_info* info, uint64_t expected_owner) {
    const ku_status_t status = ku_service_query(
        QUAL_SERVICE_NAME,
        sizeof(QUAL_SERVICE_NAME) - 1U,
        info);
    if (status != KU_STATUS_OK) return 0;
    if (info->service_version != 3U ||
        info->minimum_client_version != 2U ||
        info->capabilities != QUAL_CAPABILITIES ||
        info->owner_pid == 0U || info->owner_pid == ku_getpid()) {
        return 0;
    }
    return expected_owner == 0U || info->owner_pid == expected_owner;
}

static int roundtrip(uint64_t expected_owner) {
    ku_service_negotiation negotiation = {
        sizeof(ku_service_negotiation),
        KU_SERVICE_NEGOTIATION_ABI_VERSION,
        2U,
        3U,
        0U, 0U, 0U, 0U,
        UINT64_C(0),
        UINT64_C(0)
    };
    const uint8_t ping = UINT8_C(0x5A);
    const ku_result_t connected = ku_service_connect_versioned(
        QUAL_SERVICE_NAME,
        sizeof(QUAL_SERVICE_NAME) - 1U,
        &negotiation);
    uint32_t attempt;

    if (connected <= 0 || negotiation.selected_version != 3U ||
        negotiation.owner_pid != expected_owner ||
        negotiation.service_version != 3U ||
        negotiation.minimum_client_version != 2U ||
        negotiation.capabilities != QUAL_CAPABILITIES) {
        if (connected > 0) (void)ku_service_close((ku_service_connection_t)connected);
        return 0;
    }
    if (ku_service_send((ku_service_connection_t)connected, &ping, sizeof(ping)) != KU_STATUS_OK) {
        (void)ku_service_close((ku_service_connection_t)connected);
        return 0;
    }

    for (attempt = 0U; attempt < RECEIVE_ATTEMPTS; ++attempt) {
        ku_service_message message;
        const ku_status_t status = ku_service_receive(
            (ku_service_connection_t)connected,
            &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK ||
            message.sender_pid != expected_owner ||
            message.data_size != sizeof(uint64_t) ||
            *(const uint64_t*)(const void*)message.data != expected_owner) {
            (void)ku_service_close((ku_service_connection_t)connected);
            return 0;
        }
        return ku_service_close((ku_service_connection_t)connected) == KU_STATUS_OK;
    }

    (void)ku_service_close((ku_service_connection_t)connected);
    return 0;
}

__attribute__((noreturn)) void _start(void) {
    uint32_t attempt;
    uint32_t iteration;
    uint64_t owner = 0U;

    for (attempt = 0U; attempt < 500U; ++attempt) {
        ku_service_info info;
        if (query_service(&info, 0U)) {
            owner = info.owner_pid;
            break;
        }
        (void)ku_sleep(1U);
    }
    if (owner == 0U) goto fail;

    for (iteration = 0U; iteration < STRESS_ITERATIONS; ++iteration) {
        ku_service_info info;
        if (!query_service(&info, owner)) goto fail;
        if (!roundtrip(owner)) goto fail;

        /* Periodically prove missing discovery does not poison later lookups. */
        if ((iteration & UINT32_C(31)) == 0U) {
            ku_service_info missing;
            if (ku_service_query("qualification.missing", 21U, &missing) != KU_STATUS_NOT_FOUND)
                goto fail;
        }
    }

    {
        ku_service_info final_info;
        if (!query_service(&final_info, owner) || !roundtrip(owner)) goto fail;
    }

    (void)u_puts("[TEST] service_channel_churn: PASS\n");
    ku_exit(0);

fail:
    (void)u_puts("[TEST] service_channel_churn: FAIL\n");
    ku_exit(1);
}
