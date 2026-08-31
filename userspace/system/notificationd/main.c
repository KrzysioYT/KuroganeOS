#include "../../runtime/user.h"

#include <kurogane/notification.h>

#define NOTIFICATIOND_MAX_CLIENTS 8U
#define NOTIFICATIOND_MAX_RECORDS 32U

typedef struct notificationd_client {
    ku_service_connection_t connection;
    uint64_t pid;
    int active;
} notificationd_client;

typedef struct notificationd_record {
    uint64_t id;
    uint64_t owner_pid;
    uint32_t type;
    uint32_t priority;
    uint32_t flags;
    char title[KU_NOTIFICATION_TITLE_CAPACITY];
    char body[KU_NOTIFICATION_BODY_CAPACITY];
    int active;
} notificationd_record;

static notificationd_client clients[NOTIFICATIOND_MAX_CLIENTS];
static notificationd_record records[NOTIFICATIOND_MAX_RECORDS];
static uint64_t next_notification_id = UINT64_C(1);

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index = 0U;
    for (; index < size; ++index) bytes[index] = 0U;
}

static int bounded_text_valid(const char* text, size_t capacity, int allow_empty) {
    size_t index = 0U;
    if (text == (const char*)0 || capacity == 0U) return 0;
    if (!allow_empty && text[0] == '\0') return 0;
    for (; index < capacity; ++index) {
        const unsigned char value = (unsigned char)text[index];
        if (value == 0U) return 1;
        if (value < 0x20U && value != '\t') return 0;
    }
    return 0;
}

static void copy_text(char* destination, size_t capacity, const char* source) {
    size_t index = 0U;
    if (destination == (char*)0 || capacity == 0U) return;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index++] = '\0';
    while (index < capacity) destination[index++] = '\0';
}

static int type_valid(uint32_t type) {
    return type == KU_NOTIFICATION_TYPE_APPLICATION ||
        type == KU_NOTIFICATION_TYPE_SYSTEM ||
        type == KU_NOTIFICATION_TYPE_SECURITY;
}

static int priority_valid(uint32_t priority) {
    return priority >= KU_NOTIFICATION_PRIORITY_LOW &&
        priority <= KU_NOTIFICATION_PRIORITY_CRITICAL;
}

static notificationd_record* reserve_record(void) {
    size_t index = 0U;
    for (; index < NOTIFICATIOND_MAX_RECORDS; ++index) {
        if (!records[index].active) return &records[index];
    }
    return (notificationd_record*)0;
}

static notificationd_record* find_public_after(uint64_t cursor) {
    notificationd_record* best = (notificationd_record*)0;
    size_t index = 0U;
    for (; index < NOTIFICATIOND_MAX_RECORDS; ++index) {
        notificationd_record* record = &records[index];
        if (!record->active ||
            (record->flags & KU_NOTIFICATION_FLAG_PUBLIC) == 0U ||
            record->id <= cursor) continue;
        if (best == (notificationd_record*)0 || record->id < best->id) best = record;
    }
    return best;
}

static notificationd_record* find_record(uint64_t owner_pid, uint64_t id) {
    size_t index = 0U;
    for (; index < NOTIFICATIOND_MAX_RECORDS; ++index) {
        notificationd_record* record = &records[index];
        if (record->active && record->id == id && record->owner_pid == owner_pid)
            return record;
    }
    return (notificationd_record*)0;
}

static void clear_record(notificationd_record* record) {
    if (record == (notificationd_record*)0) return;
    clear_bytes(record, sizeof(*record));
}

static void cleanup_owner(uint64_t owner_pid) {
    size_t index = 0U;
    if (owner_pid == 0U) return;
    for (; index < NOTIFICATIOND_MAX_RECORDS; ++index) {
        if (records[index].active && records[index].owner_pid == owner_pid)
            clear_record(&records[index]);
    }
}

