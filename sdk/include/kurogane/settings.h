#ifndef KUROGANE_SDK_SETTINGS_H
#define KUROGANE_SDK_SETTINGS_H

#include <kurogane/service.h>

#define KU_SETTINGS_SERVICE_NAME "settings.v1"
#define KU_SETTINGS_SERVICE_NAME_SIZE 11U
#define KU_SETTINGS_CHANGED_TOPIC "settings.changed"
#define KU_SETTINGS_CHANGED_TOPIC_SIZE 16U
#define KU_SETTINGS_KEY_CAPACITY 48U
#define KU_SETTINGS_VALUE_CAPACITY 128U

enum ku_settings_operation {
    KU_SETTINGS_GET = 1,
    KU_SETTINGS_SET = 2,
    KU_SETTINGS_DELETE = 3
};

enum ku_settings_type {
    KU_SETTINGS_TYPE_NONE = 0,
    KU_SETTINGS_TYPE_BOOL = 1,
    KU_SETTINGS_TYPE_I64 = 2,
    KU_SETTINGS_TYPE_U64 = 3,
    KU_SETTINGS_TYPE_STRING = 4
};

typedef struct ku_settings_request {
    uint32_t structure_size;
    uint32_t operation;
    uint32_t type;
    uint32_t value_size;
    char key[KU_SETTINGS_KEY_CAPACITY];
    uint8_t value[KU_SETTINGS_VALUE_CAPACITY];
} ku_settings_request;

typedef struct ku_settings_response {
    uint32_t structure_size;
    ku_status_t status;
    uint32_t type;
    uint32_t value_size;
    uint8_t value[KU_SETTINGS_VALUE_CAPACITY];
} ku_settings_response;

#if defined(__cplusplus)
static_assert(sizeof(ku_settings_request) == 192, "settings request ABI mismatch");
static_assert(sizeof(ku_settings_response) == 144, "settings response ABI mismatch");
#else
_Static_assert(sizeof(ku_settings_request) == 192, "settings request ABI mismatch");
_Static_assert(sizeof(ku_settings_response) == 144, "settings response ABI mismatch");
#endif

static inline ku_result_t ku_settings_connect(void) {
    return ku_service_connect(KU_SETTINGS_SERVICE_NAME, KU_SETTINGS_SERVICE_NAME_SIZE);
}

#endif
