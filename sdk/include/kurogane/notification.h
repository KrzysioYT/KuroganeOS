#ifndef KUROGANE_SDK_NOTIFICATION_H
#define KUROGANE_SDK_NOTIFICATION_H

#include <kurogane/service.h>

#define KU_NOTIFICATION_SERVICE_NAME "notifications.v1"
#define KU_NOTIFICATION_SERVICE_NAME_SIZE 16U
#define KU_NOTIFICATION_TITLE_CAPACITY 48U
#define KU_NOTIFICATION_BODY_CAPACITY 128U

enum ku_notification_operation {
    KU_NOTIFICATION_POST = 1,
    KU_NOTIFICATION_GET = 2,
    KU_NOTIFICATION_DISMISS = 3
};

enum ku_notification_type {
    KU_NOTIFICATION_TYPE_APPLICATION = 1,
    KU_NOTIFICATION_TYPE_SYSTEM = 2,
    KU_NOTIFICATION_TYPE_SECURITY = 3
};

enum ku_notification_priority {
    KU_NOTIFICATION_PRIORITY_LOW = 1,
    KU_NOTIFICATION_PRIORITY_NORMAL = 2,
    KU_NOTIFICATION_PRIORITY_HIGH = 3,
    KU_NOTIFICATION_PRIORITY_CRITICAL = 4
};

enum ku_notification_state {
    KU_NOTIFICATION_STATE_ACTIVE = 1,
    KU_NOTIFICATION_STATE_DISMISSED = 2
};

typedef struct ku_notification_request {
    uint32_t structure_size;
    uint32_t operation;
    uint64_t notification_id;
    uint32_t type;
    uint32_t priority;
    uint32_t flags;
    uint32_t reserved;
    char title[KU_NOTIFICATION_TITLE_CAPACITY];
    char body[KU_NOTIFICATION_BODY_CAPACITY];
} ku_notification_request;

typedef struct ku_notification_response {
    uint32_t structure_size;
    ku_status_t status;
    uint64_t notification_id;
    uint64_t owner_pid;
    uint32_t type;
    uint32_t priority;
    uint32_t state;
    uint32_t flags;
    char title[KU_NOTIFICATION_TITLE_CAPACITY];
    char body[KU_NOTIFICATION_BODY_CAPACITY];
} ku_notification_response;

#if defined(__cplusplus)
static_assert(sizeof(ku_notification_request) == 208, "notification request ABI mismatch");
static_assert(sizeof(ku_notification_response) == 216, "notification response ABI mismatch");
#else
_Static_assert(sizeof(ku_notification_request) == 208, "notification request ABI mismatch");
_Static_assert(sizeof(ku_notification_response) == 216, "notification response ABI mismatch");
#endif

static inline ku_result_t ku_notification_connect(void) {
    return ku_service_connect(
        KU_NOTIFICATION_SERVICE_NAME,
        KU_NOTIFICATION_SERVICE_NAME_SIZE);
}

#endif
