#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../userspace/gui/common.h"
#include <kurogane/settings.h>

static ku_ui_window_geometry shown, requested;
static ku_status_t geometry_status, settings_status, write_status;
static ku_settings_request last_request;
static unsigned writes;
static char stored[43];

static ku_window_t fake_open(const char* title, int x, int y, int w, int h) {
    assert(strcmp(title, "Example") == 0);
    requested = (ku_ui_window_geometry){sizeof(requested), x, y, w, h, 0, 0};
    return 7U;
}

static ku_status_t fake_get_geometry(ku_window_t window, ku_ui_window_geometry* geometry) {
    assert(window == 7U && geometry->structure_size == sizeof(*geometry));
    if (geometry_status == KU_STATUS_OK) *geometry = shown;
    return geometry_status;
}

static ku_result_t fake_connect(void) { return settings_status == KU_STATUS_OK ? 1 : settings_status; }
static ku_status_t fake_send(ku_service_connection_t connection, const void* data, size_t size) {
    assert(connection == 1 && size == sizeof(last_request));
    memcpy(&last_request, data, size);
    assert(strcmp(last_request.key, "window.example") == 0);
    if (last_request.operation == KU_SETTINGS_SET) ++writes;
    return KU_STATUS_OK;
}
static ku_status_t fake_receive(ku_service_connection_t connection, ku_service_message* message) {
    assert(connection == 1);
    ku_settings_response response = {0};
    response.structure_size = sizeof(response);
    if (last_request.operation == KU_SETTINGS_GET) {
        response.type = KU_SETTINGS_TYPE_STRING;
        response.value_size = sizeof(stored);
        memcpy(response.value, stored, sizeof(stored));
    } else {
        assert(last_request.value_size == sizeof(stored));
        response.status = write_status;
        if (write_status == KU_STATUS_OK) memcpy(stored, last_request.value, sizeof(stored));
    }
    message->data_size = sizeof(response);
    memcpy(message->data, &response, sizeof(response));
    return KU_STATUS_OK;
}
static ku_status_t fake_close(ku_ipc_handle_t connection) {
    assert(connection == 1);
    return KU_STATUS_OK;
}
ku_status_t kuro_sleep(uint64_t ticks) {
    (void)ticks;
    assert(!"unexpected Settings wait");
    return KU_STATUS_OK;
}

#define gui_open fake_open
#define ku_ui_get_geometry fake_get_geometry
#define ku_settings_connect fake_connect
#define ku_service_send fake_send
#define ku_service_receive fake_receive
#define ku_service_close fake_close
#include "../userspace/gui/window_profile.h"

static ku_ui_window_geometry sample_geometry(void) {
    ku_ui_window_geometry g = {0};
    g.structure_size = sizeof(g);
    g.x = 250; g.y = 75; g.width = 650; g.height = 630;
    g.state = KU_UI_WINDOW_NORMAL;
    return g;
}

static void test_profile_lifecycle(void) {
    gui_window_profile profile;
    const ku_ui_window_geometry preferred = sample_geometry();
    assert(gui_window_profile_encode(&preferred, stored));
    shown = preferred;
    shown.height = 380;
    shown.x = 140;
    const ku_window_t window = gui_window_profile_open(
        &profile, "window.example", "Example", 100, 80, 500, 350);
    assert(gui_window_same_geometry(&requested, &preferred));
    for (unsigned i = 0; i < 4U * GUI_WINDOW_POLL_TICKS; ++i)
        gui_window_profile_tick(&profile, window);
    gui_window_profile_flush(&profile, window);
    assert(writes == 0);

    shown.x += 20;
    shown.width += 30;
    char old_record[sizeof(stored)];
    memcpy(old_record, stored, sizeof(stored));
    geometry_status = KU_STATUS_NOT_FOUND;
    gui_window_profile_flush(&profile, window);
    assert(writes == 0);
    geometry_status = KU_STATUS_OK;
    write_status = KU_STATUS_IO_ERROR;
    gui_window_profile_flush(&profile, window);
    assert(writes == 1 && memcmp(stored, old_record, sizeof(stored)) == 0);
    write_status = KU_STATUS_OK;
    gui_window_profile_flush(&profile, window);
    gui_window_profile_flush(&profile, window);
    ku_ui_window_geometry restored;
    assert(writes == 2 && gui_window_profile_decode(stored, sizeof(stored), &restored));
    assert(gui_window_same_geometry(&restored, &shown));

    settings_status = KU_STATUS_NOT_FOUND;
    shown = (ku_ui_window_geometry){sizeof(shown), 100, 80, 500, 350, 0, 0};
    assert(gui_window_profile_open(&profile, "window.example", "Example",
                                  100, 80, 500, 350) == window);
    settings_status = KU_STATUS_OK;
    for (unsigned i = 0; i < 4U * GUI_WINDOW_POLL_TICKS; ++i)
        gui_window_profile_tick(&profile, window);
    gui_window_profile_flush(&profile, window);
    assert(writes == 2);
}

int main(void) {
    ku_ui_window_geometry original = sample_geometry();
    ku_ui_window_geometry result = {0};
    char encoded[GUI_WINDOW_PROFILE_LENGTH];
    char damaged[GUI_WINDOW_PROFILE_LENGTH];

    assert(gui_window_profile_encode(&original, encoded));
    assert(encoded[0] == 'W' && encoded[1] == '1' && encoded[42] == '\0');
    assert(gui_window_profile_decode(encoded, sizeof(encoded), &result));
    assert(gui_window_same_geometry(&original, &result));

    memcpy(damaged, encoded, sizeof(damaged));
    damaged[3] = damaged[3] == '0' ? '1' : '0';
    assert(!gui_window_profile_decode(damaged, sizeof(damaged), &result));
    memcpy(damaged, encoded, sizeof(damaged));
    damaged[40] = damaged[40] == '0' ? '1' : '0';
    assert(!gui_window_profile_decode(damaged, sizeof(damaged), &result));
    memcpy(damaged, encoded, sizeof(damaged));
    damaged[0] = 'X';
    assert(!gui_window_profile_decode(damaged, sizeof(damaged), &result));
    memcpy(damaged, encoded, sizeof(damaged));
    damaged[2] = 'Z';
    assert(!gui_window_profile_decode(damaged, sizeof(damaged), &result));
    assert(!gui_window_profile_decode(encoded, sizeof(encoded) - 1U, &result));
    assert(!gui_window_profile_decode(NULL, sizeof(encoded), &result));
    assert(!gui_window_profile_decode(encoded, sizeof(encoded), NULL));

    original.width = 0;
    assert(!gui_window_profile_encode(&original, encoded));
    original = sample_geometry();
    original.height = 159;
    assert(!gui_window_profile_encode(&original, encoded));
    original = sample_geometry();
    original.x = -1;
    assert(!gui_window_profile_encode(&original, encoded));
    original = sample_geometry();
    original.width = 16385;
    assert(!gui_window_profile_encode(&original, encoded));

    original = sample_geometry();
    original.x = 1024; original.y = 768;
    original.width = 1920; original.height = 1080;
    assert(gui_window_profile_encode(&original, encoded));
    assert(gui_window_profile_decode(encoded, sizeof(encoded), &result));
    assert(gui_window_same_geometry(&original, &result));
    result.width += 1;
    assert(!gui_window_same_geometry(&original, &result));

    test_profile_lifecycle();
    return 0;
}
