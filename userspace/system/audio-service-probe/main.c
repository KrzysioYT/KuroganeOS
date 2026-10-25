#include "../../runtime/user.h"

#include <kurogane/audio.h>
#include <kurogane/audio_service.h>

#define AUDIO_PROBE_ATTEMPTS 2000U

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void fail(uint32_t code) {
    (void)u_puts("[TEST] audio_service_roundtrip: FAIL code=");
    (void)u_put_u64(code);
    (void)u_puts("\n");
    ku_exit((int32_t)code);
}

static ku_result_t connect_audio(void) {
    uint32_t attempt;
    ku_result_t result = KU_STATUS_NOT_FOUND;
    for (attempt = 0U; attempt < AUDIO_PROBE_ATTEMPTS; ++attempt) {
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
    for (attempt = 0U; attempt < AUDIO_PROBE_ATTEMPTS; ++attempt) {
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
    const ku_status_t status = ku_audio_service_send(connection, request);
    if (status != KU_STATUS_OK) return status;
    return wait_response(connection, response);
}

static ku_status_t query(
    ku_service_connection_t connection,
    ku_audio_service_response* response) {
    ku_audio_service_request request;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_AUDIO_SERVICE_QUERY;
    clear_bytes(response, sizeof(*response));
    return transact(connection, &request, response);
}

static int wait_active_streams(
    ku_service_connection_t connection,
    uint32_t expected) {
    uint32_t attempt;
    for (attempt = 0U; attempt < AUDIO_PROBE_ATTEMPTS; ++attempt) {
        ku_audio_service_response response;
        if (query(connection, &response) == KU_STATUS_OK &&
            response.status == KU_STATUS_OK &&
            response.queue_capacity == KU_AUDIO_SERVICE_QUEUE_DEPTH &&
            response.active_streams == expected) {
            return 1;
        }
        (void)ku_sleep(1U);
    }
    return 0;
}

static ku_audio_stream_t open_stream(
    ku_service_connection_t connection,
    uint32_t gain,
    uint32_t expected_active,
    uint32_t fail_code) {
    ku_audio_service_request request;
    ku_audio_service_response response;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_AUDIO_SERVICE_OPEN;
    request.gain_percent = gain;
    clear_bytes(&response, sizeof(response));
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OK || response.stream == 0U ||
        response.queue_capacity != KU_AUDIO_SERVICE_QUEUE_DEPTH ||
        response.active_streams != expected_active) {
        fail(fail_code);
    }
    return response.stream;
}

static void close_stream(
    ku_service_connection_t connection,
    ku_audio_stream_t stream,
    uint32_t expected_active,
    uint32_t fail_code) {
    ku_audio_service_request request;
    ku_audio_service_response response;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_AUDIO_SERVICE_CLOSE;
    request.stream = stream;
    clear_bytes(&response, sizeof(response));
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OK || response.stream != 0U ||
        response.active_streams != expected_active) {
        fail(fail_code);
    }
}

static void fill_chunk(
    ku_audio_service_request* request,
    ku_audio_stream_t stream,
    int16_t left,
    int16_t right) {
    uint32_t frame;
    clear_bytes(request, sizeof(*request));
    request->structure_size = sizeof(*request);
    request->operation = KU_AUDIO_SERVICE_SUBMIT;
    request->stream = stream;
    request->frame_count = KU_AUDIO_SERVICE_CHUNK_FRAMES;
    for (frame = 0U; frame < KU_AUDIO_SERVICE_CHUNK_FRAMES; ++frame) {
        request->samples[(size_t)frame * KU_AUDIO_PCM_CHANNELS] = left;
        request->samples[(size_t)frame * KU_AUDIO_PCM_CHANNELS + 1U] = right;
    }
}

