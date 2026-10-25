#include "../../runtime/user.h"

#include <kurogane/audio_service.h>

#define AUDIO_EXIT_ATTEMPTS 2000U

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static ku_result_t connect_audio(void) {
    uint32_t attempt;
    ku_result_t result = KU_STATUS_NOT_FOUND;
    for (attempt = 0U; attempt < AUDIO_EXIT_ATTEMPTS; ++attempt) {
        result = ku_audio_service_connect();
        if (result > 0) return result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK) return result;
        (void)ku_sleep(1U);
    }
    return result;
}

static ku_status_t wait_response(
    ku_service_connection_t connection,
    ku_audio_service_response* response) {
    uint32_t attempt;
    if (response == (ku_audio_service_response*)0) return KU_STATUS_INVALID_ARGUMENT;
    for (attempt = 0U; attempt < AUDIO_EXIT_ATTEMPTS; ++attempt) {
        const ku_status_t status = ku_audio_service_receive(connection, response);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        return status;
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_status_t transact(
    ku_service_connection_t connection,
    const ku_audio_service_request* request,
    ku_audio_service_response* response) {
    ku_status_t status = ku_audio_service_send(connection, request);
    if (status != KU_STATUS_OK) return status;
    return wait_response(connection, response);
}

__attribute__((noreturn)) void _start(void) {
    const ku_result_t connected = connect_audio();
    ku_audio_service_request request;
    ku_audio_service_response response;
    ku_audio_stream_t stream;
    size_t sample;

    if (connected <= 0) ku_exit(1);

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_AUDIO_SERVICE_OPEN;
    request.gain_percent = 100U;
    clear_bytes(&response, sizeof(response));
    if (transact((ku_service_connection_t)connected, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OK || response.stream == 0U) {
        ku_exit(2);
    }
    stream = response.stream;

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_AUDIO_SERVICE_SUBMIT;
    request.stream = stream;
    request.frame_count = KU_AUDIO_SERVICE_CHUNK_FRAMES;
    for (sample = 0U;
         sample < KU_AUDIO_SERVICE_CHUNK_FRAMES * KU_AUDIO_PCM_CHANNELS;
         ++sample) {
        request.samples[sample] = (sample & 1U) == 0U ? INT16_C(1200) : INT16_C(-1200);
    }
    clear_bytes(&response, sizeof(response));
    if (transact((ku_service_connection_t)connected, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OK) {
        ku_exit(3);
    }

    (void)u_puts("[TEST] audio_exit_client_armed: PASS\n");
    /* Deliberately do not CLOSE the stream or service connection. */
    ku_exit(0);
}
