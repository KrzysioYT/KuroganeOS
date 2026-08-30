#ifndef KUROGANE_SDK_CLIPBOARD_H
#define KUROGANE_SDK_CLIPBOARD_H

#include <kurogane/service.h>

#define KU_CLIPBOARD_SERVICE_NAME "clipboard.v1"
#define KU_CLIPBOARD_SERVICE_NAME_SIZE 12U
#define KU_CLIPBOARD_DATA_CAPACITY 192U

enum ku_clipboard_operation {
    KU_CLIPBOARD_GET = 1,
    KU_CLIPBOARD_SET = 2,
    KU_CLIPBOARD_CLEAR = 3
};

enum ku_clipboard_format {
    KU_CLIPBOARD_FORMAT_NONE = 0,
    KU_CLIPBOARD_FORMAT_UTF8 = 1,
    KU_CLIPBOARD_FORMAT_BINARY = 2
};

typedef struct ku_clipboard_request {
    uint32_t structure_size;
    uint32_t operation;
    uint32_t format;
    uint32_t data_size;
    uint64_t generation;
    uint8_t data[KU_CLIPBOARD_DATA_CAPACITY];
} ku_clipboard_request;

typedef struct ku_clipboard_response {
    uint32_t structure_size;
    ku_status_t status;
    uint32_t format;
    uint32_t data_size;
    uint64_t generation;
    uint64_t owner_pid;
    uint8_t data[KU_CLIPBOARD_DATA_CAPACITY];
} ku_clipboard_response;

#if defined(__cplusplus)
static_assert(sizeof(ku_clipboard_request) == 216, "clipboard request ABI mismatch");
static_assert(sizeof(ku_clipboard_response) == 224, "clipboard response ABI mismatch");
#else
_Static_assert(sizeof(ku_clipboard_request) == 216, "clipboard request ABI mismatch");
_Static_assert(sizeof(ku_clipboard_response) == 224, "clipboard response ABI mismatch");
#endif

static inline ku_result_t ku_clipboard_connect(void) {
    return ku_service_connect(
        KU_CLIPBOARD_SERVICE_NAME,
        KU_CLIPBOARD_SERVICE_NAME_SIZE);
}

#endif
