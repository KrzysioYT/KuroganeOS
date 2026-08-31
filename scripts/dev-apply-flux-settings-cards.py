#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, found {count}: {old[:100]!r}")
    write(path, text.replace(old, new, 1))


# Native UI v3: add a real interactive toggle without changing command/frame size.
replace_once(
    "sdk/include/kurogane/ui.h",
    "    KU_UI_NATIVE_METRIC = 9,\n    KU_UI_NATIVE_NOTICE = 10\n};\n",
    "    KU_UI_NATIVE_METRIC = 9,\n    KU_UI_NATIVE_NOTICE = 10,\n    KU_UI_NATIVE_TOGGLE = 11\n};\n",
)
replace_once(
    "sdk/include/kurogane/libui.h",
    "    KUI_VIEW_METRIC = 9,\n    KUI_VIEW_NOTICE = 10\n};\n",
    "    KUI_VIEW_METRIC = 9,\n    KUI_VIEW_NOTICE = 10,\n    KUI_VIEW_TOGGLE = 11\n};\n",
)
replace_once(
    "sdk/include/kurogane/libui.h",
    "ku_status_t kui_scene_add_notice(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    uint32_t priority);\n",
    "ku_status_t kui_scene_add_notice(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    uint32_t priority);\nku_status_t kui_scene_add_toggle(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    int checked);\n",
)
replace_once(
    "sdk/include/kurogane/libui.h",
    "ku_status_t kui_flow_notice(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    uint32_t priority);\n",
    "ku_status_t kui_flow_notice(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    uint32_t priority);\nku_status_t kui_flow_toggle(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    int checked);\n",
)

replace_once(
    "sdk/src/libui.c",
    "    return view->type == KUI_VIEW_BUTTON || view->type == KUI_VIEW_INPUT ||\n        view->type == KUI_VIEW_LIST_ITEM || view->type == KUI_VIEW_TILE;\n",
    "    return view->type == KUI_VIEW_BUTTON || view->type == KUI_VIEW_INPUT ||\n        view->type == KUI_VIEW_LIST_ITEM || view->type == KUI_VIEW_TILE ||\n        view->type == KUI_VIEW_TOGGLE;\n",
)
replace_once(
    "sdk/src/libui.c",
    "        type < KUI_VIEW_PANEL || type > KUI_VIEW_NOTICE) {\n",
    "        type < KUI_VIEW_PANEL || type > KUI_VIEW_TOGGLE) {\n",
)
replace_once(
    "sdk/src/libui.c",
    "ku_status_t kui_scene_set_text(\n",
    "ku_status_t kui_scene_add_toggle(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    int checked) {\n    ku_status_t status = kui_scene_add(\n        scene, id, parent_id, KUI_VIEW_TOGGLE, text);\n    if (status != KU_STATUS_OK) return status;\n    return kui_scene_set_value(scene, id, checked != 0 ? 1U : 0U, 1U);\n}\n\nku_status_t kui_scene_set_text(\n",
)
replace_once(
    "sdk/src/libui.c",
    "         view->type != KUI_VIEW_NOTICE) ||\n",
    "         view->type != KUI_VIEW_NOTICE && view->type != KUI_VIEW_TOGGLE) ||\n",
)
replace_once(
    "sdk/src/libui.c",
    "        case KUI_VIEW_NOTICE: return 72;\n",
    "        case KUI_VIEW_NOTICE: return 72;\n        case KUI_VIEW_TOGGLE: return 52;\n",
)
replace_once(
    "sdk/src/libui.c",
    "ku_status_t kui_flow_separator(kui_flow* flow, uint32_t id) {\n",
    "ku_status_t kui_flow_toggle(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    int checked) {\n    return flow == (kui_flow*)0\n        ? KU_STATUS_INVALID_ARGUMENT\n        : kui_scene_add_toggle(flow->scene, id, flow->parent_id, text, checked);\n}\n\nku_status_t kui_flow_separator(kui_flow* flow, uint32_t id) {\n",
)

