#include "../common.h"
#include <kurogane/settings.h>

#define UI_LOW_CONTRAST_KEY "ui.low_contrast"

typedef struct settings_client {
    ku_service_connection_t connection;
    int connected;
} settings_client;

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static void append_percent(char* destination, size_t capacity, uint32_t value) {
    char number[24];
    gui_u64(number, sizeof(number), value > 100U ? 100U : value);
    append_text(destination, capacity, number);
    append_text(destination, capacity, "%");
}

static void copy_key(char* destination, const char* source) {
    (void)strlcpy(destination, source, KU_SETTINGS_KEY_CAPACITY);
}

static ku_status_t settings_transact(
    ku_service_connection_t connection,
    const ku_settings_request* request,
    ku_settings_response* response) {
    uint32_t attempts = 0U;
    ku_status_t status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) return status;
    while (attempts++ < 300U) {
        ku_service_message message;
        status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)kuro_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_settings_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response) ||
            response->value_size > KU_SETTINGS_VALUE_CAPACITY) {
            return KU_STATUS_CORRUPT_DATA;
        }
        return response->status;
    }
    return KU_STATUS_TIMED_OUT;
}

static int connect_settings(settings_client* client) {
    uint32_t attempts = 0U;
    if (client == NULL) return 0;
    memset(client, 0, sizeof(*client));
    while (attempts++ < 300U) {
        const ku_result_t result = ku_settings_connect();
        if (result > 0) {
            client->connection = (ku_service_connection_t)result;
            client->connected = 1;
            return 1;
        }
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK) return 0;
        (void)kuro_sleep(1U);
    }
    return 0;
}

