#ifndef KUROGANE_SDK_ACCOUNT_H
#define KUROGANE_SDK_ACCOUNT_H

#include <kurogane/service.h>

#define KU_ACCOUNT_SERVICE_NAME "account.v1"
#define KU_ACCOUNT_SERVICE_NAME_SIZE 10U
#define KU_ACCOUNT_USERNAME_CAPACITY 24U
#define KU_ACCOUNT_LOCALE_CAPACITY 16U
#define KU_ACCOUNT_ABI_VERSION UINT32_C(1)

enum ku_account_operation {
    KU_ACCOUNT_GET_CURRENT = 1,
    KU_ACCOUNT_LOOKUP = 2
};

enum ku_account_flags {
    KU_ACCOUNT_FLAG_LIVE = UINT32_C(1) << 0,
    KU_ACCOUNT_FLAG_INSTALLED = UINT32_C(1) << 1,
    KU_ACCOUNT_FLAG_PASSWORD_REQUIRED = UINT32_C(1) << 2,
    KU_ACCOUNT_FLAG_PROFILE_VALID = UINT32_C(1) << 3
};

typedef struct ku_account_request {
    uint32_t structure_size;
    uint32_t operation;
    uint64_t reserved;
    char username[KU_ACCOUNT_USERNAME_CAPACITY];
} ku_account_request;

typedef struct ku_account_response {
    uint32_t structure_size;
    int32_t status;
    uint64_t account_id;
    uint32_t flags;
    uint32_t reserved;
    char username[KU_ACCOUNT_USERNAME_CAPACITY];
    char locale[KU_ACCOUNT_LOCALE_CAPACITY];
} ku_account_response;

#if defined(__cplusplus)
static_assert(sizeof(ku_account_request) == 40, "account request ABI mismatch");
static_assert(sizeof(ku_account_response) == 64, "account response ABI mismatch");
#else
_Static_assert(sizeof(ku_account_request) == 40, "account request ABI mismatch");
_Static_assert(sizeof(ku_account_response) == 64, "account response ABI mismatch");
#endif

#endif