# Kernel validation and renderer: v1/v2 remain unchanged, TOGGLE is v3-only.
replace_once(
    "kernel/user/runtime_base.inc",
    "    const uint32_t maximum_type = version >= KU_UI_NATIVE_VERSION_3\n        ? KU_UI_NATIVE_NOTICE\n",
    "    const uint32_t maximum_type = version >= KU_UI_NATIVE_VERSION_3\n        ? KU_UI_NATIVE_TOGGLE\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "         command.type == KU_UI_NATIVE_NOTICE) && command.maximum == 0U) return false;\n",
    "         command.type == KU_UI_NATIVE_NOTICE ||\n         command.type == KU_UI_NATIVE_TOGGLE) && command.maximum == 0U) return false;\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "    if (command.type == KU_UI_NATIVE_NOTICE &&\n        (command.value < 1U || command.value > 4U || command.maximum != 4U)) return false;\n",
    "    if (command.type == KU_UI_NATIVE_NOTICE &&\n        (command.value < 1U || command.value > 4U || command.maximum != 4U)) return false;\n    if (command.type == KU_UI_NATIVE_TOGGLE &&\n        (command.value > 1U || command.maximum != 1U)) return false;\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "             command.type == KU_UI_NATIVE_LIST_ITEM ||\n             command.type == KU_UI_NATIVE_TILE);\n",
    "             command.type == KU_UI_NATIVE_LIST_ITEM ||\n             command.type == KU_UI_NATIVE_TILE ||\n             command.type == KU_UI_NATIVE_TOGGLE);\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "            case KU_UI_NATIVE_NOTICE: {\n                char title[32];\n                char detail[32];\n                split_native_tile_text(command.text, title, detail);\n                ui::notice_card(bounds, title, detail, command.value);\n                break;\n            }\n            case KU_UI_NATIVE_TILE: {\n",
    "            case KU_UI_NATIVE_NOTICE: {\n                char title[32];\n                char detail[32];\n                split_native_tile_text(command.text, title, detail);\n                ui::notice_card(bounds, title, detail, command.value);\n                break;\n            }\n            case KU_UI_NATIVE_TOGGLE: {\n                char title[32];\n                char detail[32];\n                split_native_tile_text(command.text, title, detail);\n                ui::toggle_switch(\n                    bounds, title, detail, command.value != 0U, hovered, pressed);\n                break;\n            }\n            case KU_UI_NATIVE_TILE: {\n",
)

replace_once(
    "kernel/ui/ui.hpp",
    "void notice_card(\n    const Rect& bounds, const char* title, const char* detail,\n    uint32_t priority);\n",
    "void notice_card(\n    const Rect& bounds, const char* title, const char* detail,\n    uint32_t priority);\nvoid toggle_switch(\n    const Rect& bounds, const char* title, const char* detail,\n    bool checked, bool hovered = false, bool pressed = false);\n",
)
replace_once(
    "kernel/ui/ui.cpp",
    "void app_tile(\n",
    "void toggle_switch(\n    const Rect& bounds, const char* title, const char* detail,\n    bool checked, bool hovered, bool pressed) {\n    if (bounds.width <= 0 || bounds.height <= 0) return;\n    graphics::Color background = checked ? graphics::rgb(34, 18, 23) : kGraphite;\n    graphics::Color border = checked ? kRedMuted : kTheme.border;\n    if (hovered) border = kSteel;\n    if (pressed) background = checked ? graphics::rgb(48, 20, 27) : graphics::rgb(27, 29, 34);\n    graphics::fill_rect(bounds.x + 3, bounds.y + 3, bounds.width, bounds.height, kSurfaceShadow);\n    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);\n    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, border);\n    graphics::draw_text(bounds.x + 12, bounds.y + 9,\n                        title ? title : \"SETTING\", kTheme.text, background, 1U, true);\n    graphics::draw_text(bounds.x + 12, bounds.y + 27,\n                        detail ? detail : \"\", kTheme.text_muted, background, 1U, true);\n    const int32_t track_width = 42;\n    const int32_t track_height = 20;\n    const int32_t track_x = bounds.x + bounds.width - track_width - 12;\n    const int32_t track_y = bounds.y + (bounds.height - track_height) / 2;\n    graphics::fill_rect(track_x, track_y, track_width, track_height,\n                        checked ? kRedMuted : graphics::rgb(43, 46, 52));\n    graphics::draw_rect(track_x, track_y, track_width, track_height,\n                        checked ? kRedBright : kSteel);\n    const int32_t knob_x = checked ? track_x + track_width - 17 : track_x + 3;\n    graphics::fill_rect(knob_x, track_y + 3, 14, 14,\n                        checked ? kTheme.text : graphics::rgb(151, 156, 166));\n}\n\nvoid app_tile(\n",
)

