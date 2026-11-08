#include <assert.h>
#include "../userspace/runtime/user.h"
#include <kurogane/settings.h>
#include <kurogane/filesystem.h>
#include <kurogane/event_broker.h>

static ku_service_message inbox[2];
static int queued[2], receive_calls[2], sends[2], closes[2];
static ku_status_t send_status[2];
static ku_settings_response delivered[2];
static ku_status_t fake_send(ku_service_connection_t connection, const void* data, size_t size) {
    const size_t index = (size_t)connection - 1U;
    if (size != sizeof(ku_settings_response)) return KU_STATUS_INVALID_ARGUMENT;
    ++sends[index];
    if (send_status[index] == KU_STATUS_OK)
        delivered[index] = *(const ku_settings_response*)data;
    return send_status[index];
}
static ku_status_t fake_receive(ku_service_connection_t connection, ku_service_message* message) {
    const size_t index = (size_t)connection - 1U;
    ++receive_calls[index];
    if (!queued[index]) return KU_STATUS_WOULD_BLOCK;
    *message = inbox[index]; queued[index] = 0;
    return KU_STATUS_OK;
}
static ku_status_t fake_close(ku_service_connection_t connection) {
    ++closes[(size_t)connection - 1U]; return KU_STATUS_OK;
}
#define ku_service_send fake_send
#define ku_service_receive fake_receive
#define ku_service_close fake_close
#define _start settings_daemon_entry
#include "../userspace/system/settingsd/main.c"
#undef _start
#define CHECK(condition) assert(condition)

static void enqueue(size_t index, uint64_t pid) {
    ku_settings_request request;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_GET;
    key_copy(request.key, "theme");
    clear_bytes(&inbox[index], sizeof(inbox[index]));
    inbox[index].sender_pid = pid;
    inbox[index].data_size = sizeof(request);
    copy_bytes(inbox[index].data, &request, sizeof(request));
    queued[index] = 1;
}

int main(void) {
    records[0].active = 1;
    records[0].type = KU_SETTINGS_TYPE_BOOL;
    records[0].value_size = 1;
    records[0].value[0] = 1;
    key_copy(records[0].key, "theme");
    for (size_t i = 0; i < 2; ++i) {
        clients[i].active = 1; clients[i].connection = i + 1;
    }
    send_status[0] = KU_STATUS_WOULD_BLOCK;
    enqueue(0, 10);
    service_clients();
    CHECK(sends[0] == 1 && receive_calls[0] == 1);
    records[0].value[0] = 0;
    enqueue(0, 10);
    enqueue(1, 11);
    service_clients();
    CHECK(receive_calls[0] == 1 && queued[0]);
    CHECK(delivered[1].status == KU_STATUS_OK && delivered[1].value[0] == 0);
    send_status[0] = KU_STATUS_OK;
    service_clients();
    CHECK(delivered[0].value[0] == 1 && queued[0]);
    service_clients();
    CHECK(delivered[0].value[0] == 0 && !queued[0]);
    /* Authorization errors must survive backpressure too. */
    send_status[0] = KU_STATUS_WOULD_BLOCK;
    enqueue(0, 99);
    service_clients();
    send_status[0] = KU_STATUS_OK;
    service_clients();
    CHECK(delivered[0].status == KU_STATUS_ACCESS_DENIED);
    send_status[0] = KU_STATUS_IO_ERROR;
    enqueue(0, 10);
    service_clients();
    CHECK(!clients[0].active && closes[0] == 1);
    return 0;
}
