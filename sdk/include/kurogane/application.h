#ifndef KUROGANE_SDK_APPLICATION_H
#define KUROGANE_SDK_APPLICATION_H

#include <kurogane/service.h>

#define KU_APPLICATION_SERVICE_NAME "appreg.v1"
#define KU_APPLICATION_SERVICE_NAME_SIZE 9U
#define KU_APPLICATION_ABI_VERSION UINT32_C(1)
#define KU_APPLICATION_ID_CAPACITY 32U
#define KU_APPLICATION_NAME_CAPACITY 48U
#define KU_APPLICATION_EXECUTABLE_CAPACITY 96U
#define KU_APPLICATION_MAX_ENTRIES 16U

enum ku_application_operation {
    KU_APPLICATION_GET_COUNT = 1,
    KU_APPLICATION_GET_BY_INDEX = 2,
    KU_APPLICATION_LOOKUP = 3
};

enum ku_application_flags {
    KU_APPLICATION_FLAG_NONE = 0,
    KU_APPLICATION_FLAG_SYSTEM = UINT32_C(1) << 0,
    KU_APPLICATION_FLAG_GRAPHICAL = UINT32_C(1) << 1,
    KU_APPLICATION_FLAG_CONSOLE = UINT32_C(1) << 2,
    KU_APPLICATION_FLAG_BUILTIN = UINT32_C(1) << 3
};

typedef struct ku_application_request {
    uint32_t structure_size;
    uint32_t operation;
    uint32_t index;
    uint32_t reserved;
    char id[KU_APPLICATION_ID_CAPACITY];
} ku_application_request;

typedef struct ku_application_response {
    uint32_t structure_size;
    int32_t status;
    uint32_t index;
    uint32_t count;
    uint32_t flags;
    uint32_t manifest_version;
    char id[KU_APPLICATION_ID_CAPACITY];
    char name[KU_APPLICATION_NAME_CAPACITY];
    char executable[KU_APPLICATION_EXECUTABLE_CAPACITY];
} ku_application_response;

#if defined(__cplusplus)
static_assert(sizeof(ku_application_request) == 48, "application request ABI mismatch");
static_assert(sizeof(ku_application_response) == 200, "application response ABI mismatch");
#else
_Static_assert(sizeof(ku_application_request) == 48, "application request ABI mismatch");
_Static_assert(sizeof(ku_application_response) == 200, "application response ABI mismatch");
#endif

#endif