static uint64_t allocate_id(void) {
    uint64_t id = next_notification_id++;
    if (id == 0U) {
        id = next_notification_id++;
    }
    return id;
}

static void fill_response_record(
    ku_notification_response* response,
    const notificationd_record* record,
    uint32_t state) {
    if (response == (ku_notification_response*)0 ||
        record == (const notificationd_record*)0) return;
    response->notification_id = record->id;
    response->owner_pid = record->owner_pid;
    response->type = record->type;
    response->priority = record->priority;
    response->state = state;
    response->flags = record->flags;
    copy_text(response->title, sizeof(response->title), record->title);
    copy_text(response->body, sizeof(response->body), record->body);
}

static ku_status_t send_response(
    ku_service_connection_t connection,
    ku_status_t status,
    const notificationd_record* record,
    uint32_t state) {
    ku_notification_response response;
    clear_bytes(&response, sizeof(response));
    response.structure_size = sizeof(response);
    response.status = status;
    if (record != (const notificationd_record*)0)
        fill_response_record(&response, record, state);
    return ku_service_send(connection, &response, sizeof(response));
}

static ku_status_t post_notification(
    uint64_t owner_pid,
    const ku_notification_request* request,
    notificationd_record** created) {
    notificationd_record* record;
    if (created == (notificationd_record**)0) return KU_STATUS_INVALID_ARGUMENT;
    *created = (notificationd_record*)0;
    if (request->notification_id != 0U || request->reserved != 0U ||
        (request->flags & ~KU_NOTIFICATION_FLAG_PUBLIC) != 0U || !type_valid(request->type) ||
        !priority_valid(request->priority) ||
        !bounded_text_valid(request->title, sizeof(request->title), 0) ||
        !bounded_text_valid(request->body, sizeof(request->body), 1))
        return KU_STATUS_INVALID_ARGUMENT;

    record = reserve_record();
    if (record == (notificationd_record*)0) return KU_STATUS_OUT_OF_MEMORY;
    clear_record(record);
    record->id = allocate_id();
    record->owner_pid = owner_pid;
    record->type = request->type;
    record->priority = request->priority;
    record->flags = request->flags;
    copy_text(record->title, sizeof(record->title), request->title);
    copy_text(record->body, sizeof(record->body), request->body);
    record->active = 1;
    *created = record;
    return KU_STATUS_OK;
}