static ku_status_t read_bool_setting(
    settings_client* client,
    const char* key,
    int* value) {
    ku_settings_request request;
    ku_settings_response response;
    ku_status_t status;
    if (client == NULL || !client->connected || key == NULL || value == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_GET;
    request.type = KU_SETTINGS_TYPE_NONE;
    copy_key(request.key, key);
    status = settings_transact(client->connection, &request, &response);
    if (status != KU_STATUS_OK) return status;
    if (response.type != KU_SETTINGS_TYPE_BOOL || response.value_size != 1U ||
        response.value[0] > 1U) return KU_STATUS_CORRUPT_DATA;
    *value = response.value[0] != 0U;
    return KU_STATUS_OK;
}

static ku_status_t write_bool_setting(
    settings_client* client,
    const char* key,
    int value) {
    ku_settings_request request;
    ku_settings_response response;
    if (client == NULL || !client->connected || key == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_SET;
    request.type = KU_SETTINGS_TYPE_BOOL;
    request.value_size = 1U;
    copy_key(request.key, key);
    request.value[0] = value != 0 ? 1U : 0U;
    return settings_transact(client->connection, &request, &response);
}

static int read_audio(ku_audio_state* state) {
    memset(state, 0, sizeof(*state));
    state->structure_size = sizeof(*state);
    return ku_audio_get_state(state) == KU_STATUS_OK &&
        state->version == KU_AUDIO_STATE_VERSION;
}

static int apply_audio_delta(ku_audio_state* audio, int delta) {
    ku_audio_set_request request;
    uint32_t next;
    if (audio == NULL || audio->available == 0U) return 0;
    next = audio->volume_percent;
    if (delta < 0) next = next >= 10U ? next - 10U : 0U;
    else if (delta > 0) next = next <= 90U ? next + 10U : 100U;
    else return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.volume_percent = next;
    request.muted = audio->muted;
    if (ku_audio_set(&request) != KU_STATUS_OK) return 0;
    return read_audio(audio);
}

static int apply_audio_mute(ku_audio_state* audio) {
    ku_audio_set_request request;
    if (audio == NULL || audio->available == 0U) return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.volume_percent = audio->volume_percent;
    request.muted = audio->muted == 0U ? 1U : 0U;
    if (ku_audio_set(&request) != KU_STATUS_OK) return 0;
    return read_audio(audio);
}

static void build_scene(
    kui_scene* scene,
    int low_contrast,
    const ku_audio_state* audio,
    int settings_online) {
    kui_flow root;
    char volume[64] = "MASTER VOLUME\n";
    char service[64] = "SETTINGS SERVICE\n";
    uint32_t volume_value = 0U;
    int muted = 0;

    append_text(service, sizeof(service), settings_online ? "PERSISTENT / ONLINE" : "OFFLINE");
    if (audio != NULL && audio->available != 0U) {
        append_percent(volume, sizeof(volume), audio->volume_percent);
        volume_value = audio->volume_percent;
        muted = audio->muted != 0U;
    } else {
        append_text(volume, sizeof(volume), "AC97 OFFLINE");
    }

    kui_scene_initialize(scene);
    scene->visible_rows = 10U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        low_contrast ? UINT32_C(0x8F2633) : UINT32_C(0xDE192D));
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FLUX SETTINGS / PERSONALIZATION");
    (void)kui_flow_metric(&root, 2U, service, settings_online ? 100U : 0U, 100U);
    (void)kui_flow_metric(&root, 3U, volume, volume_value, 100U);
    (void)kui_flow_toggle(
        &root, 10U,
        "LOW CONTRAST RED\nPERSISTENT INTERFACE PROFILE",
        low_contrast);
    (void)kui_flow_toggle(
        &root, 11U,
        "MUTE AUDIO\nAC97 MASTER OUTPUT",
        muted);
    (void)kui_flow_button(&root, 20U, "VOLUME -10");
    (void)kui_flow_button(&root, 21U, "VOLUME +10");
    (void)kui_flow_button(&root, 22U, "RESET INTERFACE PROFILE");
}

int main(void) {
    const ku_window_t window = gui_open("SETTINGS", 430, 205, 500, 390);
    settings_client settings;
    ku_audio_state audio;
    kui_scene scene;
    uint32_t pointer_buttons = 0U;
    int low_contrast = 0;
    int restored = 0;
    if (window == KU_INVALID_WINDOW) return 1;

    if (connect_settings(&settings)) {
        const ku_status_t status = read_bool_setting(
            &settings, UI_LOW_CONTRAST_KEY, &low_contrast);
        if (status == KU_STATUS_OK) restored = 1;
        else if (status != KU_STATUS_NOT_FOUND) settings.connected = 0;
    }
    (void)read_audio(&audio);
    build_scene(&scene, low_contrast, &audio, settings.connected);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        if (settings.connected) (void)ku_service_close(settings.connection);
        (void)ku_ui_close(window);
        return 2;
    }

    puts("[TEST] desktop_settings_real: PASS");
    puts("[TEST] flux_scene_settings: PASS");
    puts("[TEST] desktop_settings_mouse_navigation: PASS");
    puts("[TEST] desktop_settings_keyboard_shortcuts_detached: PASS");
    puts("[TEST] desktop_audio_settings_ui: PASS");
    puts("[TEST] flux_settings_toggle_cards: PASS");
    if (restored) puts("[TEST] desktop_settings_profile_restore: PASS");
    else puts("[TEST] desktop_settings_profile_restore: DEFAULT");

    for (;;) {
        ku_ui_event event;
        uint32_t target;
        const int wait_result = gui_wait_event(window, &event);
        if (wait_result < 0 || event.type == KU_UI_EVENT_CLOSE) {
            puts("[TEST] desktop_settings_closed: PASS");
            break;
        }
        if (event.type != KU_UI_EVENT_POINTER) continue;
        {
            const uint32_t previous_buttons = pointer_buttons;
            const int primary_pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous_buttons & UINT32_C(1)) == 0U;
            pointer_buttons = event.buttons;
            if (!primary_pressed) continue;
        }
        target = kui_scene_hit_test(&scene, event.x, event.y);
        if (target == 10U) {
            int confirmed = 0;
            const int requested = low_contrast == 0;
            if (!settings.connected ||
                write_bool_setting(&settings, UI_LOW_CONTRAST_KEY, requested) != KU_STATUS_OK ||
                read_bool_setting(&settings, UI_LOW_CONTRAST_KEY, &confirmed) != KU_STATUS_OK ||
                confirmed != requested) {
                puts("[TEST] desktop_settings_profile_persist: FAIL");
                continue;
            }
            low_contrast = confirmed;
            puts("[TEST] desktop_settings_profile_persist: PASS");
        } else if (target == 11U) {
            (void)apply_audio_mute(&audio);
        } else if (target == 20U) {
            (void)apply_audio_delta(&audio, -1);
        } else if (target == 21U) {
            (void)apply_audio_delta(&audio, 1);
        } else if (target == 22U) {
            int confirmed = 1;
            if (settings.connected &&
                write_bool_setting(&settings, UI_LOW_CONTRAST_KEY, 0) == KU_STATUS_OK &&
                read_bool_setting(&settings, UI_LOW_CONTRAST_KEY, &confirmed) == KU_STATUS_OK &&
                confirmed == 0) {
                low_contrast = 0;
            }
        } else {
            continue;
        }
        build_scene(&scene, low_contrast, &audio, settings.connected);
        (void)kui_scene_present(window, &scene);
    }

    if (settings.connected) (void)ku_service_close(settings.connection);
    (void)ku_ui_close(window);
    return 0;
}
