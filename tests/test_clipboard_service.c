#include "../userspace/runtime/user.h"
#include <kurogane/clipboard.h>

static ku_service_message inbox[2];
static int queued[2], receive_calls[2], sends[2], closes[2];
static ku_status_t send_status[2];
static ku_clipboard_response delivered[2];
static ku_status_t fake_send(ku_service_connection_t connection, const void* data, size_t size) {
    const size_t index = (size_t)connection - 1U;
    if (size != sizeof(ku_clipboard_response)) return KU_STATUS_INVALID_ARGUMENT;
    ++sends[index];
    if (send_status[index] == KU_STATUS_OK)
        delivered[index] = *(const ku_clipboard_response*)data;
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
#define _start clipboard_daemon_entry
#include "../userspace/system/clipboardd/main.c"
#undef _start
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static void enqueue(size_t index, uint32_t operation, const uint8_t* data, size_t size) {
    ku_clipboard_request request;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = operation;
    request.format = size ? KU_CLIPBOARD_FORMAT_UTF8 : KU_CLIPBOARD_FORMAT_NONE;
    request.data_size = (uint32_t)size;
    if (size) copy_bytes(request.data, data, size);
    clear_bytes(&inbox[index], sizeof(inbox[index]));
    inbox[index].sender_pid = index + 10U;
    inbox[index].data_size = sizeof(request);
    copy_bytes(inbox[index].data, &request, sizeof(request));
    queued[index] = 1;
}

int main(void) {
    const uint8_t first[] = {'A', 0}, second[] = {'B', 0};
    const uint8_t polish[] = {0xC5, 0xBC, 0xC3, 0xB3, 0xC5, 0x82, 0xC4, 0x87, 0};
    const uint8_t emoji[] = {0xF0, 0x9F, 0x98, 0x80, 0};
    const uint8_t invalid[][5] = {
        {0xC0, 0x80, 0}, {0xED, 0xA0, 0x80, 0}, {0xF4, 0x90, 0x80, 0x80, 0},
        {0x80, 0}, {0xE2, 0x82, 0}, {'A', 0, 'B', 0}, {0xE0, 0x80, 0x80, 0}
    };
    const size_t lengths[] = {3, 4, 5, 2, 3, 4, 4};
    size_t i;
    CHECK(payload_valid(KU_CLIPBOARD_FORMAT_UTF8, sizeof(polish), polish));
    CHECK(payload_valid(KU_CLIPBOARD_FORMAT_UTF8, sizeof(emoji), emoji));
    CHECK(payload_valid(KU_CLIPBOARD_FORMAT_UTF8, 1, (const uint8_t*)""));
    CHECK(!payload_valid(KU_CLIPBOARD_FORMAT_UTF8, 1, first));
    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
        CHECK(!payload_valid(KU_CLIPBOARD_FORMAT_UTF8, (uint32_t)lengths[i], invalid[i]));
    CHECK(payload_valid(KU_CLIPBOARD_FORMAT_BINARY, 5, invalid[2]));
    for (i = 0; i < 2; ++i) { clients[i].active = 1; clients[i].connection = i + 1; }
    send_status[0] = KU_STATUS_WOULD_BLOCK;
    enqueue(0, KU_CLIPBOARD_SET, first, sizeof(first));
    service_clients();
    CHECK(clipboard.generation == 1 && clients[0].response_pending && receive_calls[0] == 1);
    enqueue(0, KU_CLIPBOARD_CLEAR, 0, 0);
    enqueue(1, KU_CLIPBOARD_SET, second, sizeof(second));
    service_clients();
    CHECK(clipboard.generation == 2 && clipboard.data[0] == 'B');
    CHECK(receive_calls[0] == 1 && queued[0] && delivered[1].generation == 2);
    CHECK(clients[0].response.generation == 1 && clients[0].response.data[0] == 'A');
    send_status[0] = KU_STATUS_OK;
    service_clients();
    CHECK(!clients[0].response_pending && delivered[0].generation == 1);
    CHECK(delivered[0].data[0] == 'A' && clipboard.generation == 2 && queued[0]);
    service_clients();
    CHECK(clipboard.generation == 3 && clipboard.format == KU_CLIPBOARD_FORMAT_NONE);
    CHECK(delivered[0].generation == 3 && delivered[0].data[0] == 0);
    enqueue(0, KU_CLIPBOARD_SET, invalid[0], lengths[0]);
    service_clients();
    CHECK(clipboard.generation == 3 && delivered[0].status == KU_STATUS_INVALID_ARGUMENT);
    send_status[0] = KU_STATUS_WOULD_BLOCK;
    enqueue(0, KU_CLIPBOARD_GET, 0, 0);
    service_clients();
    CHECK(clients[0].response_pending);
    send_status[0] = KU_STATUS_NOT_FOUND;
    service_clients();
    CHECK(!clients[0].active && !clients[0].response_pending && closes[0] == 1);
    CHECK(clients[0].response.generation == 0 && clients[0].response.data[0] == 0);
    /* Maximum binary payload and malformed request envelopes remain bounded. */
    {
        ku_clipboard_request request;
        clear_bytes(&request, sizeof(request));
        request.structure_size = sizeof(request);
        request.operation = KU_CLIPBOARD_SET;
        request.format = KU_CLIPBOARD_FORMAT_BINARY;
        request.data_size = KU_CLIPBOARD_DATA_CAPACITY;
        for (i = 0; i < sizeof(request.data); ++i) request.data[i] = (uint8_t)i;
        enqueue(1, KU_CLIPBOARD_GET, 0, 0);
        copy_bytes(inbox[1].data, &request, sizeof(request));
        service_clients();
        CHECK(delivered[1].status == KU_STATUS_OK && clipboard.generation == 4);
        CHECK(delivered[1].data[191] == 191 && delivered[1].data_size == 192);
        request.data_size = KU_CLIPBOARD_DATA_CAPACITY + 1U;
        queued[1] = 1;
        copy_bytes(inbox[1].data, &request, sizeof(request));
        service_clients();
        CHECK(delivered[1].status == KU_STATUS_INVALID_ARGUMENT && clipboard.generation == 4);
        queued[1] = 1; inbox[1].data_size = sizeof(request) - 1;
        service_clients();
        CHECK(delivered[1].status == KU_STATUS_CORRUPT_DATA && clipboard.generation == 4);
    }
    CHECK(next_generation(UINT64_MAX) == 1);
    return 0;
}
