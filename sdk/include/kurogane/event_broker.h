#ifndef KUROGANE_SDK_EVENT_BROKER_H
#define KUROGANE_SDK_EVENT_BROKER_H

#include <kurogane/event.h>
#include <kurogane/service.h>

#define KU_EVENT_BROKER_SERVICE_NAME "events.v1"
#define KU_EVENT_BROKER_SERVICE_NAME_SIZE 9U
#define KU_EVENT_BROKER_TOPIC_CAPACITY 32U

enum ku_event_broker_operation {
    KU_EVENT_BROKER_SUBSCRIBE = 1,
    KU_EVENT_BROKER_PUBLISH = 2,
    KU_EVENT_BROKER_UNSUBSCRIBE = 3
};

typedef struct ku_event_broker_request {
    uint32_t structure_size;
    uint32_t operation;
    char topic[KU_EVENT_BROKER_TOPIC_CAPACITY];
} ku_event_broker_request;

typedef struct ku_event_broker_response {
    uint32_t structure_size;
    ku_status_t status;
    uint64_t value;
} ku_event_broker_response;

#if defined(__cplusplus)
static_assert(sizeof(ku_event_broker_request) == 40, "event broker request ABI mismatch");
static_assert(sizeof(ku_event_broker_response) == 16, "event broker response ABI mismatch");
#else
_Static_assert(sizeof(ku_event_broker_request) == 40, "event broker request ABI mismatch");
_Static_assert(sizeof(ku_event_broker_response) == 16, "event broker response ABI mismatch");
#endif

static inline ku_result_t ku_event_broker_connect(void) {
    return ku_service_connect(
        KU_EVENT_BROKER_SERVICE_NAME,
        KU_EVENT_BROKER_SERVICE_NAME_SIZE);
}

#endif
