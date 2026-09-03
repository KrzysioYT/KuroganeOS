#include "../../runtime/user.h"

#include <kurogane/filesystem.h>

#define SOURCE_DIR "/kuro/source"
#define DESTINATION_DIR "/kuro/destination"
#define SOURCE_FILE "/kuro/source/pending"
#define PERSISTED_FILE "/kuro/destination/persisted"
#define PAYLOAD "KuroFS Ring-3 persistence"

static void fail(uint32_t code) {
    (void)u_puts("[TEST] kurofs_runtime: FAIL code=");
    (void)u_put_u64(code);
    (void)u_puts("\n");
    ku_exit((int32_t)code);
}

static void require_directory(const char* path, size_t size, uint32_t code) {
    const ku_status_t status = ku_file_mkdir(path, size);
    if (status != KU_STATUS_OK && status != KU_STATUS_ALREADY_EXISTS) {
        fail(code);
    }
}

static void first_boot(void) {
    const char payload[] = PAYLOAD;
    ku_result_t opened;
    ku_file_t file;
    ku_file_stat info;

    require_directory(SOURCE_DIR, sizeof(SOURCE_DIR) - 1U, 10U);
    require_directory(DESTINATION_DIR, sizeof(DESTINATION_DIR) - 1U, 11U);
    if (ku_file_create(SOURCE_FILE, sizeof(SOURCE_FILE) - 1U) != KU_STATUS_OK) {
        fail(12U);
    }
    opened = ku_file_open_ex(
        SOURCE_FILE, sizeof(SOURCE_FILE) - 1U, KU_FILE_OPEN_WRITE);
    if (opened <= 0) fail(13U);
    file = (ku_file_t)opened;
    if (ku_file_write(file, payload, sizeof(payload) - 1U) !=
            (ku_result_t)(sizeof(payload) - 1U) ||
        ku_file_close(file) != KU_STATUS_OK ||
        ku_file_sync() != KU_STATUS_OK) {
        fail(14U);
    }
    if (ku_file_rename(
            SOURCE_FILE, sizeof(SOURCE_FILE) - 1U,
            PERSISTED_FILE, sizeof(PERSISTED_FILE) - 1U) != KU_STATUS_OK ||
        ku_file_sync() != KU_STATUS_OK) {
        fail(15U);
    }
    if (ku_file_stat_path(
            SOURCE_FILE, sizeof(SOURCE_FILE) - 1U, &info) !=
            KU_STATUS_NOT_FOUND ||
        ku_file_stat_path(
            PERSISTED_FILE, sizeof(PERSISTED_FILE) - 1U, &info) !=
            KU_STATUS_OK ||
        info.type != KU_FILE_TYPE_REGULAR ||
        info.size != sizeof(payload) - 1U) {
        fail(16U);
    }
    (void)u_puts("[TEST] kurofs_runtime_first_boot: PASS\n");
    ku_exit(0);
}

static void second_boot(void) {
    const char payload[] = PAYLOAD;
    char received[sizeof(payload)] = {0};
    ku_result_t opened = ku_file_open(
        PERSISTED_FILE, sizeof(PERSISTED_FILE) - 1U);
    ku_file_t file;
    size_t index;
    if (opened <= 0) fail(20U);
    file = (ku_file_t)opened;
    if (ku_file_read(file, received, sizeof(received)) !=
            (ku_result_t)(sizeof(payload) - 1U) ||
        ku_file_close(file) != KU_STATUS_OK) {
        fail(21U);
    }
    for (index = 0U; index < sizeof(payload) - 1U; ++index) {
        if (received[index] != payload[index]) fail(22U);
    }
    (void)u_puts("[TEST] kurofs_runtime_second_boot: PASS\n");
    ku_exit(0);
}

__attribute__((noreturn)) void _start(void) {
    ku_file_stat info;
    const ku_status_t status = ku_file_stat_path(
        PERSISTED_FILE, sizeof(PERSISTED_FILE) - 1U, &info);
    if (status == KU_STATUS_NOT_FOUND) first_boot();
    if (status != KU_STATUS_OK || info.type != KU_FILE_TYPE_REGULAR) fail(1U);
    second_boot();
}