static void handle_request(
    notificationd_client* client,
    const ku_service_message* message) {
    const ku_notification_request* request;
    notificationd_record* record = (notificationd_record*)0;
    ku_status_t status;

    if (client == (notificationd_client*)0 ||
        message == (const ku_service_message*)0) return;
    if (message->data_size != sizeof(ku_notification_request)) {
        (void)send_response(
            client->connection, KU_STATUS_CORRUPT_DATA,
            (const notificationd_record*)0, 0U);
        return;
    }
    request = (const ku_notification_request*)(const void*)message->data;
    if (request->structure_size != sizeof(*request)) {
        (void)send_response(
            client->connection, KU_STATUS_INVALID_ARGUMENT,
            (const notificationd_record*)0, 0U);
        return;
    }
    if (client->pid == 0U) client->pid = message->sender_pid;
    else if (client->pid != message->sender_pid) {
        (void)send_response(
            client->connection, KU_STATUS_ACCESS_DENIED,
            (const notificationd_record*)0, 0U);
        return;
    }

    switch (request->operation) {
        case KU_NOTIFICATION_POST:
            status = post_notification(message->sender_pid, request, &record);
            (void)send_response(
                client->connection, status, record,
                status == KU_STATUS_OK ? KU_NOTIFICATION_STATE_ACTIVE : 0U);
            break;
        case KU_NOTIFICATION_LIST_PUBLIC:
            if (request->reserved != 0U || request->flags != 0U ||
                request->type != 0U || request->priority != 0U ||
                request->title[0] != '\0' || request->body[0] != '\0') {
                (void)send_response(
                    client->connection, KU_STATUS_INVALID_ARGUMENT,
                    (const notificationd_record*)0, 0U);
                break;
            }
            record = find_public_after(request->notification_id);
            (void)send_response(
                client->connection,
                record != (notificationd_record*)0 ? KU_STATUS_OK : KU_STATUS_NOT_FOUND,
                record,
                record != (notificationd_record*)0 ? KU_NOTIFICATION_STATE_ACTIVE : 0U);
            break;
        case KU_NOTIFICATION_GET:
            if (request->notification_id == 0U) {
                (void)send_response(
                    client->connection, KU_STATUS_INVALID_ARGUMENT,
                    (const notificationd_record*)0, 0U);
                break;
            }
            record = find_record(message->sender_pid, request->notification_id);
            (void)send_response(
                client->connection,
                record != (notificationd_record*)0 ? KU_STATUS_OK : KU_STATUS_NOT_FOUND,
                record,
                record != (notificationd_record*)0 ? KU_NOTIFICATION_STATE_ACTIVE : 0U);
            break;
        case KU_NOTIFICATION_DISMISS:
            if (request->notification_id == 0U) {
                (void)send_response(
                    client->connection, KU_STATUS_INVALID_ARGUMENT,
                    (const notificationd_record*)0, 0U);
                break;
            }
            record = find_record(message->sender_pid, request->notification_id);
            if (record == (notificationd_record*)0) {
                (void)send_response(
                    client->connection, KU_STATUS_NOT_FOUND,
                    (const notificationd_record*)0, 0U);
                break;
            }
            (void)send_response(
                client->connection, KU_STATUS_OK, record,
                KU_NOTIFICATION_STATE_DISMISSED);
            clear_record(record);
            break;
        default:
            (void)send_response(
                client->connection, KU_STATUS_NOT_SUPPORTED,
                (const notificationd_record*)0, 0U);
            break;
    }
}

static void accept_clients(ku_service_endpoint_t endpoint) {
    for (;;) {
        const ku_result_t accepted = ku_service_accept(endpoint);
        size_t index = 0U;
        if (accepted == KU_STATUS_WOULD_BLOCK) return;
        if (accepted <= 0) return;
        for (; index < NOTIFICATIOND_MAX_CLIENTS; ++index) {
            if (!clients[index].active) {
                clients[index].connection = (ku_service_connection_t)accepted;
                clients[index].pid = 0U;
                clients[index].active = 1;
                break;
            }
        }
        if (index == NOTIFICATIOND_MAX_CLIENTS) {
            (void)ku_service_close((ku_service_connection_t)accepted);
            return;
        }
    }
}

static void service_clients(void) {
    size_t index = 0U;
    for (; index < NOTIFICATIOND_MAX_CLIENTS; ++index) {
        notificationd_client* client = &clients[index];
        ku_service_message message;
        ku_status_t status;
        if (!client->active) continue;
        status = ku_service_receive(client->connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) continue;
        if (status != KU_STATUS_OK) {
            cleanup_owner(client->pid);
            (void)ku_service_close(client->connection);
            clear_bytes(client, sizeof(*client));
            continue;
        }
        handle_request(client, &message);
    }
}

__attribute__((noreturn)) void _start(void) {
    const ku_result_t endpoint = ku_service_register(
        KU_NOTIFICATION_SERVICE_NAME,
        KU_NOTIFICATION_SERVICE_NAME_SIZE);
    if (endpoint <= 0) {
        (void)u_puts("notificationd: service registration failed\n");
        (void)u_puts("[TEST] notification_service: FAIL\n");
        ku_exit(1);
    }

    (void)u_puts("notificationd: notifications.v1 online\n");
    (void)u_puts("[TEST] notification_service: PASS\n");
    for (;;) {
        accept_clients((ku_service_endpoint_t)endpoint);
        service_clients();
        (void)ku_sleep(1U);
    }
}
