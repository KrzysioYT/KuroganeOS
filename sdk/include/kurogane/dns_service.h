#ifndef KUROGANE_SDK_DNS_SERVICE_H
#define KUROGANE_SDK_DNS_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include <kurogane/network.h>
#include <kurogane/service.h>
#include <kurogane/status.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_DNS_SERVICE_NAME "dnsd.v1"
#define KU_DNS_SERVICE_NAME_SIZE 7U
#define KU_DNS_SERVICE_HOST_CAPACITY KU_DNS_NAME_CAPACITY
#define KU_DNS_SERVICE_FLAG_NONE UINT32_C(0)

enum ku_dns_service_operation {
    KU_DNS_SERVICE_RESOLVE_A = 1
};

typedef struct ku_dns_service_request {
    uint32_t structure_size;
    uint32_t operation;
    uint64_t request_id;
    uint32_t flags;
    uint32_t reserved;
    char host[KU_DNS_SERVICE_HOST_CAPACITY];
} ku_dns_service_request;

typedef struct ku_dns_service_response {
    uint32_t structure_size;
    ku_status_t status;
    uint64_t request_id;
    uint8_t address[4];
    uint32_t reserved;
} ku_dns_service_response;

#if defined(__cplusplus)
static_assert(sizeof(ku_dns_service_request) <= KU_SERVICE_MESSAGE_CAPACITY,
    "DNS service request exceeds IPC message capacity");
static_assert(sizeof(ku_dns_service_response) <= KU_SERVICE_MESSAGE_CAPACITY,
    "DNS service response exceeds IPC message capacity");
#else
_Static_assert(sizeof(ku_dns_service_request) <= KU_SERVICE_MESSAGE_CAPACITY,
    "DNS service request exceeds IPC message capacity");
_Static_assert(sizeof(ku_dns_service_response) <= KU_SERVICE_MESSAGE_CAPACITY,
    "DNS service response exceeds IPC message capacity");
#endif

static inline ku_result_t ku_dns_service_connect(void) {
    return ku_service_connect(KU_DNS_SERVICE_NAME, KU_DNS_SERVICE_NAME_SIZE);
}

static inline ku_status_t ku_dns_service_submit_a(
    ku_service_connection_t connection,
    uint64_t request_id,
    const char* host,
    size_t host_size) {
    ku_dns_service_request request;
    size_t index;
    if (connection == 0U || request_id == 0U || host == NULL ||
        host_size == 0U || host_size >= KU_DNS_SERVICE_HOST_CAPACITY) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    request.structure_size = sizeof(request);
    request.operation = KU_DNS_SERVICE_RESOLVE_A;
    request.request_id = request_id;
    request.flags = KU_DNS_SERVICE_FLAG_NONE;
    request.reserved = 0U;
    for (index = 0U; index < KU_DNS_SERVICE_HOST_CAPACITY; ++index) {
        request.host[index] = '\0';
    }
    for (index = 0U; index < host_size; ++index) request.host[index] = host[index];
    request.host[host_size] = '\0';
    return ku_service_send(connection, &request, sizeof(request));
}

static inline ku_status_t ku_dns_service_receive(
    ku_service_connection_t connection,
    ku_dns_service_response* response) {
    ku_service_message message;
    ku_status_t status;
    if (connection == 0U || response == NULL) return KU_STATUS_INVALID_ARGUMENT;
    status = ku_service_receive(connection, &message);
    if (status != KU_STATUS_OK) return status;
    if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
    *response = *(const ku_dns_service_response*)(const void*)message.data;
    if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
    return KU_STATUS_OK;
}

#ifdef __cplusplus
}
#endif

#endif