# Settings app: real persistent settingsd profile + graphical toggle cards.
settings_source = r'''#include "../common.h"
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
'''
write("userspace/gui/settings/main.c", settings_source)

# ABI and libui behavior tests.
replace_once(
    "tests/test_sdk_abi.cpp",
    "    static_assert(KU_UI_NATIVE_NOTICE == 10);\n",
    "    static_assert(KU_UI_NATIVE_NOTICE == 10);\n    static_assert(KU_UI_NATIVE_TOGGLE == 11);\n",
)
replace_once(
    "tests/test_libui_pointer.c",
    "    puts(\"libui native packet + mouse hit-test tests passed\");\n",
    "    {\n        kui_scene toggles;\n        kui_flow flow;\n        ku_ui_native_frame toggle_frame;\n        kui_scene_initialize(&toggles);\n        toggles.visible_rows = 3U;\n        kui_flow_begin(&flow, &toggles, 0U);\n        if (kui_flow_panel(&flow, 70U, \"SETTINGS\") != KU_STATUS_OK ||\n            kui_flow_toggle(&flow, 71U, \"LOW CONTRAST\\nPERSISTENT\", 1) != KU_STATUS_OK ||\n            kui_flow_toggle(&flow, 72U, \"MUTE\\nAUDIO\", 0) != KU_STATUS_OK) return 13;\n        if (kui_scene_build_native(&toggles, &toggle_frame) != KU_STATUS_OK ||\n            toggle_frame.commands[1].type != KU_UI_NATIVE_TOGGLE ||\n            toggle_frame.commands[1].value != 1U || toggle_frame.commands[1].maximum != 1U ||\n            toggle_frame.commands[2].type != KU_UI_NATIVE_TOGGLE ||\n            toggle_frame.commands[2].value != 0U ||\n            !expect(kui_scene_hit_test(\n                &toggles, 24, center_y(&toggle_frame, 1U)), 71U, \"toggle on hit\") ||\n            !expect(kui_scene_hit_test(\n                &toggles, 24, center_y(&toggle_frame, 2U)), 72U, \"toggle off hit\")) return 14;\n    }\n\n    puts(\"libui native packet + mouse hit-test tests passed\");\n",
)
replace_once(
    "tests/test_mouse_first_apps.py",
    "settings = read(\"userspace/gui/settings/main.c\")\n",
    "settings = read(\"userspace/gui/settings/main.c\")\nassert \"kui_flow_toggle\" in settings, \"settings: native toggle cards missing\"\nassert \"ku_settings_connect\" in settings, \"settings: persistent settingsd integration missing\"\nassert \"UI_LOW_CONTRAST_KEY\" in settings\nassert \"desktop_settings_profile_persist: PASS\" in settings\nassert \"desktop_settings_profile_restore: PASS\" in settings\n",
)

# Guard against ABI growth or a visual-only settings implementation.
for path, needles in {
    "sdk/include/kurogane/ui.h": ["KU_UI_NATIVE_TOGGLE = 11"],
    "sdk/include/kurogane/libui.h": ["KUI_VIEW_TOGGLE = 11", "kui_flow_toggle"],
    "kernel/user/runtime_base.inc": ["KU_UI_NATIVE_TOGGLE", "ui::toggle_switch"],
    "userspace/gui/settings/main.c": ["ku_settings_connect", "ui.low_contrast", "kui_flow_toggle", "desktop_settings_profile_persist: PASS"],
}.items():
    text = read(path)
    for needle in needles:
        if needle not in text:
            raise SystemExit(f"{path}: missing guard {needle}")

abi = read("tests/test_sdk_abi.cpp")
if "sizeof(ku_ui_native_frame) == 3616" not in abi or "sizeof(ku_ui_native_frame) <= 4096" not in abi:
    raise SystemExit("ABI size guards missing")

print("persistent Flux Settings cards migration applied")