__attribute__((noreturn)) void _start(void) {
    ku_audio_state state;
    ku_result_t connected1;
    ku_result_t connected2;
    ku_service_connection_t connection1;
    ku_service_connection_t connection2;
    ku_audio_service_response response;
    ku_audio_stream_t stream1;
    ku_audio_stream_t stream2;
    ku_audio_stream_t reopened;
    ku_audio_service_request request1;
    ku_audio_service_request request2;
    int16_t raw_samples[KU_AUDIO_SERVICE_CHUNK_FRAMES * KU_AUDIO_PCM_CHANNELS];
    ku_result_t child;
    int32_t child_status = -1;
    size_t sample;

    clear_bytes(&state, sizeof(state));
    state.structure_size = sizeof(state);
    if (ku_audio_get_state(&state) != KU_STATUS_OK || state.available == 0U ||
        state.sample_rate != KU_AUDIO_PCM_SAMPLE_RATE ||
        state.channels != KU_AUDIO_PCM_CHANNELS ||
        state.bits_per_sample != KU_AUDIO_PCM_BITS_PER_SAMPLE) {
        fail(1U);
    }

    connected1 = connect_audio();
    if (connected1 <= 0) fail(2U);
    connection1 = (ku_service_connection_t)connected1;

    clear_bytes(&response, sizeof(response));
    if (query(connection1, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OK || response.stream != 0U ||
        response.queue_capacity != KU_AUDIO_SERVICE_QUEUE_DEPTH ||
        response.active_streams != 0U) {
        fail(3U);
    }

    stream1 = open_stream(connection1, 100U, 1U, 4U);

    connected2 = connect_audio();
    if (connected2 <= 0) fail(5U);
    connection2 = (ku_service_connection_t)connected2;
    stream2 = open_stream(connection2, 50U, 2U, 6U);
    if (stream1 == stream2) fail(7U);

    fill_chunk(&request1, stream1, INT16_C(1400), INT16_C(-1400));
    fill_chunk(&request2, stream2, INT16_C(-700), INT16_C(700));
    if (ku_audio_service_send(connection1, &request1) != KU_STATUS_OK) fail(8U);
    if (ku_audio_service_send(connection2, &request2) != KU_STATUS_OK) fail(9U);
    clear_bytes(&response, sizeof(response));
    if (wait_response(connection1, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OK ||
        response.queue_capacity != KU_AUDIO_SERVICE_QUEUE_DEPTH ||
        response.active_streams != 2U) {
        fail(10U);
    }
    clear_bytes(&response, sizeof(response));
    if (wait_response(connection2, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OK ||
        response.queue_capacity != KU_AUDIO_SERVICE_QUEUE_DEPTH ||
        response.active_streams != 2U) {
        fail(11U);
    }

    clear_bytes(&request1, sizeof(request1));
    request1.structure_size = sizeof(request1);
    request1.operation = KU_AUDIO_SERVICE_SUBMIT;
    request1.stream = stream1;
    request1.frame_count = KU_AUDIO_SERVICE_CHUNK_FRAMES + 1U;
    clear_bytes(&response, sizeof(response));
    if (transact(connection1, &request1, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OUT_OF_RANGE) {
        fail(12U);
    }

    close_stream(connection1, stream1, 1U, 13U);

    clear_bytes(&request1, sizeof(request1));
    request1.structure_size = sizeof(request1);
    request1.operation = KU_AUDIO_SERVICE_SET_GAIN;
    request1.stream = stream1;
    request1.gain_percent = 25U;
    clear_bytes(&response, sizeof(response));
    if (transact(connection1, &request1, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_INVALID_ARGUMENT) {
        fail(14U);
    }

    reopened = open_stream(connection1, 75U, 2U, 15U);
    if (reopened == stream1) fail(16U);
    close_stream(connection1, reopened, 1U, 17U);

    child = u_spawn("/system/audxcli");
    if (child <= 0 || !u_wait((uint64_t)child, &child_status) || child_status != 0) {
        fail(18U);
    }
    if (!wait_active_streams(connection2, 1U)) fail(19U);

    close_stream(connection2, stream2, 0U, 20U);
    if (ku_service_close(connection1) != KU_STATUS_OK) fail(21U);
    if (ku_service_close(connection2) != KU_STATUS_OK) fail(22U);

    (void)u_puts("[TEST] audio_service_roundtrip: PASS\n");

    child_status = -1;
    child = u_spawn("/system/audownx");
    if (child <= 0 || !u_wait((uint64_t)child, &child_status) || child_status != 0) {
        fail(23U);
    }

    for (sample = 0U;
         sample < KU_AUDIO_SERVICE_CHUNK_FRAMES * KU_AUDIO_PCM_CHANNELS;
         ++sample) {
        raw_samples[sample] = (sample & 1U) == 0U ? INT16_C(900) : INT16_C(-900);
    }
    if (ku_audio_play_pcm16_stereo(raw_samples, KU_AUDIO_SERVICE_CHUNK_FRAMES) != KU_STATUS_OK) {
        fail(24U);
    }
    if (ku_audio_stop() != KU_STATUS_OK) fail(25U);

    (void)u_puts("[TEST] audio_exit_cleanup: PASS\n");
    ku_exit(0);
}
