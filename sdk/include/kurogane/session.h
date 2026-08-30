#ifndef KUROGANE_SDK_SESSION_H
#define KUROGANE_SDK_SESSION_H

#include <kurogane/service.h>

#define KU_SESSION_SERVICE_NAME "session.v1"
#define KU_SESSION_SERVICE_NAME_SIZE 10U
#define KU_SESSION_ABI_VERSION UINT32_C(1)
#define KU_SESSION_MAX_APPLICATIONS 8U

typedef uint64_t ku_session_id_t;
#define KU_SESSION_INVALID_ID UINT64_C(0)

enum ku_session_operation {
    KU_SESSION_CREATE = 1,
    KU_SESSION_QUERY = 2,
    KU_SESSION_SET_HOME = 3,
    KU_SESSION_ATTACH_APPLICATION = 4,
    KU_SESSION_DETACH_APPLICATION = 5,
    KU_SESSION_TERMINATE = 6
};

enum ku_session_state {
    KU_SESSION_STATE_ACTIVE = 1,
    KU_SESSION_STATE_TERMINATING = 2
};

typedef struct ku_session_request {
    uint32_t structure_size;
    uint32_t operation;
    ku_session_id_t session_id;
    uint64_t account_id;
    uint64_t process_id;
    uint64_t reserved;
} ku_session_request;

typedef struct ku_session_response {
    uint32_t structure_size;
    int32_t status;
    ku_session_id_t session_id;
    uint64_t account_id;
    uint64_t owner_pid;
    uint64_t home_pid;
    uint32_t state;
    uint32_t application_count;
    uint64_t applications[KU_SESSION_MAX_APPLICATIONS];
} ku_session_response;

#if defined(__cplusplus)
static_assert(sizeof(ku_session_request) == 40, "session request ABI mismatch");
static_assert(sizeof(ku_session_response) == 112, "session response ABI mismatch");
#else
_Static_assert(sizeof(ku_session_request) == 40, "session request ABI mismatch");
_Static_assert(sizeof(ku_session_response) == 112, "session response ABI mismatch");
#endif

#endif
