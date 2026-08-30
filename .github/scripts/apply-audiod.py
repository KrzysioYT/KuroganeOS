from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: anchor count={count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


Path('sdk/include/kurogane/audio_service.h').write_text(r'''#ifndef KUROGANE_SDK_AUDIO_SERVICE_H
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
''')

Path('userspace/system/audiod').mkdir(parents=True, exist_ok=True)
Path('userspace/system/audiod/main.c').write_text(r'''#include "../../runtime/user.h"

#include <kurogane/audio.h>
#include <kurogane/audio_service.h>

#define AUDIOD_MAX_CLIENTS 8U

typedef struct audio_chunk {
    uint32_t frames;
    int16_t samples[KU_AUDIO_SERVICE_CHUNK_FRAMES * KU_AUDIO_PCM_CHANNELS];
} audio_chunk;

typedef struct audio_client {
    ku_service_connection_t connection;
    uint64_t pid;
    ku_audio_stream_t stream;
    uint32_t generation;
    uint32_t gain_percent;
    audio_chunk queue[KU_AUDIO_SERVICE_QUEUE_DEPTH];
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t queue_count;
    int active;
} audio_client;

static audio_client clients[AUDIOD_MAX_CLIENTS];
static int hardware_busy;

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static ku_audio_stream_t make_stream(size_t index, uint32_t generation) {
    return ((uint64_t)generation << 32U) | (uint64_t)(index + 1U);
}

static uint32_t active_stream_count(void) {
    uint32_t count = 0U;
    size_t index;
    for (index = 0U; index < AUDIOD_MAX_CLIENTS; ++index) {
        if (clients[index].active && clients[index].stream != 0U) ++count;
    }
    return count;
}

static void reset_client(audio_client* client) {
    const uint32_t generation = client->generation;
    clear_bytes(client, sizeof(*client));
    client->generation = generation;
}

static ku_status_t send_response(
    audio_client* client,
    ku_status_t status) {
    ku_audio_service_response response;
    if (client == (audio_client*)0 || !client->active) return KU_STATUS_INVALID_ARGUMENT;
    clear_bytes(&response, sizeof(response));
    response.structure_size = sizeof(response);
    response.status = status;
    response.stream = client->stream;
    response.queued_chunks = client->queue_count;
    response.queue_capacity = KU_AUDIO_SERVICE_QUEUE_DEPTH;
    response.active_streams = active_stream_count();
    return ku_service_send(client->connection, &response, sizeof(response));
}

static int stream_matches(const audio_client* client, ku_audio_stream_t stream) {
    return client != (const audio_client*)0 && client->active &&
        client->stream != 0U && client->stream == stream;
}

static void open_stream(audio_client* client, const ku_audio_service_request* request) {
    size_t index;
    if (client->stream != 0U) {
        (void)send_response(client, KU_STATUS_ALREADY_EXISTS);
        return;
    }
    if (request->gain_percent > 100U) {
        (void)send_response(client, KU_STATUS_OUT_OF_RANGE);
        return;
    }
    for (index = 0U; index < AUDIOD_MAX_CLIENTS; ++index) {
        if (&clients[index] != client) continue;
        ++client->generation;
        if (client->generation == 0U) client->generation = 1U;
        client->stream = make_stream(index, client->generation);
        client->gain_percent = request->gain_percent;
        client->queue_head = 0U;
        client->queue_tail = 0U;
        client->queue_count = 0U;
        (void)send_response(client, KU_STATUS_OK);
        return;
    }
    (void)send_response(client, KU_STATUS_BAD_STATE);
}

static void submit_chunk(audio_client* client, const ku_audio_service_request* request) {
    audio_chunk* chunk;
    size_t sample_count;
    size_t index;
    if (!stream_matches(client, request->stream)) {
        (void)send_response(client, KU_STATUS_INVALID_ARGUMENT);
        return;
    }
    if (request->frame_count == 0U || request->frame_count > KU_AUDIO_SERVICE_CHUNK_FRAMES) {
        (void)send_response(client, KU_STATUS_OUT_OF_RANGE);
        return;
    }
    if (client->queue_count >= KU_AUDIO_SERVICE_QUEUE_DEPTH) {
        (void)send_response(client, KU_STATUS_WOULD_BLOCK);
        return;
    }
    chunk = &client->queue[client->queue_head];
    clear_bytes(chunk, sizeof(*chunk));
    chunk->frames = request->frame_count;
    sample_count = (size_t)request->frame_count * KU_AUDIO_PCM_CHANNELS;
    for (index = 0U; index < sample_count; ++index) {
        chunk->samples[index] = request->samples[index];
    }
    client->queue_head = (client->queue_head + 1U) % KU_AUDIO_SERVICE_QUEUE_DEPTH;
    ++client->queue_count;
    (void)send_response(client, KU_STATUS_OK);
}

static void set_gain(audio_client* client, const ku_audio_service_request* request) {
    if (!stream_matches(client, request->stream)) {
        (void)send_response(client, KU_STATUS_INVALID_ARGUMENT);
        return;
    }
    if (request->gain_percent > 100U) {
        (void)send_response(client, KU_STATUS_OUT_OF_RANGE);
        return;
    }
    client->gain_percent = request->gain_percent;
    (void)send_response(client, KU_STATUS_OK);
}

static void close_stream(audio_client* client, const ku_audio_service_request* request) {
    if (!stream_matches(client, request->stream)) {
        (void)send_response(client, KU_STATUS_INVALID_ARGUMENT);
        return;
    }
    client->stream = 0U;
    client->gain_percent = 0U;
    client->queue_head = 0U;
    client->queue_tail = 0U;
    client->queue_count = 0U;
    clear_bytes(client->queue, sizeof(client->queue));
    (void)send_response(client, KU_STATUS_OK);
}

static void handle_request(audio_client* client, const ku_service_message* message) {
    const ku_audio_service_request* request;
    if (message->data_size != sizeof(ku_audio_service_request)) {
        (void)send_response(client, KU_STATUS_CORRUPT_DATA);
        return;
    }
    request = (const ku_audio_service_request*)(const void*)message->data;
    if (request->structure_size != sizeof(*request)) {
        (void)send_response(client, KU_STATUS_INVALID_ARGUMENT);
        return;
    }
    if (client->pid == 0U) client->pid = message->sender_pid;
    else if (client->pid != message->sender_pid) {
        (void)send_response(client, KU_STATUS_ACCESS_DENIED);
        return;
    }
    switch (request->operation) {
        case KU_AUDIO_SERVICE_OPEN:
            open_stream(client, request);
            return;
        case KU_AUDIO_SERVICE_SUBMIT:
            submit_chunk(client, request);
            return;
        case KU_AUDIO_SERVICE_SET_GAIN:
            set_gain(client, request);
            return;
        case KU_AUDIO_SERVICE_CLOSE:
            close_stream(client, request);
            return;
        case KU_AUDIO_SERVICE_QUERY:
            (void)send_response(client, KU_STATUS_OK);
            return;
        default:
            (void)send_response(client, KU_STATUS_NOT_SUPPORTED);
            return;
    }
}

static void accept_clients(ku_service_endpoint_t endpoint) {
    for (;;) {
        const ku_result_t accepted = ku_service_accept(endpoint);
        size_t index;
        if (accepted == KU_STATUS_WOULD_BLOCK) return;
        if (accepted <= 0) return;
        for (index = 0U; index < AUDIOD_MAX_CLIENTS; ++index) {
            audio_client* client = &clients[index];
            if (client->active) continue;
            reset_client(client);
            client->connection = (ku_service_connection_t)accepted;
            client->active = 1;
            client->gain_percent = 100U;
            break;
        }
        if (index == AUDIOD_MAX_CLIENTS) {
            (void)ku_service_close((ku_service_connection_t)accepted);
            return;
        }
    }
}

static void service_clients(void) {
    size_t index;
    for (index = 0U; index < AUDIOD_MAX_CLIENTS; ++index) {
        audio_client* client = &clients[index];
        ku_service_message message;
        ku_status_t status;
        if (!client->active) continue;
        status = ku_service_receive(client->connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) continue;
        if (status != KU_STATUS_OK) {
            (void)ku_service_close(client->connection);
            reset_client(client);
            continue;
        }
        handle_request(client, &message);
    }
}

static int16_t clamp_sample(int32_t value) {
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

static uint32_t next_mix_frames(void) {
    uint32_t frames = 0U;
    size_t index;
    for (index = 0U; index < AUDIOD_MAX_CLIENTS; ++index) {
        const audio_client* client = &clients[index];
        if (!client->active || client->stream == 0U || client->queue_count == 0U) continue;
        if (client->queue[client->queue_tail].frames > frames) {
            frames = client->queue[client->queue_tail].frames;
        }
    }
    return frames;
}

static void consume_front_chunks(void) {
    size_t index;
    for (index = 0U; index < AUDIOD_MAX_CLIENTS; ++index) {
        audio_client* client = &clients[index];
        if (!client->active || client->stream == 0U || client->queue_count == 0U) continue;
        clear_bytes(&client->queue[client->queue_tail], sizeof(audio_chunk));
        client->queue_tail = (client->queue_tail + 1U) % KU_AUDIO_SERVICE_QUEUE_DEPTH;
        --client->queue_count;
    }
}

static void pump_audio(void) {
    int16_t mixed[KU_AUDIO_SERVICE_CHUNK_FRAMES * KU_AUDIO_PCM_CHANNELS];
    uint32_t frames;
    size_t sample;
    size_t client_index;
    ku_status_t status;

    if (hardware_busy) {
        status = ku_audio_poll();
        if (status == KU_STATUS_WOULD_BLOCK) return;
        hardware_busy = 0;
        if (status != KU_STATUS_OK) (void)ku_audio_stop();
    }

    frames = next_mix_frames();
    if (frames == 0U) return;
    clear_bytes(mixed, sizeof(mixed));
    for (sample = 0U; sample < (size_t)frames * KU_AUDIO_PCM_CHANNELS; ++sample) {
        int32_t accumulator = 0;
        const uint32_t frame_index = (uint32_t)(sample / KU_AUDIO_PCM_CHANNELS);
        const uint32_t channel = (uint32_t)(sample % KU_AUDIO_PCM_CHANNELS);
        for (client_index = 0U; client_index < AUDIOD_MAX_CLIENTS; ++client_index) {
            const audio_client* client = &clients[client_index];
            const audio_chunk* chunk;
            size_t source_index;
            if (!client->active || client->stream == 0U || client->queue_count == 0U) continue;
            chunk = &client->queue[client->queue_tail];
            if (frame_index >= chunk->frames) continue;
            source_index = (size_t)frame_index * KU_AUDIO_PCM_CHANNELS + channel;
            accumulator += ((int32_t)chunk->samples[source_index] *
                (int32_t)client->gain_percent) / 100;
        }
        mixed[sample] = clamp_sample(accumulator);
    }

    status = ku_audio_play_pcm16_stereo(mixed, frames);
    if (status == KU_STATUS_OK) {
        consume_front_chunks();
        hardware_busy = 1;
    }
}

__attribute__((noreturn)) void _start(void) {
    const ku_result_t endpoint = ku_service_register(
        KU_AUDIO_SERVICE_NAME, KU_AUDIO_SERVICE_NAME_SIZE);
    if (endpoint <= 0) ku_exit(1);
    (void)u_puts("audiod: audiod.v1 mixer online\n");
    for (;;) {
        accept_clients((ku_service_endpoint_t)endpoint);
        service_clients();
        pump_audio();
        (void)ku_sleep(1U);
        (void)ku_yield();
    }
}
''')

replace_once(
    'scripts/build-linux.sh',
    '    "appregistryd|userspace/system/app-registryd/main.c|system/appregd|c"\n',
    '    "appregistryd|userspace/system/app-registryd/main.c|system/appregd|c"\n'
    '    "audiod|userspace/system/audiod/main.c|system/audiod|c"\n',
)

replace_once(
    'userspace/system/init/main.c',
    '#define APPLICATION_REGISTRY_PATH "/system/appregd"\n',
    '#define APPLICATION_REGISTRY_PATH "/system/appregd"\n'
    '#define AUDIO_SERVICE_PATH "/system/audiod"\n',
)
replace_once(
    'userspace/system/init/main.c',
    '''static uint64_t spawn_application_registry(void) {
    const ku_result_t result = u_spawn(APPLICATION_REGISTRY_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}
''',
    '''static uint64_t spawn_application_registry(void) {
    const ku_result_t result = u_spawn(APPLICATION_REGISTRY_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

static uint64_t spawn_audio_service(void) {
    const ku_result_t result = u_spawn(AUDIO_SERVICE_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}
''',
)
replace_once(
    'userspace/system/init/main.c',
    '''    const uint64_t settings_service_pid = spawn_settings_service();
''',
    '''    const uint64_t audio_service_pid = spawn_audio_service();
    if (audio_service_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/audiod\n");
        ku_exit(37);
    }

    const uint64_t settings_service_pid = spawn_settings_service();
''',
)
replace_once(
    'userspace/system/init/main.c',
    '''    service_watch settings_watch = {
''',
    '''    service_watch audio_service_watch = {
        spawn_audio_service, audio_service_pid, 0U,
        (const char*)0
    };
    service_watch settings_watch = {
''',
)
replace_once(
    'userspace/system/init/main.c',
    '''    if (!supervise_service(&settings_watch)) {
''',
    '''    if (!supervise_service(&audio_service_watch)) {
        (void)u_puts("init: audio service supervision failed\n");
        ku_exit(38);
    }

    if (!supervise_service(&settings_watch)) {
''',
)
replace_once(
    'userspace/system/init/main.c',
    '''        if (!supervise_service(&settings_watch)) {
''',
    '''        if (!supervise_service(&audio_service_watch)) {
            (void)u_puts("init: audio service supervision failed\n");
            ku_exit(39);
        }
        if (!supervise_service(&settings_watch)) {
''',
)
replace_once(
    'TODO-DEFERRED-TESTS.md',
    '- Application registry regressions: manifest discovery, malformed manifest rejection, duplicate IDs, bounded capacity, lookup/index/count IPC, supervisor restart.\n',
    '- Application registry regressions: manifest discovery, malformed manifest rejection, duplicate IDs, bounded capacity, lookup/index/count IPC, supervisor restart.\n'
    '- Async Audio Service regressions: multi-client open/submit/gain/close, bounded queue backpressure, PCM saturation mixing, disconnect cleanup, AC97 busy/error recovery, PID1 supervisor restart.\n',
)

print('audiod v1 service applied')
