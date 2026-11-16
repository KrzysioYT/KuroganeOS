#include "../common.h"
#include <kurogane/flux_app_catalog.h>
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

static void build_scene(ku_window_t window, kui_scene* scene, const notice_page* page,
                        const char* error) {
    kui_flow root;
    char summary[64] = "PUBLIC FEED / ";
    char number[24];
    size_t index;
    gui_u64(number, sizeof(number), page->count);
    append_text(summary, sizeof(summary), number);
    append_text(summary, sizeof(summary), page->count == 1U ? " NOTICE" : " NOTICES");

    kui_scene_initialize(scene);
    scene->visible_rows = 8U;
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FLUX NOTIFICATION CENTER");
    (void)kui_flow_label(&root, 2U, error != NULL ? error : summary);
    (void)kui_flow_button(&root, 20U, "REFRESH");
    (void)kui_flow_button(&root, 21U, "NEXT PAGE");
    for (index = 0U; index < page->count; ++index) {
        char text[64];
        notice_text(&page->records[index], text, sizeof(text));
        (void)kui_flow_notice(
            &root, 10U + (uint32_t)index, text, page->records[index].priority);
    }
    if (page->count == 0U) {
        (void)kui_flow_label(&root, 19U, "NO PUBLIC NOTIFICATIONS");
    }
    if (page->count < NOTICE_PAGE_SIZE || error != NULL) {
        (void)kui_scene_set_flags(scene, 21U, KUI_VIEW_DISABLED);
    }
    (void)gui_update_scene_viewport(window, scene);
}

int main(void) {
    const ku_window_t window = gui_open(KU_FLUX_TITLE_NOTIFICATIONS, 470, 100, 520, 455);
    ku_service_connection_t connection = 0U;
    notice_page page = {0};
    kui_scene scene;
    uint32_t pointer_buttons = 0U;
    if (window == KU_INVALID_WINDOW) return 1;
    const ku_result_t connected = connect_notifications();
    if (connected > 0) connection = (ku_service_connection_t)connected;
    notice_page first;
    if (connection != 0U && load_page(connection, 0U, &first) == KU_STATUS_OK) {
        page = first;
    } else if (connection != 0U) {
        (void)ku_service_close(connection);
        connection = 0U;
    }
    build_scene(window, &scene, &page, connection != 0U ? NULL : "FEED OFFLINE / USE REFRESH");
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        if (connection != 0U) (void)ku_service_close(connection);
        (void)ku_ui_close(window);
        return 4;
    }
    if (connection != 0U) puts("[TEST] flux_notification_center_connected: PASS");
    puts("[TEST] flux_notification_center_notice_card: PASS");
    if (page.count != 0U) puts("[TEST] flux_notification_center_public_record: PASS");

    for (;;) {
        ku_ui_event event;
        uint32_t target;
        if (gui_update_scene_viewport(window, &scene)) (void)kui_scene_present(window, &scene);
        const int result = kui_next_event(window, &event);
        if (result == 0) {
            (void)kuro_sleep(1U);
            continue;
        }
        if (result < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type == KU_UI_EVENT_SCROLL) {
            const int32_t step = event.wheel_y == INT32_MIN ? INT32_MAX : -event.wheel_y;
            (void)kui_scene_scroll(&scene, step);
            (void)kui_scene_present(window, &scene);
            continue;
        }
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
        if (target != 20U && target != 21U) continue;
        if (connection == 0U) {
            const ku_result_t reopened = connect_notifications();
            if (reopened > 0) connection = (ku_service_connection_t)reopened;
        }
        notice_page next;
        ku_status_t status = connection != 0U
            ? load_page(connection, target == 20U ? 0U : page.next_cursor, &next)
            : KU_STATUS_NOT_FOUND;
        if (status == KU_STATUS_OK && target == 21U && next.count == 0U)
            status = load_page(connection, 0U, &next);
        if (status == KU_STATUS_OK) {
            page = next;
        } else {
            if (connection != 0U) (void)ku_service_close(connection);
            connection = 0U;
        }
        build_scene(window, &scene, &page, status == KU_STATUS_OK ? NULL : "FEED UPDATE FAILED / RETRY");
        if (kui_scene_present(window, &scene) != KU_STATUS_OK) break;
        if (status == KU_STATUS_OK && target == 20U)
            printf("[NOTIFY] refreshed=%u\n", (unsigned)page.count);
    }

    if (connection != 0U) (void)ku_service_close(connection);
    (void)ku_ui_close(window);
    return 0;
}
