#include "../../runtime/user.h"

#include <kurogane/filesystem.h>

#define TEST_DIR "/fstest"
#define TEST_FILE_A "/fstest/one.txt"
#define TEST_FILE_B "/fstest/two.txt"
#define TEST_PAYLOAD "Kurogane filesystem ABI"

static void fail(uint32_t code) {
    (void)u_puts("[TEST] filesystem_service_api: FAIL code=");
    (void)u_put_u64(code);
    (void)u_puts("\n");
    ku_exit((int32_t)code);
}

static int entry_named(const ku_directory_entry* entry, const char* name) {
    size_t index;
    const size_t size = u_strlen(name);
    if (entry == (const ku_directory_entry*)0 || entry->name_length != size) return 0;
    for (index = 0U; index < size; ++index) {
        if (entry->name[index] != name[index]) return 0;
    }
    return entry->name[size] == '\0';
}

__attribute__((noreturn)) void _start(void) {
    const char payload[] = TEST_PAYLOAD;
    ku_result_t opened;
    ku_file_t file;
    ku_file_stat stat;
    ku_directory_entry entry;
    ku_status_t status;
    int found_one = 0;

    /* Clean a stale qualification tree without weakening error checks below. */
    (void)ku_file_unlink(TEST_FILE_A, sizeof(TEST_FILE_A) - 1U);
    (void)ku_file_unlink(TEST_FILE_B, sizeof(TEST_FILE_B) - 1U);
    (void)ku_file_rmdir(TEST_DIR, sizeof(TEST_DIR) - 1U);

    status = ku_file_mkdir(TEST_DIR, sizeof(TEST_DIR) - 1U);
    if (status != KU_STATUS_OK) fail(1U);

    status = ku_file_create(TEST_FILE_A, sizeof(TEST_FILE_A) - 1U);
    if (status != KU_STATUS_OK) fail(2U);

    opened = ku_file_open_ex(
        TEST_FILE_A,
        sizeof(TEST_FILE_A) - 1U,
        KU_FILE_OPEN_WRITE);
    if (opened <= 0) fail(3U);
    file = (ku_file_t)opened;
    if (ku_file_write(file, payload, sizeof(payload) - 1U) !=
        (ku_result_t)(sizeof(payload) - 1U)) {
        (void)ku_file_close(file);
        fail(4U);
    }
    if (ku_file_close(file) != KU_STATUS_OK) fail(5U);
    if (ku_file_sync() != KU_STATUS_OK) fail(6U);

    status = ku_file_stat_path(TEST_FILE_A, sizeof(TEST_FILE_A) - 1U, &stat);
    if (status != KU_STATUS_OK || stat.type != KU_FILE_TYPE_REGULAR ||
        stat.size != sizeof(payload) - 1U) {
        fail(7U);
    }

    opened = ku_file_open_ex(
        TEST_DIR,
        sizeof(TEST_DIR) - 1U,
        KU_FILE_OPEN_READ | KU_FILE_OPEN_DIRECTORY);
    if (opened <= 0) fail(8U);
    file = (ku_file_t)opened;
    for (;;) {
        status = ku_file_readdir(file, &entry);
        if (status == KU_STATUS_END_OF_STREAM) break;
        if (status != KU_STATUS_OK) {
            (void)ku_file_close(file);
            fail(9U);
        }
        if (entry_named(&entry, "one.txt")) {
            if (entry.type != KU_FILE_TYPE_REGULAR ||
                entry.size != sizeof(payload) - 1U) {
                (void)ku_file_close(file);
                fail(10U);
            }
            found_one = 1;
        }
    }
    if (ku_file_close(file) != KU_STATUS_OK || !found_one) fail(11U);

    status = ku_file_rename(
        TEST_FILE_A,
        sizeof(TEST_FILE_A) - 1U,
        TEST_FILE_B,
        sizeof(TEST_FILE_B) - 1U);
    if (status != KU_STATUS_OK) fail(12U);

    status = ku_file_stat_path(TEST_FILE_A, sizeof(TEST_FILE_A) - 1U, &stat);
    if (status != KU_STATUS_NOT_FOUND) fail(13U);
    status = ku_file_stat_path(TEST_FILE_B, sizeof(TEST_FILE_B) - 1U, &stat);
    if (status != KU_STATUS_OK || stat.type != KU_FILE_TYPE_REGULAR ||
        stat.size != sizeof(payload) - 1U) {
        fail(14U);
    }

    status = ku_file_unlink(TEST_FILE_B, sizeof(TEST_FILE_B) - 1U);
    if (status != KU_STATUS_OK) fail(15U);
    status = ku_file_rmdir(TEST_DIR, sizeof(TEST_DIR) - 1U);
    if (status != KU_STATUS_OK) fail(16U);
    if (ku_file_sync() != KU_STATUS_OK) fail(17U);

    status = ku_file_stat_path(TEST_DIR, sizeof(TEST_DIR) - 1U, &stat);
    if (status != KU_STATUS_NOT_FOUND) fail(18U);

    (void)u_puts("[TEST] filesystem_service_api: PASS\n");
    ku_exit(0);
}
