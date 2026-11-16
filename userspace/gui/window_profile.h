#ifndef KUROGANE_GUI_WINDOW_PROFILE_H
#define KUROGANE_GUI_WINDOW_PROFILE_H

#include "common.h"
#include <kurogane/settings.h>

#define GUI_WINDOW_PROFILE_LENGTH 43U
#define GUI_WINDOW_POLL_TICKS 20U

typedef struct gui_window_profile {
    const char* key;
    ku_ui_window_geometry sample;
    ku_ui_window_geometry saved;
    uint32_t ticks;
    uint32_t stable_samples;
    uint32_t retry_after;
    int have_sample;
    int have_saved;
} gui_window_profile;

static inline uint32_t gui_window_hash(const char* text, size_t length) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t index = 0U; index < length; ++index) {
        hash ^= (uint8_t)text[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}
static inline void gui_window_write_hex(char* out, uint32_t value) {
    static const char chars[] = "0123456789abcdef";
    for (size_t index = 0U; index < 8U; ++index)
        out[index] = chars[(value >> (28U - 4U * index)) & 15U];
}
static inline int gui_window_read_hex(const char* text, uint32_t* out) {
    uint32_t value = 0U;
    if (text == NULL || out == NULL) return 0;
    for (size_t index = 0U; index < 8U; ++index) {
        const char c = text[index];
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (uint32_t)(c - 'a' + 10);
        else return 0;
        value = (value << 4U) | digit;
    }
    *out = value;
    return 1;
}
static inline int gui_window_geometry_valid(const ku_ui_window_geometry* g) {
    return g != NULL && g->structure_size == sizeof(*g) &&
        g->x >= 0 && g->y >= 0 &&
        g->width >= 260 && g->height >= 160 &&
        g->width <= 16384 && g->height <= 16384 &&
        g->x <= 16384 && g->y <= 16384;
}
static inline int gui_window_profile_encode(
    const ku_ui_window_geometry* geometry,
    char output[GUI_WINDOW_PROFILE_LENGTH]) {
    if (output == NULL || !gui_window_geometry_valid(geometry)) return 0;
    const uint32_t values[4] = {
        (uint32_t)geometry->x, (uint32_t)geometry->y,
        (uint32_t)geometry->width, (uint32_t)geometry->height
    };
    output[0] = 'W'; output[1] = '1';
    for (size_t index = 0U; index < 4U; ++index)
        gui_window_write_hex(output + 2U + 8U * index, values[index]);
    gui_window_write_hex(output + 34U, gui_window_hash(output, 34U));
    output[42] = '\0';
    return 1;
}
static inline int gui_window_profile_decode(
    const char* input, size_t length, ku_ui_window_geometry* geometry) {
    uint32_t values[4], expected = 0U;
    if (input == NULL || geometry == NULL || length != GUI_WINDOW_PROFILE_LENGTH ||
        input[0] != 'W' || input[1] != '1' || input[42] != '\0') return 0;
    for (size_t index = 0U; index < 4U; ++index)
        if (!gui_window_read_hex(input + 2U + 8U * index, &values[index]))
            return 0;
    if (!gui_window_read_hex(input + 34U, &expected) ||
        expected != gui_window_hash(input, 34U) ||
        values[0] > INT32_MAX || values[1] > INT32_MAX ||
        values[2] > INT32_MAX || values[3] > INT32_MAX) return 0;
    ku_ui_window_geometry candidate = {0};
    candidate.structure_size = sizeof(candidate);
    candidate.x = (int32_t)values[0];
    candidate.y = (int32_t)values[1];
    candidate.width = (int32_t)values[2];
    candidate.height = (int32_t)values[3];
    if (!gui_window_geometry_valid(&candidate)) return 0;
    *geometry = candidate;
    return 1;
}
static inline int gui_window_same_geometry(
    const ku_ui_window_geometry* left, const ku_ui_window_geometry* right) {
    return left->x == right->x && left->y == right->y &&
        left->width == right->width && left->height == right->height;
}

/* One operation per connection. Closing after timeout prevents late replies
 * from being confused with the response to a subsequent write. */
static inline ku_status_t gui_window_settings_request(
    const char* key, uint32_t operation,
    const char* value, ku_settings_response* response) {
    if (key == NULL || response == NULL ||
        strlen(key) >= KU_SETTINGS_KEY_CAPACITY ||
        (operation == KU_SETTINGS_SET && value == NULL))
        return KU_STATUS_INVALID_ARGUMENT;
    const ku_result_t result = ku_settings_connect();
    if (result <= 0) return (ku_status_t)result;
    const ku_service_connection_t connection = (ku_service_connection_t)result;
    ku_settings_request request = {0};
    request.structure_size = sizeof(request);
    request.operation = operation;
    request.type = operation == KU_SETTINGS_SET
        ? KU_SETTINGS_TYPE_STRING : KU_SETTINGS_TYPE_NONE;
    (void)strlcpy(request.key, key, sizeof(request.key));
    if (operation == KU_SETTINGS_SET) {
        request.value_size = GUI_WINDOW_PROFILE_LENGTH;
        memcpy(request.value, value, GUI_WINDOW_PROFILE_LENGTH);
    }
    ku_status_t status = ku_service_send(connection, &request, sizeof(request));
    if (status == KU_STATUS_OK) {
        status = KU_STATUS_TIMED_OUT;
        for (uint32_t attempt = 0U; attempt < 100U; ++attempt) {
            ku_service_message message;
            const ku_status_t received = ku_service_receive(connection, &message);
            if (received == KU_STATUS_WOULD_BLOCK) {
                (void)kuro_sleep(1U);
                continue;
            }
            status = received;
            if (received == KU_STATUS_OK) {
                if (message.data_size != sizeof(*response)) {
                    status = KU_STATUS_CORRUPT_DATA;
                } else {
                    memcpy(response, message.data, sizeof(*response));
                    if (response->structure_size != sizeof(*response) ||
                        response->value_size > KU_SETTINGS_VALUE_CAPACITY)
                        status = KU_STATUS_CORRUPT_DATA;
                    else
                        status = response->status;
                }
            }
            break;
        }
    }
    (void)ku_service_close(connection);
    return status;
}

static inline int gui_window_profile_load(
    const char* key, ku_ui_window_geometry* geometry) {
    ku_settings_response response = {0};
    if (gui_window_settings_request(key, KU_SETTINGS_GET, NULL, &response) !=
            KU_STATUS_OK ||
        response.type != KU_SETTINGS_TYPE_STRING ||
        response.value_size != GUI_WINDOW_PROFILE_LENGTH) return 0;
    return gui_window_profile_decode(
        (const char*)response.value, response.value_size, geometry);
}
static inline int gui_window_profile_save(
    const char* key, const ku_ui_window_geometry* geometry) {
    char value[GUI_WINDOW_PROFILE_LENGTH];
    ku_settings_response response = {0};
    return gui_window_profile_encode(geometry, value) &&
        gui_window_settings_request(key, KU_SETTINGS_SET, value, &response) ==
            KU_STATUS_OK;
}

static inline ku_window_t gui_window_profile_open(
    gui_window_profile* profile, const char* key,
    const char* title, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (profile == NULL) return KU_INVALID_WINDOW;
    memset(profile, 0, sizeof(*profile));
    profile->key = key;
    ku_ui_window_geometry restored = {0};
    if (gui_window_profile_load(key, &restored)) {
        x = restored.x; y = restored.y;
        width = restored.width; height = restored.height;
        profile->saved = restored;
        profile->have_saved = 1;
    }
    return gui_open(title, x, y, width, height);
}
static inline void gui_window_profile_tick(
    gui_window_profile* profile, ku_window_t window) {
    if (profile == NULL || profile->key == NULL ||
        window == KU_INVALID_WINDOW || ++profile->ticks < GUI_WINDOW_POLL_TICKS)
        return;
    profile->ticks = 0U;
    ku_ui_window_geometry current = {0};
    current.structure_size = sizeof(current);
    if (ku_ui_get_geometry(window, &current) != KU_STATUS_OK ||
        !gui_window_geometry_valid(&current)) return;
    if (!profile->have_sample ||
        !gui_window_same_geometry(&profile->sample, &current)) {
        profile->sample = current;
        profile->have_sample = 1;
        profile->stable_samples = 0U;
        return;
    }
    if (profile->stable_samples < 2U) ++profile->stable_samples;
    if (profile->stable_samples < 2U ||
        (profile->have_saved && gui_window_same_geometry(
            &profile->saved, &current))) return;
    if (profile->retry_after != 0U) {
        --profile->retry_after;
        return;
    }
    if (gui_window_profile_save(profile->key, &current)) {
        profile->saved = current;
        profile->have_saved = 1;
        puts("[WINDOW] geometry_saved");
    } else {
        profile->retry_after = 10U;
    }
}
static inline void gui_window_profile_flush(gui_window_profile* profile) {
    if (profile == NULL || !profile->have_sample ||
        (profile->have_saved &&
         gui_window_same_geometry(&profile->sample, &profile->saved))) return;
    if (gui_window_profile_save(profile->key, &profile->sample)) {
        profile->saved = profile->sample;
        profile->have_saved = 1;
    }
}

#endif
