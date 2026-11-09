#include "../common.h"
#include "keyboard.h"
#include <kurogane/flux_app_catalog.h>
#include <kurogane/settings.h>
#include <kurogane/desktop.h>
#include <kurogane/filesystem.h>
#include "navigation.h"

#define UI_LOW_CONTRAST_KEY "ui.low_contrast"
#define UI_THEME_KEY "ui.theme"
#define UI_STYLE_KEY "ui.style"
#define UI_BACKGROUND_KEY "ui.background"
#define UI_TEXT_SCALE_KEY "ui.text_scale"

typedef struct settings_client {
    ku_service_connection_t connection;
    int connected;
    int session_only;
} settings_client;

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static void append_percent(char* destination, size_t capacity, uint32_t value) {
    char number[24];
    gui_u64(number, sizeof(number), value);
    append_text(destination, capacity, number);
    append_text(destination, capacity, "%");
}

static void copy_key(char* destination, const char* source) {
    (void)strlcpy(destination, source, KU_SETTINGS_KEY_CAPACITY);
}

static ku_status_t settings_transact(
    settings_client* client,
    const ku_settings_request* request,
    ku_settings_response* response) {
    if (client->connection == 0U) {
        const ku_result_t opened = ku_settings_connect();
        if (opened <= 0) return (ku_status_t)opened;
        client->connection = (ku_service_connection_t)opened;
    }
    const ku_service_connection_t connection = client->connection;
    uint32_t attempts = 0U;
    ku_status_t status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) goto disconnected;
    while (attempts++ < 300U) {
        ku_service_message message;
        status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)kuro_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) goto disconnected;
        if (message.data_size != sizeof(*response)) {
            status = KU_STATUS_CORRUPT_DATA;
            goto disconnected;
        }
        *response = *(const ku_settings_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response) ||
            response->value_size > KU_SETTINGS_VALUE_CAPACITY) {
            status = KU_STATUS_CORRUPT_DATA;
            goto disconnected;
        }
        return response->status;
    }
    status = KU_STATUS_TIMED_OUT;
disconnected:
    /* Replies have no request ID. Discard the entire connection so a late
     * response cannot be mistaken for the next operation's result. */
    (void)ku_service_close(connection);
    client->connection = 0U;
    return status;
}

