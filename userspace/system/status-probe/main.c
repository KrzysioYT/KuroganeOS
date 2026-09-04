#include <kurogane/event.h>
#include <kurogane/filesystem.h>
#include <kurogane/ipc.h>
#include <kurogane/network.h>
#include <kurogane/shared_memory.h>

#include "../../runtime/user.h"

#define STATUS_ENDPOINT "qualification.status"
#define STATUS_FILE "/stale.tmp"

__attribute__((noreturn)) static void fail(uint64_t code) {
    (void)u_puts("[TEST] unified_status_runtime: FAIL code=");
    (void)u_put_u64(code);
    (void)u_puts("\n");
    ku_exit((int32_t)code);
}

__attribute__((noreturn)) void _start(void) {
    ku_result_t created = ku_ipc_bind(
        STATUS_ENDPOINT, sizeof(STATUS_ENDPOINT) - 1U);
    if (created <= 0) fail(10U);
    const ku_ipc_handle_t endpoint = (ku_ipc_handle_t)created;
    if (ku_ipc_close(endpoint) != KU_STATUS_OK ||
        ku_ipc_close(endpoint) != KU_STATUS_STALE_HANDLE) {
        fail(11U);
    }
    (void)u_puts("[TEST] status_ipc_stale_handle: PASS\n");

    created = ku_shm_create(KU_SHM_PAGE_SIZE);
    if (created <= 0) fail(20U);
    const ku_shm_handle_t shared = (ku_shm_handle_t)created;
    if (ku_shm_close(shared) != KU_STATUS_OK ||
        ku_shm_close(shared) != KU_STATUS_STALE_HANDLE) {
        fail(21U);
    }
    (void)u_puts("[TEST] status_shm_stale_handle: PASS\n");

    created = ku_event_create(KU_EVENT_AUTO_RESET, 0);
    if (created <= 0) fail(30U);
    const ku_event_handle_t event = (ku_event_handle_t)created;
    if (ku_event_close(event) != KU_STATUS_OK ||
        ku_event_poll(event) != KU_STATUS_STALE_HANDLE) {
        fail(31U);
    }
    (void)u_puts("[TEST] status_event_stale_handle: PASS\n");

    created = ku_socket_create(KU_SOCKET_DATAGRAM, KU_SOCKET_PROTOCOL_UDP);
    if (created <= 0) fail(40U);
    const ku_socket_t socket = (ku_socket_t)created;
    if (ku_socket_close(socket) != KU_STATUS_OK ||
        ku_socket_close(socket) != KU_STATUS_STALE_HANDLE) {
        fail(41U);
    }
    (void)u_puts("[TEST] status_socket_stale_handle: PASS\n");

    (void)ku_file_unlink(STATUS_FILE, sizeof(STATUS_FILE) - 1U);
    if (ku_file_create(STATUS_FILE, sizeof(STATUS_FILE) - 1U) != KU_STATUS_OK) {
        fail(50U);
    }
    created = ku_file_open(STATUS_FILE, sizeof(STATUS_FILE) - 1U);
    if (created <= 0) fail(51U);
    const ku_file_t file = (ku_file_t)created;
    if (ku_file_close(file) != KU_STATUS_OK ||
        ku_file_close(file) != KU_STATUS_STALE_HANDLE) {
        fail(52U);
    }
    if (ku_file_unlink(STATUS_FILE, sizeof(STATUS_FILE) - 1U) != KU_STATUS_OK) {
        fail(53U);
    }
    (void)u_puts("[TEST] status_vfs_stale_handle: PASS\n");
    (void)u_puts("[TEST] unified_status_runtime: PASS\n");
    ku_exit(0);
}
