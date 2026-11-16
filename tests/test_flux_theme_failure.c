#include <assert.h>
#include "../userspace/gui/common.h"
#include <kurogane/settings.h>
#include <kurogane/desktop.h>

static uint64_t stored, live;
static int present, reject_apply, reject_rollback, timeout_after_set;
static unsigned writes, closed, opened;
static ku_service_connection_t connection;
static ku_settings_response reply;
static int reply_delayed;

static ku_status_t fake_send(ku_service_connection_t handle, const void* data, size_t size) {
    const ku_settings_request* request = data;
    assert(handle == connection && size == sizeof(*request));
    assert(strcmp(request->key, "ui.theme") == 0);
    reply = (ku_settings_response){.structure_size = sizeof(reply)};
    reply_delayed = 0;
    if (request->operation == KU_SETTINGS_GET) {
        if (present) {
            reply.type = KU_SETTINGS_TYPE_U64;
            reply.value_size = sizeof(stored);
            memcpy(reply.value, &stored, sizeof(stored));
        } else {
            reply.status = KU_STATUS_NOT_FOUND;
        }
    } else if (request->operation == KU_SETTINGS_DELETE) {
        present = 0;
    } else {
        assert(request->operation == KU_SETTINGS_SET);
        ++writes;
        if (reject_rollback && writes > 1U) {
            reply.status = KU_STATUS_IO_ERROR;
        } else {
            memcpy(&stored, request->value, sizeof(stored));
            present = 1;
            reply_delayed = timeout_after_set && writes == 1U;
        }
    }
    return KU_STATUS_OK;
}

static ku_status_t fake_receive(ku_service_connection_t handle, ku_service_message* message) {
    assert(handle == connection);
    if (reply_delayed) return KU_STATUS_WOULD_BLOCK;
    message->data_size = sizeof(reply);
    memcpy(message->data, &reply, sizeof(reply));
    return KU_STATUS_OK;
}

static ku_result_t fake_connect(void) {
    ++opened;
    return (ku_result_t)++connection;
}

static ku_status_t fake_close(ku_service_connection_t handle) {
    assert(handle == connection);
    ++closed;
    return KU_STATUS_OK;
}

ku_status_t kuro_sleep(uint64_t ticks) { (void)ticks; return KU_STATUS_OK; }

static size_t test_strlcpy(char* output, const char* input, size_t capacity) {
    const size_t length = strlen(input);
    if (capacity != 0U) {
        const size_t count = length < capacity ? length : capacity - 1U;
        memcpy(output, input, count);
        output[count] = '\0';
    }
    return length;
}

static ku_status_t fake_theme(ku_desktop_theme_request* request) {
    assert(request->action == KU_DESKTOP_THEME_SET);
    if (reject_apply && request->theme_id == KU_FLUX_THEME_VIOLET) return KU_STATUS_BAD_STATE;
    live = request->theme_id;
    return KU_STATUS_OK;
}

#define ku_service_send fake_send
#define ku_service_receive fake_receive
#define ku_service_close fake_close
#define ku_settings_connect fake_connect
#define ku_desktop_theme fake_theme
#define strlcpy test_strlcpy
#define main settings_main
#include "../userspace/gui/settings/main.c"
#undef main

static int apply(settings_client* client) {
    return apply_appearance_choice(client, UI_THEME_KEY, KU_FLUX_THEME_VIOLET,
        KU_FLUX_THEME_RED, read_theme_setting, write_theme_setting, select_desktop_theme);
}

int main(void) {
    settings_client client = {.connection = 1U, .connected = 1};
    connection = 1U;
    stored = KU_FLUX_THEME_STEEL;
    present = reject_apply = 1;
    assert(apply(&client) == 0 && stored == KU_FLUX_THEME_STEEL && live == KU_FLUX_THEME_RED);
    present = 0;
    assert(apply(&client) == 0 && !present && live == KU_FLUX_THEME_RED);
    writes = 0U;
    present = reject_rollback = 1;
    stored = KU_FLUX_THEME_RED;
    assert(apply(&client) == -1 && stored == KU_FLUX_THEME_VIOLET && live == KU_FLUX_THEME_RED);
    writes = 0U;
    reject_rollback = reject_apply = 0;
    timeout_after_set = 1;
    stored = KU_FLUX_THEME_RED;
    assert(apply(&client) == 0 && stored == KU_FLUX_THEME_RED && live == KU_FLUX_THEME_RED);
    assert(closed == 1U && opened == 1U && client.connection == 2U);
    assert(apply(&client) == 1 && stored == KU_FLUX_THEME_VIOLET && live == KU_FLUX_THEME_VIOLET);
    return 0;
}