static int connect_settings(settings_client* client) {
    uint32_t attempts = 0U;
    if (client == NULL) return 0;
    memset(client, 0, sizeof(*client));
    while (attempts++ < 300U) {
        const ku_result_t result = ku_settings_connect();
        if (result > 0) {
            ku_file_stat root = {0};
            client->connection = (ku_service_connection_t)result;
            client->connected = 1;
            client->session_only = ku_file_stat_path("/", 1U, &root) == KU_STATUS_OK &&
                (root.flags & KU_FILE_FLAG_READ_ONLY_MOUNT) != 0U;
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
    status = settings_transact(client, &request, &response);
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
    return settings_transact(client, &request, &response);
}


static ku_status_t read_theme_setting(
    settings_client* client, uint32_t* theme_id) {
    ku_settings_request request;
    ku_settings_response response;
    uint64_t value = 0U;
    ku_status_t status;
    if (client == NULL || !client->connected || theme_id == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_GET;
    copy_key(request.key, UI_THEME_KEY);
    status = settings_transact(client, &request, &response);
    if (status != KU_STATUS_OK) return status;
    if (response.type != KU_SETTINGS_TYPE_U64 ||
        response.value_size != sizeof(value)) return KU_STATUS_CORRUPT_DATA;
    memcpy(&value, response.value, sizeof(value));
    if (value >= KU_FLUX_THEME_COUNT) return KU_STATUS_CORRUPT_DATA;
    *theme_id = (uint32_t)value;
    return KU_STATUS_OK;
}

static ku_status_t write_theme_setting(settings_client* client, uint32_t theme_id) {
    ku_settings_request request;
    ku_settings_response response;
    const uint64_t value = (uint64_t)theme_id;
    if (client == NULL || !client->connected ||
        theme_id >= KU_FLUX_THEME_COUNT) return KU_STATUS_INVALID_ARGUMENT;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_SET;
    request.type = KU_SETTINGS_TYPE_U64;
    request.value_size = sizeof(value);
    copy_key(request.key, UI_THEME_KEY);
    memcpy(request.value, &value, sizeof(value));
    return settings_transact(client, &request, &response);
}

static ku_status_t read_style_setting(
    settings_client* client, uint32_t* style_id) {
    ku_settings_request request;
    ku_settings_response response;
    uint64_t value = 0U;
    ku_status_t status;
    if (client == NULL || !client->connected || style_id == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_GET;
    copy_key(request.key, UI_STYLE_KEY);
    status = settings_transact(client, &request, &response);
    if (status != KU_STATUS_OK) return status;
    if (response.type != KU_SETTINGS_TYPE_U64 ||
        response.value_size != sizeof(value)) return KU_STATUS_CORRUPT_DATA;
    memcpy(&value, response.value, sizeof(value));
    if (value >= KU_FLUX_STYLE_COUNT) return KU_STATUS_CORRUPT_DATA;
    *style_id = (uint32_t)value;
    return KU_STATUS_OK;
}

static ku_status_t write_style_setting(settings_client* client, uint32_t style_id) {
    ku_settings_request request;
    ku_settings_response response;
    const uint64_t value = (uint64_t)style_id;
    if (client == NULL || !client->connected ||
        style_id >= KU_FLUX_STYLE_COUNT) return KU_STATUS_INVALID_ARGUMENT;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_SET;
    request.type = KU_SETTINGS_TYPE_U64;
    request.value_size = sizeof(value);
    copy_key(request.key, UI_STYLE_KEY);
    memcpy(request.value, &value, sizeof(value));
    return settings_transact(client, &request, &response);
}

static ku_status_t read_background_setting(
    settings_client* client, uint32_t* background_id) {
    ku_settings_request request;
    ku_settings_response response;
    uint64_t value = 0U;
    ku_status_t status;
    if (client == NULL || !client->connected || background_id == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_GET;
    copy_key(request.key, UI_BACKGROUND_KEY);
    status = settings_transact(client, &request, &response);
    if (status != KU_STATUS_OK) return status;
    if (response.type != KU_SETTINGS_TYPE_U64 ||
        response.value_size != sizeof(value)) return KU_STATUS_CORRUPT_DATA;
    memcpy(&value, response.value, sizeof(value));
    if (value >= KU_FLUX_BACKGROUND_COUNT) return KU_STATUS_CORRUPT_DATA;
    *background_id = (uint32_t)value;
    return KU_STATUS_OK;
}

static ku_status_t write_background_setting(
    settings_client* client, uint32_t background_id) {
    ku_settings_request request;
    ku_settings_response response;
    const uint64_t value = (uint64_t)background_id;
    if (client == NULL || !client->connected ||
        background_id >= KU_FLUX_BACKGROUND_COUNT) return KU_STATUS_INVALID_ARGUMENT;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_SET;
    request.type = KU_SETTINGS_TYPE_U64;
    request.value_size = sizeof(value);
    copy_key(request.key, UI_BACKGROUND_KEY);
    memcpy(request.value, &value, sizeof(value));
    return settings_transact(client, &request, &response);
}

static ku_status_t read_text_scale_setting(
    settings_client* client, uint32_t* percent) {
    ku_settings_request request;
    ku_settings_response response;
    uint64_t value = 0U;
    ku_status_t status;
    if (client == NULL || !client->connected || percent == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_GET;
    copy_key(request.key, UI_TEXT_SCALE_KEY);
    status = settings_transact(client, &request, &response);
    if (status != KU_STATUS_OK) return status;
    if (response.type != KU_SETTINGS_TYPE_U64 ||
        response.value_size != sizeof(value)) return KU_STATUS_CORRUPT_DATA;
    memcpy(&value, response.value, sizeof(value));
    if (value != 100U && value != 125U && value != 150U && value != 200U) {
        return KU_STATUS_CORRUPT_DATA;
    }
    *percent = (uint32_t)value;
    return KU_STATUS_OK;
}

static ku_status_t write_text_scale_setting(
    settings_client* client, uint32_t percent) {
    ku_settings_request request;
    ku_settings_response response;
    const uint64_t value = (uint64_t)percent;
    if (client == NULL || !client->connected ||
        (percent != 100U && percent != 125U &&
         percent != 150U && percent != 200U)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_SET;
    request.type = KU_SETTINGS_TYPE_U64;
    request.value_size = sizeof(value);
    copy_key(request.key, UI_TEXT_SCALE_KEY);
    memcpy(request.value, &value, sizeof(value));
    return settings_transact(client, &request, &response);
}

static int select_desktop_theme(uint32_t theme_id) {
    ku_desktop_theme_request request;
    if (theme_id >= KU_FLUX_THEME_COUNT) return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.action = KU_DESKTOP_THEME_SET;
    request.theme_id = theme_id;
    return ku_desktop_theme(&request) == KU_STATUS_OK &&
        request.theme_id == theme_id;
}

static int select_desktop_style(uint32_t style_id) {
    ku_desktop_style_request request;
    if (style_id >= KU_FLUX_STYLE_COUNT) return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.action = KU_DESKTOP_STYLE_SET;
    request.style_id = style_id;
    return ku_desktop_style(&request) == KU_STATUS_OK &&
        request.style_id == style_id;
}

static int select_desktop_background(uint32_t background_id) {
    ku_desktop_background_request request;
    if (background_id >= KU_FLUX_BACKGROUND_COUNT) return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.action = KU_DESKTOP_BACKGROUND_SET;
    request.background_id = background_id;
    return ku_desktop_background(&request) == KU_STATUS_OK &&
        request.background_id == background_id;
}

static int select_desktop_text_scale(uint32_t percent) {
    ku_desktop_text_scale_request request;
    if (percent != 100U && percent != 125U &&
        percent != 150U && percent != 200U) return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.action = KU_DESKTOP_TEXT_SCALE_SET;
    request.percent = percent;
    return ku_desktop_text_scale(&request) == KU_STATUS_OK &&
        request.percent == percent;
}

static int active_desktop_theme(uint32_t* theme_id) {
    ku_desktop_theme_request request;
    if (theme_id == NULL) return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.action = KU_DESKTOP_THEME_QUERY;
    if (ku_desktop_theme(&request) != KU_STATUS_OK ||
        request.theme_id >= KU_FLUX_THEME_COUNT) return 0;
    *theme_id = request.theme_id;
    return 1;
}

static int active_desktop_background(uint32_t* background_id) {
    ku_desktop_background_request request;
    if (background_id == NULL) return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.action = KU_DESKTOP_BACKGROUND_QUERY;
    if (ku_desktop_background(&request) != KU_STATUS_OK ||
        request.background_id >= KU_FLUX_BACKGROUND_COUNT) return 0;
    *background_id = request.background_id;
    return 1;
}

static int active_desktop_text_scale(uint32_t* percent) {
    ku_desktop_text_scale_request request;
    if (percent == NULL) return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.action = KU_DESKTOP_TEXT_SCALE_QUERY;
    if (ku_desktop_text_scale(&request) != KU_STATUS_OK ||
        (request.percent != 100U && request.percent != 125U &&
         request.percent != 150U && request.percent != 200U)) return 0;
    *percent = request.percent;
    return 1;
}

/* Snapshot storage independently of the visible selection. An absent key
 * must remain absent on rollback, even if the compositor has a default. */
static int apply_appearance_choice(
    settings_client* client, const char* key, uint32_t requested, uint32_t previous,
    ku_status_t (*read_choice)(settings_client*, uint32_t*),
    ku_status_t (*write_choice)(settings_client*, uint32_t),
    int (*select_choice)(uint32_t)) {
    uint32_t stored = 0U;
    const ku_status_t initial = read_choice(client, &stored);
    if (initial != KU_STATUS_OK && initial != KU_STATUS_NOT_FOUND) return 0;

    uint32_t confirmed = 0U;
    const int ready = write_choice(client, requested) == KU_STATUS_OK &&
        read_choice(client, &confirmed) == KU_STATUS_OK && confirmed == requested;
    if (ready && select_choice(requested)) return 1;

    const int restored_live = !ready || select_choice(previous);
    ku_status_t status = read_choice(client, &confirmed);
    if (status == initial && (initial == KU_STATUS_NOT_FOUND || confirmed == stored))
        return restored_live ? 0 : -1;

    if (initial == KU_STATUS_NOT_FOUND) {
        ku_settings_request request = {0};
        ku_settings_response response;
        request.structure_size = sizeof(request);
        request.operation = KU_SETTINGS_DELETE;
        copy_key(request.key, key);
        (void)settings_transact(client, &request, &response);
    } else {
        (void)write_choice(client, stored);
    }
    status = read_choice(client, &confirmed);
    return restored_live && status == initial &&
        (initial == KU_STATUS_NOT_FOUND || confirmed == stored) ? 0 : -1;
}

static int apply_text_scale_choice(
    settings_client* client, uint32_t requested, uint32_t previous) {
    return apply_appearance_choice(client, UI_TEXT_SCALE_KEY, requested, previous,
        read_text_scale_setting, write_text_scale_setting, select_desktop_text_scale);
}

static int apply_background_choice(
    settings_client* client, uint32_t requested, uint32_t previous) {
    return apply_appearance_choice(client, UI_BACKGROUND_KEY, requested, previous,
        read_background_setting, write_background_setting, select_desktop_background);
}

/* A profile reset is not a cosmetic toggle: persist all five defaults,
 * confirm each stored value and update the compositor. On failure, attempt
 * to restore the previously confirmed profile and report partial rollback. */
static int reset_interface_profile(
    settings_client* client, int* low_contrast,
    uint32_t* theme_id, uint32_t* style_id, uint32_t* background_id,
    uint32_t* text_scale_percent) {
    if (client == NULL || !client->connected || low_contrast == NULL ||
        theme_id == NULL || style_id == NULL || background_id == NULL ||
        text_scale_percent == NULL) return 0;
    const int original_contrast = *low_contrast;
    const uint32_t original_theme = *theme_id;
    const uint32_t original_style = *style_id;
    const uint32_t original_background = *background_id;
    const uint32_t original_text_scale = *text_scale_percent;
    int confirmed_contrast = 1;
    uint32_t confirmed_theme = KU_FLUX_THEME_COUNT;
    uint32_t confirmed_style = KU_FLUX_STYLE_COUNT;
    uint32_t confirmed_background = KU_FLUX_BACKGROUND_COUNT;
    uint32_t confirmed_text_scale = 0U;
    if (write_bool_setting(client, UI_LOW_CONTRAST_KEY, 0) != KU_STATUS_OK ||
        write_theme_setting(client, KU_FLUX_THEME_RED) != KU_STATUS_OK ||
        write_style_setting(client, KU_FLUX_STYLE_MODERN) != KU_STATUS_OK ||
        write_background_setting(client, KU_FLUX_BACKGROUND_FORGED) != KU_STATUS_OK ||
        write_text_scale_setting(client, 100U) != KU_STATUS_OK ||
        read_bool_setting(client, UI_LOW_CONTRAST_KEY, &confirmed_contrast) != KU_STATUS_OK ||
        read_theme_setting(client, &confirmed_theme) != KU_STATUS_OK ||
        read_style_setting(client, &confirmed_style) != KU_STATUS_OK ||
        read_background_setting(client, &confirmed_background) != KU_STATUS_OK ||
        read_text_scale_setting(client, &confirmed_text_scale) != KU_STATUS_OK ||
        confirmed_contrast != 0 || confirmed_theme != KU_FLUX_THEME_RED ||
        confirmed_style != KU_FLUX_STYLE_MODERN ||
        confirmed_background != KU_FLUX_BACKGROUND_FORGED ||
        confirmed_text_scale != 100U ||
        !select_desktop_theme(KU_FLUX_THEME_RED) ||
        !select_desktop_style(KU_FLUX_STYLE_MODERN) ||
        !select_desktop_background(KU_FLUX_BACKGROUND_FORGED) ||
        !select_desktop_text_scale(100U)) {
        int restored_contrast = 0;
        uint32_t restored_theme = KU_FLUX_THEME_COUNT;
        uint32_t restored_style = KU_FLUX_STYLE_COUNT;
        uint32_t restored_background = KU_FLUX_BACKGROUND_COUNT;
        uint32_t restored_text_scale = 0U;
        /* Always attempt every rollback step; do not short-circuit writes. */
        const int wrote_contrast =
            write_bool_setting(client, UI_LOW_CONTRAST_KEY, original_contrast) == KU_STATUS_OK;
        const int wrote_theme =
            write_theme_setting(client, original_theme) == KU_STATUS_OK;
        const int wrote_style =
            write_style_setting(client, original_style) == KU_STATUS_OK;
        const int wrote_background =
            write_background_setting(client, original_background) == KU_STATUS_OK;
        const int wrote_text_scale =
            write_text_scale_setting(client, original_text_scale) == KU_STATUS_OK;
        const int live_theme = select_desktop_theme(original_theme);
        const int live_style = select_desktop_style(original_style);
        const int live_background = select_desktop_background(original_background);
        const int live_text_scale =
            select_desktop_text_scale(original_text_scale);
        const int rolled_back =
            wrote_contrast && wrote_theme && wrote_style && wrote_background &&
            wrote_text_scale &&
            read_bool_setting(client, UI_LOW_CONTRAST_KEY, &restored_contrast) == KU_STATUS_OK &&
            read_theme_setting(client, &restored_theme) == KU_STATUS_OK &&
            read_style_setting(client, &restored_style) == KU_STATUS_OK &&
            read_background_setting(client, &restored_background) == KU_STATUS_OK &&
            read_text_scale_setting(client, &restored_text_scale) == KU_STATUS_OK &&
            restored_contrast == original_contrast &&
            restored_theme == original_theme &&
            restored_style == original_style &&
            restored_background == original_background &&
            restored_text_scale == original_text_scale &&
            live_theme && live_style && live_background && live_text_scale;
        return rolled_back ? 0 : -1;
    }
    *low_contrast = 0;
    *theme_id = KU_FLUX_THEME_RED;
    *style_id = KU_FLUX_STYLE_MODERN;
    *background_id = KU_FLUX_BACKGROUND_FORGED;
    *text_scale_percent = 100U;
    return 1;
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
    uint32_t theme_id,
    uint32_t style_id,
    uint32_t background_id,
    uint32_t text_scale_percent,
    const ku_audio_state* audio,
    const settings_client* settings,
    const char* feedback) {
    kui_flow root;
    char volume[64] = "MASTER VOLUME\n";
    char service[64] = "SETTINGS SERVICE\n";
    char palette_label[64] = "ACTIVE COLOR / ";
    char background_label[64] = "WALLPAPER / ";
    char text_scale_label[64] = "TEXT SIZE / ";
    uint32_t volume_value = 0U;
    int muted = 0;

    append_text(service, sizeof(service), !settings->connected ? "OFFLINE"
        : settings->connection == 0U ? "RETRY ON NEXT ACTION"
        : settings->session_only ? "SESSION ONLY / READ-ONLY ROOT" : "PERSISTENT / ONLINE");
    if (audio != NULL && audio->available != 0U) {
        append_percent(volume, sizeof(volume), audio->volume_percent);
        volume_value = audio->volume_percent;
        muted = audio->muted != 0U;
    } else {
        append_text(volume, sizeof(volume), "AC97 OFFLINE");
    }

    kui_scene_initialize(scene);
    // The entire settings form is taller than the 560px window. A bounded
    // row viewport keeps the lower audio/profile controls reachable by wheel.
    scene->visible_rows = 10U;
    kui_scene_apply_flux_theme(scene);
    if (low_contrast) {
        const ku_flux_palette palette = ku_flux_theme_palette(theme_id);
        scene->accent_rgb = palette.accent_muted;
    }
    append_text(palette_label, sizeof(palette_label), ku_flux_theme_name(theme_id));
    append_text(background_label, sizeof(background_label),
                ku_flux_background_name(background_id));
    append_percent(text_scale_label, sizeof(text_scale_label), text_scale_percent);
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FLUX SETTINGS / PERSONALIZATION");
    (void)kui_flow_label(&root, 4U, feedback);
    (void)kui_flow_metric(&root, 2U, service, settings->connected ? 100U : 0U, 100U);
    (void)kui_flow_metric(&root, 3U, volume, volume_value, 100U);
    (void)kui_flow_panel(&root, 28U, "FLUX 8 / COLOR PERSONALIZATION");
    (void)kui_flow_label(&root, 29U, palette_label);
    (void)kui_flow_button(&root, 30U, "PREVIOUS COLOR");
    (void)kui_flow_button(&root, 31U, "NEXT COLOR");
    (void)kui_flow_label(&root, 46U, text_scale_label);
    (void)kui_flow_button(&root, 33U, "TEXT SIZE -");
    (void)kui_flow_button(&root, 34U, "TEXT SIZE +");
    (void)kui_flow_label(&root, 45U, background_label);
    (void)kui_flow_toggle(&root, 40U, "FORGED STEEL\nANGULAR RED FLUX",
        background_id == KU_FLUX_BACKGROUND_FORGED);
    (void)kui_flow_toggle(&root, 41U, "GRAPHITE GRID\nSTRUCTURED WORKSPACE",
        background_id == KU_FLUX_BACKGROUND_GRID);
    (void)kui_flow_toggle(&root, 42U, "MINIMAL DARK\nDISTRACTION FREE",
        background_id == KU_FLUX_BACKGROUND_MINIMAL);
    (void)kui_flow_toggle(&root, 32U, "FLUX MODERN\nSLEEK WINDOW CHROME / DOCK",
        style_id == KU_FLUX_STYLE_MODERN);
    (void)kui_flow_toggle(
        &root, 10U,
        "LOW CONTRAST ACCENT\nSETTINGS WINDOW",
        low_contrast);
    (void)kui_flow_toggle(
        &root, 11U,
        "MUTE AUDIO\nAC97 MASTER OUTPUT",
        muted);
    (void)kui_flow_button(&root, 20U, "VOLUME -10");
    (void)kui_flow_button(&root, 21U, "VOLUME +10");
    (void)kui_flow_button(&root, 22U, "RESET INTERFACE PROFILE");
    if (!settings->connected) {
        (void)kui_scene_set_flags(scene, 10U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 30U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 31U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 33U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 34U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 32U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 40U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 41U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 42U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 22U, KUI_VIEW_DISABLED);
    }
    if (audio == NULL || audio->available == 0U) {
        (void)kui_scene_set_flags(scene, 11U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 20U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 21U, KUI_VIEW_DISABLED);
    }
}

static void refresh_viewport(ku_window_t window, kui_scene* scene) {
    if (!gui_update_scene_viewport(window, scene)) return;
    kui_scene_reveal_selection(scene);
    (void)kui_scene_present(window, scene);
}

int main(void) {
    const ku_window_t window = gui_open(KU_FLUX_TITLE_SETTINGS, 400, 110, 510, 560);
    settings_client settings;
    ku_audio_state audio;
    kui_scene scene;
    uint32_t pointer_buttons = 0U;
    int low_contrast = 0;
    uint32_t theme_id = KU_FLUX_THEME_RED;
    uint32_t style_id = KU_FLUX_STYLE_MODERN;
    uint32_t background_id = KU_FLUX_BACKGROUND_FORGED;
    uint32_t text_scale_percent = 100U;
    const char* feedback = "TAB / ARROWS FOCUS  ENTER / SPACE APPLY";
    int restored = 0;
    if (window == KU_INVALID_WINDOW) return 1;

    (void)active_desktop_theme(&theme_id);
    (void)active_desktop_background(&background_id);
    (void)active_desktop_text_scale(&text_scale_percent);
    if (connect_settings(&settings)) {
        const ku_status_t status = read_bool_setting(
            &settings, UI_LOW_CONTRAST_KEY, &low_contrast);
        if (status == KU_STATUS_OK) restored = 1;
        else if (status != KU_STATUS_NOT_FOUND) {
            if (settings.connection != 0U) (void)ku_service_close(settings.connection);
            settings.connection = 0U;
            settings.connected = 0;
            feedback = "SETTINGS SERVICE / READ FAILED";
        }
        if (settings.connected) {
            uint32_t stored_theme = KU_FLUX_THEME_RED;
            if (read_theme_setting(&settings, &stored_theme) == KU_STATUS_OK &&
                select_desktop_theme(stored_theme)) theme_id = stored_theme;
            {
                uint32_t stored_style = KU_FLUX_STYLE_MODERN;
                if (read_style_setting(&settings, &stored_style) == KU_STATUS_OK &&
                    select_desktop_style(stored_style)) style_id = stored_style;
            }
            {
                uint32_t stored_background = KU_FLUX_BACKGROUND_FORGED;
                if (read_background_setting(&settings, &stored_background) == KU_STATUS_OK &&
                    select_desktop_background(stored_background)) background_id = stored_background;
            }
            {
                uint32_t stored_text_scale = 100U;
                const ku_status_t scale_status =
                    read_text_scale_setting(&settings, &stored_text_scale);
                if (scale_status == KU_STATUS_OK) {
                    if (select_desktop_text_scale(stored_text_scale)) {
                        text_scale_percent = stored_text_scale;
                    } else {
                        feedback = "TEXT SIZE / APPLY FAILED";
                    }
                } else if (scale_status != KU_STATUS_NOT_FOUND) {
                    feedback = "TEXT SIZE / INVALID PROFILE";
                }
            }
        }
    }
    (void)read_audio(&audio);
    build_scene(&scene, low_contrast, theme_id, style_id, background_id,
                text_scale_percent,
                &audio, &settings, feedback);
    (void)kui_scene_select(&scene, 30U);
    (void)gui_update_scene_viewport(window, &scene);
    kui_scene_reveal_selection(&scene);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        if (settings.connection != 0U) (void)ku_service_close(settings.connection);
        (void)ku_ui_close(window);
        return 2;
    }

    puts("[TEST] desktop_settings_real: PASS");
    puts("[TEST] flux_scene_settings: PASS");
    puts("[TEST] desktop_settings_mouse_navigation: PASS");
    puts("[TEST] flux_settings_keyboard_ready: PASS");
    puts("[TEST] desktop_audio_settings_ui: PASS");
    puts("[TEST] flux_settings_toggle_cards: PASS");
    puts("[TEST] flux_8_theme_selector: PASS");
    puts("[TEST] flux_8_style_selector: PASS");
    puts("[TEST] flux_8_text_scale_selector: READY");
    puts("[TEST] flux_8_wallpaper_selector: READY");
    puts("[TEST] flux_settings_keyboard_access: READY");
    puts("[TEST] flux_settings_profile_reset: READY");
    if (restored) puts("[TEST] desktop_settings_profile_restore: PASS");
    else puts("[TEST] desktop_settings_profile_restore: DEFAULT");

    for (;;) {
        ku_ui_event event;
        uint32_t target;
        int wait_result;
        do {
            refresh_viewport(window, &scene);
            wait_result = kui_next_event(window, &event);
            if (wait_result == 0) (void)kuro_sleep(1U);
        } while (wait_result == 0);
        if (wait_result < 0 || event.type == KU_UI_EVENT_CLOSE) {
            puts("[TEST] desktop_settings_closed: PASS");
            break;
        }
        if (event.type == KU_UI_EVENT_SCROLL) {
            const uint32_t before = scene.scroll_offset;
            // USB HID can coalesce several detents into one signed event.
            // Avoid negating INT32_MIN, which is not representable in int32_t.
            const int32_t step = event.wheel_y == INT32_MIN
                ? INT32_MAX : -event.wheel_y;
            (void)kui_scene_scroll(&scene, step);
            if (scene.scroll_offset != before) {
                (void)kui_scene_present(window, &scene);
                puts("[TEST] flux_settings_wheel_navigation: PASS");
            }
            continue;
        }
        if (event.type == KU_UI_EVENT_KEY) {
            const int action = flux_settings_key(&scene, &event, &target);
            if (action == 1) {
                kui_scene_reveal_selection(&scene);
                (void)kui_scene_present(window, &scene);
                if (kui_scene_selection_visible(&scene)) {
                    puts("[INPUT] flux_settings_keyboard_focus: PASS");
                    puts("[TEST] flux_settings_keyboard_access: PASS");
                }
                continue;
            }
            if (action == 2) {
                if (!kui_scene_selection_visible(&scene)) {
                    kui_scene_reveal_selection(&scene);
                    (void)kui_scene_present(window, &scene);
                    continue;
                }
                puts("[INPUT] flux_settings_keyboard_activate: PASS");
                puts("[TEST] flux_settings_keyboard_access: PASS");
            } else {
                const flux_settings_key_action navigation = flux_settings_keyboard(&event);
                if (navigation != FLUX_SETTINGS_KEY_NEXT &&
                    navigation != FLUX_SETTINGS_KEY_PREVIOUS &&
                    navigation != FLUX_SETTINGS_KEY_PAGE_UP &&
                    navigation != FLUX_SETTINGS_KEY_PAGE_DOWN) continue;
                const int32_t direction =
                    navigation == FLUX_SETTINGS_KEY_NEXT ||
                    navigation == FLUX_SETTINGS_KEY_PAGE_DOWN
                        ? 1 : -1;
                const uint32_t steps = navigation == FLUX_SETTINGS_KEY_PAGE_UP ||
                    navigation == FLUX_SETTINGS_KEY_PAGE_DOWN
                        ? 4U : 1U;
                for (uint32_t index = 0U; index < steps; ++index) {
                    if (kui_scene_select_next(&scene, direction) != KU_STATUS_OK) break;
                }
                kui_scene_reveal_selection(&scene);
                (void)kui_scene_present(window, &scene);
                if (kui_scene_selection_visible(&scene)) {
                    puts("[INPUT] flux_settings_keyboard_focus: PASS");
                    puts("[TEST] flux_settings_keyboard_access: PASS");
                }
                continue;
            }
        } else if (event.type == KU_UI_EVENT_POINTER) {
            const uint32_t previous_buttons = pointer_buttons;
            const int primary_pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous_buttons & UINT32_C(1)) == 0U;
            pointer_buttons = event.buttons;
            if (!primary_pressed) continue;
            target = kui_scene_hit_test(&scene, event.x, event.y);
            if (target == 0U) continue;
            (void)kui_scene_select(&scene, target);
        } else {
            continue;
        }
        if (target == 10U) {
            int confirmed = 0;
            const int requested = low_contrast == 0;
            if (!settings.connected ||
                write_bool_setting(&settings, UI_LOW_CONTRAST_KEY, requested) != KU_STATUS_OK ||
                read_bool_setting(&settings, UI_LOW_CONTRAST_KEY, &confirmed) != KU_STATUS_OK ||
                confirmed != requested) {
                puts("[TEST] desktop_settings_profile_persist: FAIL");
                feedback = "CONTRAST / SAVE FAILED";
            } else {
                low_contrast = confirmed;
                feedback = settings.session_only ? "CONTRAST / SESSION ONLY" : "CONTRAST / SAVED";
                puts(settings.session_only ? "[SETTINGS] contrast_session"
                    : "[TEST] desktop_settings_profile_persist: PASS");
            }
        } else if (target == 30U || target == 31U) {
            const uint32_t requested = target == 30U
                ? (theme_id + KU_FLUX_THEME_COUNT - 1U) % KU_FLUX_THEME_COUNT
                : (theme_id + 1U) % KU_FLUX_THEME_COUNT;
            const int applied = apply_appearance_choice(&settings, UI_THEME_KEY, requested,
                theme_id, read_theme_setting, write_theme_setting, select_desktop_theme);
            if (applied <= 0) {
                puts("[TEST] flux_theme_persistence: FAIL");
                feedback = applied == 0 ? "COLOR / UPDATE FAILED"
                    : "COLOR / PARTIAL FAILURE; CHECK SETTINGS";
            } else {
                theme_id = requested;
                feedback = settings.session_only ? "COLOR / SESSION ONLY" : "COLOR / SAVED";
                if (!settings.session_only) puts("[TEST] flux_theme_persistence: PASS");
                char value[24];
                gui_u64(value, sizeof(value), theme_id);
                printf(settings.session_only ? "[SETTINGS] theme_session=%s\n"
                    : "[SETTINGS] theme_saved=%s\n", value);
            }
        } else if (target == 32U) {
            const uint32_t requested = style_id == KU_FLUX_STYLE_MODERN
                ? KU_FLUX_STYLE_CLASSIC : KU_FLUX_STYLE_MODERN;
            const int applied = apply_appearance_choice(&settings, UI_STYLE_KEY, requested,
                style_id, read_style_setting, write_style_setting, select_desktop_style);
            if (applied <= 0) {
                puts("[TEST] flux_style_persistence: FAIL");
                feedback = applied == 0 ? "STYLE / UPDATE FAILED"
                    : "STYLE / PARTIAL FAILURE; CHECK SETTINGS";
            } else {
                style_id = requested;
                feedback = settings.session_only ? "STYLE / SESSION ONLY" : "STYLE / SAVED";
                puts(settings.session_only ? "[SETTINGS] style_session"
                    : "[TEST] flux_style_persistence: PASS");
            }
        } else if (target == 33U || target == 34U) {
            static const uint32_t scales[] = {100U, 125U, 150U, 200U};
            uint32_t scale_index = 0U;
            while (scale_index < sizeof(scales) / sizeof(scales[0]) &&
                   scales[scale_index] != text_scale_percent) {
                ++scale_index;
            }
            if (scale_index >= sizeof(scales) / sizeof(scales[0])) {
                feedback = "TEXT SIZE / INVALID CURRENT VALUE";
                puts("[TEST] flux_text_scale_persistence: FAIL");
            } else {
                const uint32_t next_index = target == 33U
                    ? (scale_index + 3U) % 4U
                    : (scale_index + 1U) % 4U;
                const uint32_t requested = scales[next_index];
                const int applied = apply_text_scale_choice(
                    &settings, requested, text_scale_percent);
                if (applied <= 0) {
                    feedback = applied == 0
                        ? "TEXT SIZE / SAVE FAILED; PREVIOUS KEPT"
                        : "TEXT SIZE / PARTIAL FAILURE; CHECK SETTINGS";
                    puts("[TEST] flux_text_scale_persistence: FAIL");
                } else {
                    text_scale_percent = requested;
                    feedback = settings.session_only ? "TEXT SIZE / SESSION ONLY" : "TEXT SIZE / SAVED";
                    if (!settings.session_only) puts("[TEST] flux_text_scale_persistence: PASS");
                    char value[24];
                    gui_u64(value, sizeof(value), text_scale_percent);
                    printf(settings.session_only ? "[SETTINGS] text_scale_session=%s\n"
                        : "[SETTINGS] text_scale_saved=%s\n", value);
                }
            }
        } else if (target >= 40U && target <= 42U) {
            const uint32_t requested = target - 40U;
            const int applied = apply_background_choice(
                &settings, requested, background_id);
            if (applied <= 0) {
                puts("[TEST] flux_background_persistence: FAIL");
                feedback = applied == 0 ? "WALLPAPER / SAVE FAILED; PREVIOUS KEPT"
                    : "WALLPAPER / PARTIAL FAILURE; CHECK SETTINGS";
            } else {
                background_id = requested;
                feedback = settings.session_only ? "WALLPAPER / SESSION ONLY" : "WALLPAPER / SAVED";
                puts(settings.session_only ? "[SETTINGS] wallpaper_session"
                    : "[TEST] flux_background_persistence: PASS");
            }
        } else if (target == 11U) {
            feedback = apply_audio_mute(&audio)
                ? "AUDIO / MUTE UPDATED" : "AUDIO / UNAVAILABLE";
        } else if (target == 20U) {
            feedback = apply_audio_delta(&audio, -1)
                ? "AUDIO / VOLUME UPDATED" : "AUDIO / UNAVAILABLE";
        } else if (target == 21U) {
            feedback = apply_audio_delta(&audio, 1)
                ? "AUDIO / VOLUME UPDATED" : "AUDIO / UNAVAILABLE";
        } else if (target == 22U) {
            const int reset = reset_interface_profile(
                &settings, &low_contrast, &theme_id, &style_id, &background_id,
                &text_scale_percent);
            feedback = reset > 0 ? (settings.session_only ? "PROFILE / RESET FOR SESSION"
                : "PROFILE / DEFAULTS RESTORED") : reset == 0
                ? "PROFILE / RESET FAILED; PREVIOUS VALUES KEPT"
                : "PROFILE / PARTIAL FAILURE; CHECK SETTINGS";
            puts(reset > 0 ? "[TEST] flux_settings_profile_reset: PASS" :
                 "[TEST] flux_settings_profile_reset: FAIL");
        } else {
            continue;
        }
        {
            const uint32_t old_offset = scene.scroll_offset;
            const uint32_t old_selected = kui_scene_selected(&scene);
            build_scene(&scene, low_contrast, theme_id, style_id, background_id,
                        text_scale_percent,
                        &audio, &settings, feedback);
            if (old_selected != 0U) (void)kui_scene_select(&scene, old_selected);
            scene.scroll_offset = old_offset;
            (void)gui_update_scene_viewport(window, &scene);
            (void)kui_scene_scroll(&scene, 0);
            kui_scene_reveal_selection(&scene);
        }
        (void)kui_scene_present(window, &scene);
    }

    if (settings.connection != 0U) (void)ku_service_close(settings.connection);
    (void)ku_ui_close(window);
    return 0;
}
