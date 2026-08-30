#include "../../runtime/user.h"

#include <kurogane/service.h>

#define QUAL_SERVICE_NAME "qualification.meta"
#define QUAL_CAPABILITIES UINT64_C(0x0000000000000025)

static void clear_negotiation(
    ku_service_negotiation* negotiation,
    uint32_t minimum,
    uint32_t maximum) {
    *negotiation = (ku_service_negotiation){
        sizeof(ku_service_negotiation),
        KU_SERVICE_NEGOTIATION_ABI_VERSION,
        minimum,
        maximum,
        0U, 0U, 0U, 0U, UINT64_C(0), UINT64_C(0)
    };
}

static ku_status_t verify_connection(uint32_t minimum, uint32_t maximum, uint32_t selected) {
    ku_service_negotiation negotiation;
    ku_service_message message;
    uint32_t attempts;
    const uint8_t ping = UINT8_C(0x5A);
    clear_negotiation(&negotiation, minimum, maximum);
    {
        const ku_result_t connected = ku_service_connect_versioned(
            QUAL_SERVICE_NAME,
            sizeof(QUAL_SERVICE_NAME) - 1U,
            &negotiation);
        if (connected <= 0) return (ku_status_t)connected;
        if (negotiation.selected_version != selected ||
            negotiation.service_version != 3U ||
            negotiation.minimum_client_version != 2U ||
            negotiation.capabilities != QUAL_CAPABILITIES ||
            negotiation.owner_pid == 0U || negotiation.owner_pid == ku_getpid()) {
            (void)ku_service_close((ku_service_connection_t)connected);
            return KU_STATUS_CORRUPT_DATA;
        }
        if (ku_service_send((ku_service_connection_t)connected, &ping, sizeof(ping)) != KU_STATUS_OK) {
            (void)ku_service_close((ku_service_connection_t)connected);
            return KU_STATUS_IO_ERROR;
        }
        for (attempts = 0U; attempts < 500U; ++attempts) {
            const ku_status_t status = ku_service_receive((ku_service_connection_t)connected, &message);
            if (status == KU_STATUS_WOULD_BLOCK) {
                (void)ku_sleep(1U);
                continue;
            }
            if (status != KU_STATUS_OK || message.data_size != sizeof(uint64_t)) {
                (void)ku_service_close((ku_service_connection_t)connected);
                return status == KU_STATUS_OK ? KU_STATUS_CORRUPT_DATA : status;
            }
            {
                const uint64_t reported_owner = *(const uint64_t*)(const void*)message.data;
                (void)ku_service_close((ku_service_connection_t)connected);
                return reported_owner == negotiation.owner_pid && message.sender_pid == negotiation.owner_pid
                    ? KU_STATUS_OK : KU_STATUS_CORRUPT_DATA;
            }
        }
        (void)ku_service_close((ku_service_connection_t)connected);
        return KU_STATUS_TIMED_OUT;
    }
}

static ku_status_t expect_failure(uint32_t minimum, uint32_t maximum, ku_status_t expected) {
    ku_service_negotiation negotiation;
    clear_negotiation(&negotiation, minimum, maximum);
    {
        const ku_result_t result = ku_service_connect_versioned(
            QUAL_SERVICE_NAME,
            sizeof(QUAL_SERVICE_NAME) - 1U,
            &negotiation);
        if (result > 0) {
            (void)ku_service_close((ku_service_connection_t)result);
            return KU_STATUS_BAD_STATE;
        }
        return (ku_status_t)result == expected ? KU_STATUS_OK : (ku_status_t)result;
    }
}

__attribute__((noreturn)) void _start(void) {
    uint32_t attempts;
    ku_status_t status = KU_STATUS_NOT_FOUND;

    for (attempts = 0U; attempts < 500U; ++attempts) {
        status = verify_connection(2U, 3U, 3U);
        if (status == KU_STATUS_NOT_FOUND || status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        break;
    }
    if (status != KU_STATUS_OK) goto fail;
    if (verify_connection(2U, 2U, 2U) != KU_STATUS_OK) goto fail;
    if (expect_failure(4U, 5U, KU_STATUS_VERSION_MISMATCH) != KU_STATUS_OK) goto fail;
    if (expect_failure(1U, 1U, KU_STATUS_VERSION_MISMATCH) != KU_STATUS_OK) goto fail;
    if (expect_failure(3U, 2U, KU_STATUS_INVALID_ARGUMENT) != KU_STATUS_OK) goto fail;

    (void)u_puts("[TEST] service_version_negotiation: PASS\n");
    ku_exit(0);

fail:
    (void)u_puts("[TEST] service_version_negotiation: FAIL\n");
    ku_exit(1);
}
