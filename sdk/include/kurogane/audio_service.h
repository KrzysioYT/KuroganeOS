#ifndef KUROGANE_SDK_AUDIO_SERVICE_H
#define KUROGANE_SDK_AUDIO_SERVICE_H

#include <kurogane/audio.h>
#include <kurogane/service.h>

#define KU_AUDIO_SERVICE_NAME "audiod.v1"
#define KU_AUDIO_SERVICE_NAME_SIZE 8U
#define KU_AUDIO_SERVICE_ABI_VERSION UINT32_C(1)
#define KU_AUDIO_SERVICE_CHUNK_FRAMES 56U
#define KU_AUDIO_SERVICE_QUEUE_DEPTH 4U

typedef uint64_t ku_audio_stream_t;

enum ku_audio_service_operation {
    KU_AUDIO_SERVICE_OPEN = 1,
    KU_AUDIO_SERVICE_SUBMIT = 2,
    KU_AUDIO_SERVICE_SET_GAIN = 3,
    KU_AUDIO_SERVICE_CLOSE = 4,
    KU_AUDIO_SERVICE_QUERY = 5
};

typedef struct ku_audio_service_request {
    uint32_t structure_size;
    uint32_t operation;
    ku_audio_stream_t stream;
    uint32_t frame_count;
    uint32_t gain_percent;
    int16_t samples[KU_AUDIO_SERVICE_CHUNK_FRAMES * KU_AUDIO_PCM_CHANNELS];
} ku_audio_service_request;

typedef struct ku_audio_service_response {
    uint32_t structure_size;
    int32_t status;
    ku_audio_stream_t stream;
    uint32_t queued_chunks;
    uint32_t queue_capacity;
    uint32_t active_streams;
    uint32_t reserved;
} ku_audio_service_response;

#if defined(__cplusplus)
static_assert(sizeof(ku_audio_service_request) == 248, "audio service request ABI mismatch");
static_assert(sizeof(ku_audio_service_response) == 32, "audio service response ABI mismatch");
#else
_Static_assert(sizeof(ku_audio_service_request) == 248, "audio service request ABI mismatch");
_Static_assert(sizeof(ku_audio_service_response) == 32, "audio service response ABI mismatch");
#endif

static inline ku_result_t ku_audio_service_connect(void) {
    return ku_service_connect(KU_AUDIO_SERVICE_NAME, KU_AUDIO_SERVICE_NAME_SIZE);
}

static inline ku_status_t ku_audio_service_send(
    ku_service_connection_t connection,
    const ku_audio_service_request* request) {
    if (request == NULL || request->structure_size != sizeof(*request)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    return ku_service_send(connection, request, sizeof(*request));
}

static inline ku_status_t ku_audio_service_receive(
    ku_service_connection_t connection,
    ku_audio_service_response* response) {
    ku_service_message message;
    ku_status_t status;
    if (response == NULL) return KU_STATUS_INVALID_ARGUMENT;
    status = ku_service_receive(connection, &message);
    if (status != KU_STATUS_OK) return status;
    if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
    *response = *(const ku_audio_service_response*)(const void*)message.data;
    if (response->structure_size != sizeof(*response) || response->reserved != 0U) {
        return KU_STATUS_CORRUPT_DATA;
    }
    return KU_STATUS_OK;
}

#endif
