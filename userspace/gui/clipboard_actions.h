#ifndef KUROGANE_GUI_CLIPBOARD_ACTIONS_H
#define KUROGANE_GUI_CLIPBOARD_ACTIONS_H

#include "common.h"
#include <kurogane/clipboard.h>

/* Synchronous, bounded wrapper around the existing clipboard.v1 service.
 * The destination is updated only after validating the full UTF-8 response.
 * Never retry a non-idempotent request after an ambiguous timeout.
 */
static inline int flux_clipboard_transact(
    ku_service_connection_t conn,
    const ku_clipboard_request* request,
    ku_clipboard_response* response) {
    uint32_t attempt;
    ku_status_t status = ku_service_send(conn, request, sizeof(*request));
    if (status != KU_STATUS_OK) return 0;
    for (attempt = 0U; attempt < 300U; ++attempt) {
        ku_service_message message;
        status = ku_service_receive(conn, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)kuro_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK || message.data_size != sizeof(*response)) return 0;
        memcpy(response, message.data, sizeof(*response));
        return response->structure_size == sizeof(*response) &&
            response->data_size <= KU_CLIPBOARD_DATA_CAPACITY &&
            response->status == KU_STATUS_OK;
    }
    return 0;
}

static inline ku_result_t flux_clipboard_open(void) {
    uint32_t attempt;
    for (attempt = 0U; attempt < 40U; ++attempt) {
        const ku_result_t result = ku_clipboard_connect();
        if (result > 0) return result;
        if (result != KU_STATUS_WOULD_BLOCK &&
            result != KU_STATUS_NOT_FOUND) return result;
        (void)kuro_sleep(1U);
    }
    return KU_STATUS_NOT_FOUND;
}

static inline int flux_clipboard_set_text(const char* text) {
    ku_clipboard_request request;
    ku_clipboard_response response;
    ku_result_t opened;
    size_t length;
    int ok;
    if (text == NULL) return 0;
    length = strlen(text) + 1U;
    if (length < 2U || length > KU_CLIPBOARD_DATA_CAPACITY) return 0;
    opened = flux_clipboard_open();
    if (opened <= 0) return 0;
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    request.structure_size = sizeof(request);
    request.operation = KU_CLIPBOARD_SET;
    request.format = KU_CLIPBOARD_FORMAT_UTF8;
    request.data_size = (uint32_t)length;
    memcpy(request.data, text, length);
    ok = flux_clipboard_transact(
        (ku_service_connection_t)opened, &request, &response) &&
        response.format == KU_CLIPBOARD_FORMAT_UTF8 &&
        response.data_size == length;
    (void)ku_service_close((ku_service_connection_t)opened);
    return ok;
}

static inline int flux_clipboard_get_text(char* destination, size_t capacity) {
    ku_clipboard_request request;
    ku_clipboard_response response;
    ku_result_t opened;
    int ok;
    if (destination == NULL || capacity == 0U) return 0;
    opened = flux_clipboard_open();
    if (opened <= 0) return 0;
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    request.structure_size = sizeof(request);
    request.operation = KU_CLIPBOARD_GET;
    ok = flux_clipboard_transact(
        (ku_service_connection_t)opened, &request, &response) &&
        response.format == KU_CLIPBOARD_FORMAT_UTF8 &&
        response.data_size != 0U && response.data_size <= capacity &&
        response.data[response.data_size - 1U] == 0U;
    if (ok) {
        memcpy(destination, response.data, response.data_size);
    }
    (void)ku_service_close((ku_service_connection_t)opened);
    return ok;
}

#endif
