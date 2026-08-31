#include "../common.h"
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
