#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, found {count}: {old[:100]!r}")
    write(path, text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# notifications.v1: explicit public-feed visibility without exposing private
# owner records. Existing request/response structs stay binary-identical.
# ---------------------------------------------------------------------------
replace_once(
    "sdk/include/kurogane/notification.h",
    "    KU_NOTIFICATION_POST = 1,\n    KU_NOTIFICATION_GET = 2,\n    KU_NOTIFICATION_DISMISS = 3\n};\n",
    "    KU_NOTIFICATION_POST = 1,\n    KU_NOTIFICATION_GET = 2,\n    KU_NOTIFICATION_DISMISS = 3,\n    KU_NOTIFICATION_LIST_PUBLIC = 4\n};\n",
)
replace_once(
    "sdk/include/kurogane/notification.h",
    "enum ku_notification_state {\n    KU_NOTIFICATION_STATE_ACTIVE = 1,\n    KU_NOTIFICATION_STATE_DISMISSED = 2\n};\n",
    "enum ku_notification_state {\n    KU_NOTIFICATION_STATE_ACTIVE = 1,\n    KU_NOTIFICATION_STATE_DISMISSED = 2\n};\n\nenum ku_notification_flags {\n    /* Explicit opt-in: record may be enumerated by Notification Center. */\n    KU_NOTIFICATION_FLAG_PUBLIC = UINT32_C(1) << 0\n};\n",
)

replace_once(
    "userspace/system/notificationd/main.c",
    "static notificationd_record* find_record(uint64_t owner_pid, uint64_t id) {\n",
    "static notificationd_record* find_public_after(uint64_t cursor) {\n    notificationd_record* best = (notificationd_record*)0;\n    size_t index = 0U;\n    for (; index < NOTIFICATIOND_MAX_RECORDS; ++index) {\n        notificationd_record* record = &records[index];\n        if (!record->active ||\n            (record->flags & KU_NOTIFICATION_FLAG_PUBLIC) == 0U ||\n            record->id <= cursor) continue;\n        if (best == (notificationd_record*)0 || record->id < best->id) best = record;\n    }\n    return best;\n}\n\nstatic notificationd_record* find_record(uint64_t owner_pid, uint64_t id) {\n",
)
replace_once(
    "userspace/system/notificationd/main.c",
    "    if (request->notification_id != 0U || request->reserved != 0U ||\n        request->flags != 0U || !type_valid(request->type) ||\n",
    "    if (request->notification_id != 0U || request->reserved != 0U ||\n        (request->flags & ~KU_NOTIFICATION_FLAG_PUBLIC) != 0U || !type_valid(request->type) ||\n",
)
replace_once(
    "userspace/system/notificationd/main.c",
    "        case KU_NOTIFICATION_GET:\n",
    "        case KU_NOTIFICATION_LIST_PUBLIC:\n            if (request->reserved != 0U || request->flags != 0U ||\n                request->type != 0U || request->priority != 0U ||\n                request->title[0] != '\\0' || request->body[0] != '\\0') {\n                (void)send_response(\n                    client->connection, KU_STATUS_INVALID_ARGUMENT,\n                    (const notificationd_record*)0, 0U);\n                break;\n            }\n            record = find_public_after(request->notification_id);\n            (void)send_response(\n                client->connection,\n                record != (notificationd_record*)0 ? KU_STATUS_OK : KU_STATUS_NOT_FOUND,\n                record,\n                record != (notificationd_record*)0 ? KU_NOTIFICATION_STATE_ACTIVE : 0U);\n            break;\n        case KU_NOTIFICATION_GET:\n",
)

# Runtime probe now qualifies that an opted-in record can be discovered via the
# public feed before its owner dismisses it.
replace_once(
    "userspace/system/notification-probe/main.c",
    "    request.priority = KU_NOTIFICATION_PRIORITY_HIGH;\n    copy_text(request.title, sizeof(request.title), \"Road to 15\");\n",
    "    request.priority = KU_NOTIFICATION_PRIORITY_HIGH;\n    request.flags = KU_NOTIFICATION_FLAG_PUBLIC;\n    copy_text(request.title, sizeof(request.title), \"Road to 15\");\n",
)
replace_once(
    "userspace/system/notification-probe/main.c",
    "static ku_status_t by_id(\n",
    "static ku_status_t list_public(\n    ku_service_connection_t connection,\n    uint64_t cursor,\n    ku_notification_response* response) {\n    ku_notification_request request;\n    clear_bytes(&request, sizeof(request));\n    request.structure_size = sizeof(request);\n    request.operation = KU_NOTIFICATION_LIST_PUBLIC;\n    request.notification_id = cursor;\n    return transact(connection, &request, response);\n}\n\nstatic ku_status_t by_id(\n",
)
replace_once(
    "userspace/system/notification-probe/main.c",
    "    (void)u_puts(\"[TEST] notification_service_post: PASS\\n\");\n\n    if (by_id(\n",
    "    (void)u_puts(\"[TEST] notification_service_post: PASS\\n\");\n\n    {\n        uint64_t cursor = 0U;\n        uint32_t attempts = 0U;\n        int found = 0;\n        while (attempts++ < NOTIFICATIOND_MAX_RECORDS) {\n            const ku_status_t listed = list_public(\n                (ku_service_connection_t)connected, cursor, &response);\n            if (listed == KU_STATUS_NOT_FOUND) break;\n            if (listed != KU_STATUS_OK ||\n                (response.flags & KU_NOTIFICATION_FLAG_PUBLIC) == 0U ||\n                response.notification_id <= cursor) {\n                (void)u_puts(\"[TEST] notification_service_public_list: FAIL\\n\");\n                ku_exit(6);\n            }\n            cursor = response.notification_id;\n            if (cursor == id) { found = 1; break; }\n        }\n        if (!found) {\n            (void)u_puts(\"[TEST] notification_service_public_list: FAIL\\n\");\n            ku_exit(6);\n        }\n        (void)u_puts(\"[TEST] notification_service_public_list: PASS\\n\");\n    }\n\n    if (by_id(\n",
)
# The probe does not include notificationd internals; use the public service max
# bounded by the daemon contract instead of its private macro.
replace_once(
    "userspace/system/notification-probe/main.c",
    "        while (attempts++ < NOTIFICATIOND_MAX_RECORDS) {\n",
    "        while (attempts++ < 32U) {\n",
)

# ---------------------------------------------------------------------------
# Native UI v3 notice cards + notification glyph. Packet size is unchanged.
# ---------------------------------------------------------------------------
replace_once(
    "sdk/include/kurogane/ui.h",
    "    KU_UI_NATIVE_TILE = 8,\n    KU_UI_NATIVE_METRIC = 9\n};\n",
    "    KU_UI_NATIVE_TILE = 8,\n    KU_UI_NATIVE_METRIC = 9,\n    KU_UI_NATIVE_NOTICE = 10\n};\n",
)
replace_once(
    "sdk/include/kurogane/ui.h",
    "    KU_UI_NATIVE_ICON_FOLDER = 8,\n    KU_UI_NATIVE_ICON_DOCUMENT = 9,\n    KU_UI_NATIVE_ICON_COUNT = 10\n};\n",
    "    KU_UI_NATIVE_ICON_FOLDER = 8,\n    KU_UI_NATIVE_ICON_DOCUMENT = 9,\n    KU_UI_NATIVE_ICON_NOTIFICATION = 10,\n    KU_UI_NATIVE_ICON_COUNT = 11\n};\n",
)
replace_once(
    "sdk/include/kurogane/libui.h",
    "    KUI_VIEW_TILE = 8,\n    KUI_VIEW_METRIC = 9\n};\n",
    "    KUI_VIEW_TILE = 8,\n    KUI_VIEW_METRIC = 9,\n    KUI_VIEW_NOTICE = 10\n};\n",
)
replace_once(
    "sdk/include/kurogane/libui.h",
    "ku_status_t kui_scene_add_metric(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum);\n",
    "ku_status_t kui_scene_add_metric(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum);\nku_status_t kui_scene_add_notice(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    uint32_t priority);\n",
)
replace_once(
    "sdk/include/kurogane/libui.h",
    "ku_status_t kui_flow_metric(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum);\n",
    "ku_status_t kui_flow_metric(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum);\nku_status_t kui_flow_notice(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    uint32_t priority);\n",
)
replace_once(
    "sdk/src/libui.c",
    "        type < KUI_VIEW_PANEL || type > KUI_VIEW_METRIC) {\n",
    "        type < KUI_VIEW_PANEL || type > KUI_VIEW_NOTICE) {\n",
)
replace_once(
    "sdk/src/libui.c",
    "ku_status_t kui_scene_set_text(\n",
    "ku_status_t kui_scene_add_notice(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    uint32_t priority) {\n    ku_status_t status;\n    if (priority < 1U || priority > 4U) return KU_STATUS_INVALID_ARGUMENT;\n    status = kui_scene_add(scene, id, parent_id, KUI_VIEW_NOTICE, text);\n    if (status != KU_STATUS_OK) return status;\n    return kui_scene_set_value(scene, id, priority, 4U);\n}\n\nku_status_t kui_scene_set_text(\n",
)
replace_once(
    "sdk/src/libui.c",
    "        (view->type != KUI_VIEW_PROGRESS && view->type != KUI_VIEW_METRIC) ||\n",
    "        (view->type != KUI_VIEW_PROGRESS && view->type != KUI_VIEW_METRIC &&\n         view->type != KUI_VIEW_NOTICE) ||\n",
)
replace_once(
    "sdk/src/libui.c",
    "        case KUI_VIEW_METRIC: return KUI_METRIC_HEIGHT;\n",
    "        case KUI_VIEW_METRIC: return KUI_METRIC_HEIGHT;\n        case KUI_VIEW_NOTICE: return 72;\n",
)
replace_once(
    "sdk/src/libui.c",
    "ku_status_t kui_flow_separator(kui_flow* flow, uint32_t id) {\n",
    "ku_status_t kui_flow_notice(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    uint32_t priority) {\n    return flow == (kui_flow*)0\n        ? KU_STATUS_INVALID_ARGUMENT\n        : kui_scene_add_notice(flow->scene, id, flow->parent_id, text, priority);\n}\n\nku_status_t kui_flow_separator(kui_flow* flow, uint32_t id) {\n",
)

replace_once(
    "kernel/user/runtime_base.inc",
    "    const uint32_t maximum_type = version >= KU_UI_NATIVE_VERSION_3\n        ? KU_UI_NATIVE_METRIC\n",
    "    const uint32_t maximum_type = version >= KU_UI_NATIVE_VERSION_3\n        ? KU_UI_NATIVE_NOTICE\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "    if ((command.type == KU_UI_NATIVE_PROGRESS ||\n         command.type == KU_UI_NATIVE_METRIC) && command.maximum == 0U) return false;\n",
    "    if ((command.type == KU_UI_NATIVE_PROGRESS ||\n         command.type == KU_UI_NATIVE_METRIC ||\n         command.type == KU_UI_NATIVE_NOTICE) && command.maximum == 0U) return false;\n    if (command.type == KU_UI_NATIVE_NOTICE &&\n        (command.value < 1U || command.value > 4U || command.maximum != 4U)) return false;\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "            case KU_UI_NATIVE_METRIC: {\n                char title[32];\n                char detail[32];\n                split_native_tile_text(command.text, title, detail);\n                ui::metric_card(\n                    bounds, title, detail, command.value, command.maximum);\n                break;\n            }\n            case KU_UI_NATIVE_TILE: {\n",
    "            case KU_UI_NATIVE_METRIC: {\n                char title[32];\n                char detail[32];\n                split_native_tile_text(command.text, title, detail);\n                ui::metric_card(\n                    bounds, title, detail, command.value, command.maximum);\n                break;\n            }\n            case KU_UI_NATIVE_NOTICE: {\n                char title[32];\n                char detail[32];\n                split_native_tile_text(command.text, title, detail);\n                ui::notice_card(bounds, title, detail, command.value);\n                break;\n            }\n            case KU_UI_NATIVE_TILE: {\n",
)
replace_once(
    "kernel/ui/ui.hpp",
    "    Folder = 8,\n    Document = 9,\n};\n",
    "    Folder = 8,\n    Document = 9,\n    Notification = 10,\n};\n",
)
replace_once(
    "kernel/ui/ui.hpp",
    "void metric_card(\n    const Rect& bounds, const char* title, const char* detail,\n    uint32_t value, uint32_t maximum);\n",
    "void metric_card(\n    const Rect& bounds, const char* title, const char* detail,\n    uint32_t value, uint32_t maximum);\nvoid notice_card(\n    const Rect& bounds, const char* title, const char* detail,\n    uint32_t priority);\n",
)
replace_once(
    "kernel/ui/ui.cpp",
    "        case AppIcon::Document:\n            graphics::draw_rect(x + 4, y, 22, 27, foreground);\n            graphics::fill_rect(x + 9, y + 7, 12, 2, accent);\n            graphics::fill_rect(x + 9, y + 13, 12, 2, foreground);\n            graphics::fill_rect(x + 9, y + 19, 9, 2, foreground);\n            break;\n        case AppIcon::Home:\n",
    "        case AppIcon::Document:\n            graphics::draw_rect(x + 4, y, 22, 27, foreground);\n            graphics::fill_rect(x + 9, y + 7, 12, 2, accent);\n            graphics::fill_rect(x + 9, y + 13, 12, 2, foreground);\n            graphics::fill_rect(x + 9, y + 19, 9, 2, foreground);\n            break;\n        case AppIcon::Notification:\n            graphics::draw_rect(x + 8, y + 5, 15, 17, foreground);\n            graphics::fill_rect(x + 11, y + 2, 9, 4, foreground);\n            graphics::fill_rect(x + 5, y + 20, 21, 3, accent);\n            graphics::fill_rect(x + 13, y + 25, 5, 2, foreground);\n            break;\n        case AppIcon::Home:\n",
)
replace_once(
    "kernel/ui/ui.cpp",
    "void app_tile(\n",
    "void notice_card(\n    const Rect& bounds, const char* title, const char* detail,\n    uint32_t priority) {\n    if (bounds.width <= 0 || bounds.height <= 0) return;\n    const graphics::Color background = graphics::rgb(14, 15, 18);\n    graphics::Color signal = kSteel;\n    if (priority >= 4U) signal = kRedBright;\n    else if (priority == 3U) signal = kRedMuted;\n    else if (priority == 2U) signal = graphics::rgb(176, 181, 191);\n    graphics::fill_rect(bounds.x + 4, bounds.y + 4, bounds.width, bounds.height, kSurfaceShadow);\n    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);\n    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, kTheme.border);\n    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height, signal);\n    graphics::fill_rect(bounds.x + 13, bounds.y + 13, 8, 8, signal);\n    graphics::draw_text(bounds.x + 29, bounds.y + 10,\n                        title ? title : \"NOTIFICATION\", kTheme.text, background, 1U, true);\n    graphics::draw_text(bounds.x + 29, bounds.y + 30,\n                        detail ? detail : \"\", kTheme.text_muted, background, 1U, true);\n    graphics::fill_rect(bounds.x + 13, bounds.y + bounds.height - 12,\n                        bounds.width > 32 ? bounds.width - 26 : 4, 2,\n                        priority >= 3U ? signal : graphics::rgb(44, 47, 53));\n}\n\nvoid app_tile(\n",
)

# ---------------------------------------------------------------------------
# Real Notification Center application: bounded 3-card public feed, paged.
# ---------------------------------------------------------------------------
notification_center = r'''#include "../common.h"
#include <kurogane/notification.h>

#define NOTICE_PAGE_SIZE 3U

typedef struct notice_page {
    ku_notification_response records[NOTICE_PAGE_SIZE];
    size_t count;
    uint64_t start_cursor;
    uint64_t next_cursor;
} notice_page;

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static ku_status_t transact(
    ku_service_connection_t connection,
    const ku_notification_request* request,
    ku_notification_response* response) {
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
        *response = *(const ku_notification_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return response->status;
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_result_t connect_notifications(void) {
    uint32_t attempts = 0U;
    while (attempts++ < 300U) {
        const ku_result_t result = ku_notification_connect();
        if (result > 0) return result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK) return result;
        (void)kuro_sleep(1U);
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_status_t list_after(
    ku_service_connection_t connection,
    uint64_t cursor,
    ku_notification_response* response) {
    ku_notification_request request;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_NOTIFICATION_LIST_PUBLIC;
    request.notification_id = cursor;
    return transact(connection, &request, response);
}

static ku_status_t load_page(
    ku_service_connection_t connection,
    uint64_t cursor,
    notice_page* page) {
    size_t index;
    if (page == NULL) return KU_STATUS_INVALID_ARGUMENT;
    clear_bytes(page, sizeof(*page));
    page->start_cursor = cursor;
    page->next_cursor = cursor;
    for (index = 0U; index < NOTICE_PAGE_SIZE; ++index) {
        ku_notification_response response;
        const ku_status_t status = list_after(connection, page->next_cursor, &response);
        if (status == KU_STATUS_NOT_FOUND) break;
        if (status != KU_STATUS_OK ||
            response.notification_id <= page->next_cursor ||
            response.state != KU_NOTIFICATION_STATE_ACTIVE ||
            (response.flags & KU_NOTIFICATION_FLAG_PUBLIC) == 0U) {
            return status == KU_STATUS_OK ? KU_STATUS_CORRUPT_DATA : status;
        }
        page->records[page->count++] = response;
        page->next_cursor = response.notification_id;
    }
    return KU_STATUS_OK;
}

static void notice_text(
    const ku_notification_response* record,
    char* output,
    size_t capacity) {
    char pid[24];
    output[0] = '\0';
    append_text(output, capacity, record->title);
    append_text(output, capacity, "\n");
    if (record->body[0] != '\0') append_text(output, capacity, record->body);
    else {
        append_text(output, capacity, "PID ");
        gui_u64(pid, sizeof(pid), record->owner_pid);
        append_text(output, capacity, pid);
    }
}

static void build_scene(kui_scene* scene, const notice_page* page) {
    kui_flow root;
    char summary[64] = "PUBLIC FEED / ";
    char number[24];
    size_t index;
    gui_u64(number, sizeof(number), page->count);
    append_text(summary, sizeof(summary), number);
    append_text(summary, sizeof(summary), page->count == 1U ? " NOTICE" : " NOTICES");

    kui_scene_initialize(scene);
    scene->visible_rows = 8U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FLUX NOTIFICATION CENTER");
    (void)kui_flow_label(&root, 2U, summary);
    for (index = 0U; index < page->count; ++index) {
        char text[64];
        notice_text(&page->records[index], text, sizeof(text));
        (void)kui_flow_notice(
            &root, 10U + (uint32_t)index, text, page->records[index].priority);
    }
    if (page->count == 0U) {
        (void)kui_flow_label(&root, 19U, "NO PUBLIC NOTIFICATIONS");
    }
    (void)kui_flow_button(&root, 20U, "REFRESH");
    (void)kui_flow_button(&root, 21U, "NEXT PAGE");
    if (page->count < NOTICE_PAGE_SIZE) {
        (void)kui_scene_set_flags(scene, 21U, KUI_VIEW_DISABLED);
    }
}

int main(void) {
    const ku_window_t window = gui_open("NOTIFICATIONS", 470, 100, 520, 455);
    const ku_result_t connected = connect_notifications();
    ku_service_connection_t connection;
    notice_page page;
    kui_scene scene;
    uint32_t pointer_buttons = 0U;
    if (window == KU_INVALID_WINDOW) return 1;
    if (connected <= 0) {
        (void)ku_ui_close(window);
        return 2;
    }
    connection = (ku_service_connection_t)connected;
    if (load_page(connection, 0U, &page) != KU_STATUS_OK) {
        (void)ku_service_close(connection);
        (void)ku_ui_close(window);
        return 3;
    }
    build_scene(&scene, &page);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_service_close(connection);
        (void)ku_ui_close(window);
        return 4;
    }
    puts("[TEST] flux_notification_center_connected: PASS");
    puts("[TEST] flux_notification_center_notice_card: PASS");
    if (page.count != 0U) puts("[TEST] flux_notification_center_public_record: PASS");

    for (;;) {
        ku_ui_event event;
        uint32_t target;
        const int result = gui_wait_event(window, &event);
        if (result < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_POINTER) continue;
        {
            const uint32_t previous = pointer_buttons;
            const int pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous & UINT32_C(1)) == 0U;
            pointer_buttons = event.buttons;
            if (!pressed) continue;
        }
        target = kui_scene_hit_test(&scene, event.x, event.y);
        if (target == 20U) {
            if (load_page(connection, 0U, &page) != KU_STATUS_OK) continue;
        } else if (target == 21U) {
            notice_page next;
            if (load_page(connection, page.next_cursor, &next) != KU_STATUS_OK) continue;
            if (next.count == 0U) {
                if (load_page(connection, 0U, &page) != KU_STATUS_OK) continue;
            } else {
                page = next;
            }
        } else {
            continue;
        }
        build_scene(&scene, &page);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_service_close(connection);
    (void)ku_ui_close(window);
    return 0;
}
'''
write("userspace/gui/notifications/main.c", notification_center)

# ---------------------------------------------------------------------------
# HOME: 8th, intentionally non-pinnable system app + one real public session
# notification held alive by the session-root process.
# ---------------------------------------------------------------------------
replace_once(
    "userspace/gui/launcher/main.c",
    "#include \"../../../common/version.h\"\n\n#define APP_COUNT 7U\n",
    "#include \"../../../common/version.h\"\n#include <kurogane/notification.h>\n\n#define APP_COUNT 8U\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "typedef struct launcher_app {\n    const char* label;\n    const char* subtitle;\n    const char* path;\n    uint32_t desktop_id;\n} launcher_app;\n\nstatic const launcher_app g_apps[APP_COUNT] = {\n    {\"TERMINAL\", \"shared shell / development\", \"/gui/terminal\", KU_DESKTOP_APP_TERMINAL},\n    {\"FILES\", \"persistent root / applications\", \"/gui/files\", KU_DESKTOP_APP_FILES},\n    {\"CONTROL CENTER\", \"system pulse / network / audio\", \"/gui/perf\", KU_DESKTOP_APP_PERFORMANCE},\n    {\"KUROGANE WEB\", \"native HTTP browser\", \"/gui/browser\", KU_DESKTOP_APP_BROWSER},\n    {\"MONITOR\", \"runtime / process health\", \"/gui/sysmon\", KU_DESKTOP_APP_MONITOR},\n    {\"SETTINGS\", \"appearance / sound\", \"/gui/settings\", KU_DESKTOP_APP_SETTINGS},\n    {\"ABOUT\", \"KuroganeOS platform\", \"/gui/about\", KU_DESKTOP_APP_ABOUT},\n};\n",
    "typedef struct launcher_app {\n    const char* label;\n    const char* subtitle;\n    const char* path;\n    uint32_t desktop_id;\n    uint32_t icon;\n    int pinnable;\n} launcher_app;\n\nstatic const launcher_app g_apps[APP_COUNT] = {\n    {\"TERMINAL\", \"shared shell / development\", \"/gui/terminal\", KU_DESKTOP_APP_TERMINAL, KU_UI_NATIVE_ICON_TERMINAL, 1},\n    {\"FILES\", \"persistent root / applications\", \"/gui/files\", KU_DESKTOP_APP_FILES, KU_UI_NATIVE_ICON_FILES, 1},\n    {\"CONTROL CENTER\", \"system pulse / network / audio\", \"/gui/perf\", KU_DESKTOP_APP_PERFORMANCE, KU_UI_NATIVE_ICON_PERFORMANCE, 1},\n    {\"KUROGANE WEB\", \"native HTTP browser\", \"/gui/browser\", KU_DESKTOP_APP_BROWSER, KU_UI_NATIVE_ICON_BROWSER, 1},\n    {\"MONITOR\", \"runtime / process health\", \"/gui/sysmon\", KU_DESKTOP_APP_MONITOR, KU_UI_NATIVE_ICON_MONITOR, 1},\n    {\"SETTINGS\", \"appearance / sound\", \"/gui/settings\", KU_DESKTOP_APP_SETTINGS, KU_UI_NATIVE_ICON_SETTINGS, 1},\n    {\"ABOUT\", \"KuroganeOS platform\", \"/gui/about\", KU_DESKTOP_APP_ABOUT, KU_UI_NATIVE_ICON_ABOUT, 1},\n    {\"NOTIFICATIONS\", \"public system activity\", \"/gui/notify\", NO_APP_ID, KU_UI_NATIVE_ICON_NOTIFICATION, 0},\n};\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "static char g_status[64] = \"FLUX DECK / READY\";\n",
    "static char g_status[64] = \"FLUX DECK / READY\";\nstatic ku_service_connection_t g_notification_connection = 0U;\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "static void reap_children(void) {\n",
    "static ku_status_t notification_transact(\n    ku_service_connection_t connection,\n    const ku_notification_request* request,\n    ku_notification_response* response) {\n    uint32_t attempts = 0U;\n    ku_status_t status = ku_service_send(connection, request, sizeof(*request));\n    if (status != KU_STATUS_OK) return status;\n    while (attempts++ < 300U) {\n        ku_service_message message;\n        status = ku_service_receive(connection, &message);\n        if (status == KU_STATUS_WOULD_BLOCK) {\n            (void)kuro_sleep(1U);\n            continue;\n        }\n        if (status != KU_STATUS_OK) return status;\n        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;\n        *response = *(const ku_notification_response*)(const void*)message.data;\n        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;\n        return response->status;\n    }\n    return KU_STATUS_TIMED_OUT;\n}\n\nstatic int publish_session_notification(void) {\n    uint32_t attempts = 0U;\n    ku_result_t connected = KU_STATUS_NOT_FOUND;\n    ku_notification_request request;\n    ku_notification_response response;\n    while (attempts++ < 300U) {\n        connected = ku_notification_connect();\n        if (connected > 0) break;\n        if (connected != KU_STATUS_NOT_FOUND && connected != KU_STATUS_WOULD_BLOCK) return 0;\n        (void)kuro_sleep(1U);\n    }\n    if (connected <= 0) return 0;\n    g_notification_connection = (ku_service_connection_t)connected;\n    memset(&request, 0, sizeof(request));\n    request.structure_size = sizeof(request);\n    request.operation = KU_NOTIFICATION_POST;\n    request.type = KU_NOTIFICATION_TYPE_SYSTEM;\n    request.priority = KU_NOTIFICATION_PRIORITY_NORMAL;\n    request.flags = KU_NOTIFICATION_FLAG_PUBLIC;\n    (void)strlcpy(request.title, \"Flux session ready\", sizeof(request.title));\n    (void)strlcpy(request.body, \"System Pulse, Files and Control Center are online\", sizeof(request.body));\n    if (notification_transact(g_notification_connection, &request, &response) != KU_STATUS_OK ||\n        response.notification_id == 0U ||\n        (response.flags & KU_NOTIFICATION_FLAG_PUBLIC) == 0U) {\n        (void)ku_service_close(g_notification_connection);\n        g_notification_connection = 0U;\n        return 0;\n    }\n    return 1;\n}\n\nstatic void reap_children(void) {\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "    if (app_is_running(app->desktop_id)) {\n",
    "    if (app_is_running((uint32_t)index)) {\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "    (void)remember_child((uint64_t)result, app->desktop_id);\n",
    "    (void)remember_child((uint64_t)result, (uint32_t)index);\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "    for (index = 0U; index < APP_COUNT; ++index) {\n        if (pin_state(g_apps[index].desktop_id)) {\n",
    "    for (index = 0U; index < APP_COUNT; ++index) {\n        if (!g_apps[index].pinnable) continue;\n        if (pin_state(g_apps[index].desktop_id)) {\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "    for (index = 0U; index < APP_COUNT; ++index) {\n        const uint32_t app_id = g_apps[index].desktop_id;\n        set_pin_state(app_id, (mask & (uint8_t)(UINT8_C(1) << app_id)) != 0U);\n    }\n",
    "    for (index = 0U; index < APP_COUNT; ++index) {\n        const uint32_t app_id = g_apps[index].desktop_id;\n        if (!g_apps[index].pinnable) continue;\n        set_pin_state(app_id, (mask & (uint8_t)(UINT8_C(1) << app_id)) != 0U);\n    }\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "    const launcher_app* app = &g_apps[g_selected];\n    memset(&request, 0, sizeof(request));\n",
    "    const launcher_app* app = &g_apps[g_selected];\n    if (!app->pinnable) {\n        (void)strlcpy(g_status, \"SYSTEM APP / NOT PINNABLE\", sizeof(g_status));\n        return;\n    }\n    memset(&request, 0, sizeof(request));\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "        (void)kui_flow_tile(\n            &apps, 10U + (uint32_t)index, label, g_apps[index].desktop_id);\n        if (pin_state(g_apps[index].desktop_id)) flags |= KUI_VIEW_PINNED;\n        if (app_is_running(g_apps[index].desktop_id)) flags |= KUI_VIEW_RUNNING;\n",
    "        (void)kui_flow_tile(\n            &apps, 10U + (uint32_t)index, label, g_apps[index].icon);\n        if (g_apps[index].pinnable && pin_state(g_apps[index].desktop_id)) flags |= KUI_VIEW_PINNED;\n        if (app_is_running((uint32_t)index)) flags |= KUI_VIEW_RUNNING;\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "    puts(\"[TEST] red_flux_apps_menu: PASS\");\n\n    build_scene(&scene);\n",
    "    puts(\"[TEST] red_flux_apps_menu: PASS\");\n    if (publish_session_notification()) {\n        puts(\"[TEST] flux_home_public_notification: PASS\");\n    } else {\n        puts(\"[TEST] flux_home_public_notification: FAIL\");\n    }\n\n    build_scene(&scene);\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "        } else if (event.character == 'a') {\n            select_and_launch(6U);\n        } else {\n",
    "        } else if (event.character == 'a') {\n            select_and_launch(6U);\n        } else if (event.character == 'n') {\n            select_and_launch(7U);\n        } else {\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "    (void)ku_ui_close(window);\n    return 0;\n}\n",
    "    if (g_notification_connection != 0U) {\n        (void)ku_service_close(g_notification_connection);\n        g_notification_connection = 0U;\n    }\n    (void)ku_ui_close(window);\n    return 0;\n}\n",
)

# Cross-platform SDK builders include the new FAT 8.3-compatible /gui/notify.
replace_once(
    "scripts/build-sdk.sh",
    "    about:about\n    settings:settings\n)\n",
    "    about:about\n    settings:settings\n    notifications:notify\n)\n",
)
replace_once(
    "scripts/build-sdk.ps1",
    "    @{ Name = 'about'; InstallName = 'about'; Source = 'userspace\\gui\\about\\main.c' },\n    @{ Name = 'settings'; InstallName = 'settings'; Source = 'userspace\\gui\\settings\\main.c' }\n)\n",
    "    @{ Name = 'about'; InstallName = 'about'; Source = 'userspace\\gui\\about\\main.c' },\n    @{ Name = 'settings'; InstallName = 'settings'; Source = 'userspace\\gui\\settings\\main.c' },\n    @{ Name = 'notifications'; InstallName = 'notify'; Source = 'userspace\\gui\\notifications\\main.c' }\n)\n",
)

# ABI + host scene contracts.
replace_once(
    "tests/test_sdk_abi.cpp",
    "#include <kurogane/ipc.h>\n",
    "#include <kurogane/ipc.h>\n#include <kurogane/notification.h>\n",
)
replace_once(
    "tests/test_sdk_abi.cpp",
    "    static_assert(KU_UI_NATIVE_METRIC == 9);\n    static_assert(KU_UI_NATIVE_ICON_FOLDER == 8);\n    static_assert(KU_UI_NATIVE_ICON_DOCUMENT == 9);\n    static_assert(KU_UI_NATIVE_ICON_COUNT == 10);\n",
    "    static_assert(KU_UI_NATIVE_METRIC == 9);\n    static_assert(KU_UI_NATIVE_NOTICE == 10);\n    static_assert(KU_UI_NATIVE_ICON_FOLDER == 8);\n    static_assert(KU_UI_NATIVE_ICON_DOCUMENT == 9);\n    static_assert(KU_UI_NATIVE_ICON_NOTIFICATION == 10);\n    static_assert(KU_UI_NATIVE_ICON_COUNT == 11);\n",
)
replace_once(
    "tests/test_sdk_abi.cpp",
    "    static_assert(KU_EVENT_BROKER_SERVICE_NAME_SIZE == 9U);\n",
    "    static_assert(KU_NOTIFICATION_LIST_PUBLIC == 4);\n    static_assert(KU_NOTIFICATION_FLAG_PUBLIC == (UINT32_C(1) << 0));\n    static_assert(sizeof(ku_notification_request) == 208U);\n    static_assert(sizeof(ku_notification_response) == 216U);\n\n    static_assert(KU_EVENT_BROKER_SERVICE_NAME_SIZE == 9U);\n",
)
replace_once(
    "tests/test_libui_pointer.c",
    "    puts(\"libui native packet + mouse hit-test tests passed\");\n",
    "    {\n        kui_scene notices;\n        kui_flow flow;\n        ku_ui_native_frame notice_frame;\n        kui_scene_initialize(&notices);\n        notices.visible_rows = 4U;\n        kui_flow_begin(&flow, &notices, 0U);\n        if (kui_flow_panel(&flow, 60U, \"NOTIFICATIONS\") != KU_STATUS_OK ||\n            kui_flow_notice(&flow, 61U, \"SYSTEM READY\\nServices online\", 2U) != KU_STATUS_OK ||\n            kui_flow_notice(&flow, 62U, \"SECURITY\\nReview requested\", 4U) != KU_STATUS_OK ||\n            kui_flow_button(&flow, 63U, \"REFRESH\") != KU_STATUS_OK) return 11;\n        if (kui_scene_build_native(&notices, &notice_frame) != KU_STATUS_OK ||\n            notice_frame.commands[1].type != KU_UI_NATIVE_NOTICE ||\n            notice_frame.commands[1].value != 2U || notice_frame.commands[1].maximum != 4U ||\n            notice_frame.commands[2].type != KU_UI_NATIVE_NOTICE ||\n            !expect(kui_scene_hit_test(\n                &notices, 24, center_y(&notice_frame, 1U)), 0U, \"notice inert\") ||\n            !expect(kui_scene_hit_test(\n                &notices, 24, center_y(&notice_frame, 3U)), 63U, \"notice refresh hit\")) return 12;\n    }\n\n    puts(\"libui native packet + mouse hit-test tests passed\");\n",
)
replace_once(
    "tests/test_mouse_first_apps.py",
    "assert \"flux_home_system_pulse: PASS\" in launcher\n",
    "assert \"flux_home_system_pulse: PASS\" in launcher\nassert \"flux_home_public_notification: PASS\" in launcher\nassert \"/gui/notify\" in launcher and \"KU_UI_NATIVE_ICON_NOTIFICATION\" in launcher\nassert \"SYSTEM APP / NOT PINNABLE\" in launcher\nnotifications = read(\"userspace/gui/notifications/main.c\")\nassert \"KU_NOTIFICATION_LIST_PUBLIC\" in notifications\nassert \"kui_flow_notice\" in notifications\nassert \"flux_notification_center_connected: PASS\" in notifications\nassert \"flux_notification_center_public_record: PASS\" in notifications\n",
)

# Developer reference: public feed is opt-in and read-only to observers.
doc = read("docs/DEVELOPERS/API_REFERENCE.md")
marker = "## Notification public feed (notifications.v1)"
if marker not in doc:
    doc += """

## Notification public feed (notifications.v1)

`notifications.v1` keeps `GET` and `DISMISS` owner-scoped. A producer may opt a
record into the desktop feed with `KU_NOTIFICATION_FLAG_PUBLIC`. Observers use
`KU_NOTIFICATION_LIST_PUBLIC` with `notification_id` as an exclusive cursor;
the service returns the next active public record or `KU_STATUS_NOT_FOUND` at
the end. Records without the public flag are never returned by this operation.
The public-feed flag does not grant observers dismiss rights.
"""
    write("docs/DEVELOPERS/API_REFERENCE.md", doc)

# Migration guards.
for path, needles in {
    "sdk/include/kurogane/notification.h": ["KU_NOTIFICATION_LIST_PUBLIC = 4", "KU_NOTIFICATION_FLAG_PUBLIC"],
    "userspace/system/notificationd/main.c": ["find_public_after", "KU_NOTIFICATION_LIST_PUBLIC", "KU_NOTIFICATION_FLAG_PUBLIC"],
    "sdk/include/kurogane/ui.h": ["KU_UI_NATIVE_NOTICE = 10", "KU_UI_NATIVE_ICON_NOTIFICATION = 10", "KU_UI_NATIVE_ICON_COUNT = 11"],
    "userspace/gui/launcher/main.c": ["APP_COUNT 8U", "/gui/notify", "flux_home_public_notification: PASS", "SYSTEM APP / NOT PINNABLE"],
    "userspace/gui/notifications/main.c": ["kui_flow_notice", "KU_NOTIFICATION_LIST_PUBLIC", "flux_notification_center_public_record: PASS"],
    "scripts/build-sdk.sh": ["notifications:notify"],
    "scripts/build-sdk.ps1": ["InstallName = 'notify'"],
}.items():
    text = read(path)
    for needle in needles:
        if needle not in text:
            raise SystemExit(f"{path}: missing migration guard {needle}")

print("Flux Notification Center migration applied")
