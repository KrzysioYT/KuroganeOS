#include <assert.h>
#include <string.h>
#include "../userspace/runtime/user.h"
#include <kurogane/settings.h>
#include <kurogane/filesystem.h>

/* A bounded filesystem with the production FAT32 no-replace rename contract. */
typedef struct test_file { int exists; size_t size, cursor; unsigned char bytes[16384]; } test_file;
static test_file files[3];
static int fail_publish, fail_restore;
static int path_id(const char* path) {
    if (strcmp(path, "/settings.db") == 0) return 0;
    if (strcmp(path, "/settings.tmp") == 0) return 1;
    assert(strcmp(path, "/settings.bak") == 0);
    return 2;
}
static ku_status_t fake_unlink(const char* path, size_t size) {
    test_file* file = &files[path_id(path)]; (void)size;
    if (!file->exists) return KU_STATUS_NOT_FOUND;
    memset(file, 0, sizeof(*file)); return KU_STATUS_OK;
}
static ku_status_t fake_create(const char* path, size_t size) {
    test_file* file = &files[path_id(path)]; (void)size;
    if (file->exists) return KU_STATUS_ALREADY_EXISTS;
    memset(file, 0, sizeof(*file)); file->exists = 1; return KU_STATUS_OK;
}
static ku_result_t fake_open_ex(const char* path, size_t size, uint64_t flags) {
    const int id = path_id(path); (void)size; (void)flags;
    if (!files[id].exists) return KU_STATUS_NOT_FOUND;
    files[id].cursor = 0; return id + 1;
}
static ku_result_t fake_open(const char* path, size_t size) { return fake_open_ex(path, size, 0); }
static ku_result_t fake_write(ku_file_t handle, const void* data, size_t size) {
    test_file* file = &files[handle - 1];
    assert(file->cursor + size <= sizeof(file->bytes));
    memcpy(file->bytes + file->cursor, data, size);
    file->cursor += size; file->size = file->cursor; return (ku_result_t)size;
}
static ku_result_t fake_read(ku_file_t handle, void* data, size_t size) {
    test_file* file = &files[handle - 1];
    if (size > file->size - file->cursor) size = file->size - file->cursor;
    memcpy(data, file->bytes + file->cursor, size); file->cursor += size;
    return (ku_result_t)size;
}
static ku_status_t fake_close(ku_file_t handle) { (void)handle; return KU_STATUS_OK; }
static ku_status_t fake_sync(void) { return KU_STATUS_OK; }
static ku_status_t fake_stat(const char* path, size_t size, ku_file_stat* info) {
    test_file* file = &files[path_id(path)]; (void)size;
    if (!file->exists) return KU_STATUS_NOT_FOUND;
    info->type = KU_FILE_TYPE_REGULAR; info->size = file->size; return KU_STATUS_OK;
}
static ku_status_t fake_rename(const char* source, size_t source_size,
                             const char* destination, size_t destination_size) {
    int src = path_id(source), dst = path_id(destination);
    (void)source_size; (void)destination_size;
    if (!files[src].exists) return KU_STATUS_NOT_FOUND;
    if (files[dst].exists) return KU_STATUS_ALREADY_EXISTS;
    if ((src == 1 && dst == 0 && fail_publish) ||
        (src == 2 && dst == 0 && fail_restore)) return KU_STATUS_IO_ERROR;
    files[dst] = files[src]; memset(&files[src], 0, sizeof(files[src]));
    return KU_STATUS_OK;
}
#define ku_file_unlink fake_unlink
#define ku_file_create fake_create
#define ku_file_open_ex fake_open_ex
#define ku_file_open fake_open
#define ku_file_write fake_write
#define ku_file_read fake_read
#define ku_file_close fake_close
#define ku_file_sync fake_sync
#define ku_file_stat_path fake_stat
#define ku_file_rename fake_rename
#define _start settings_daemon_entry
#include "../userspace/system/settingsd/main.c"
#undef _start

int main(void) {
    ku_settings_request request;
    memset(&request, 0, sizeof(request));
    key_copy(request.key, "theme"); request.type = KU_SETTINGS_TYPE_BOOL;
    request.value_size = 1; request.value[0] = 1;
    assert(set_record(&request) == KU_STATUS_OK);
    fail_publish = 1;
    request.value[0] = 0;
    assert(set_record(&request) == KU_STATUS_IO_ERROR);
    assert(find_record("theme")->value[0] == 1);
    /* Restart after the failed replacement must retain the previous value. */
    assert(load_records() == KU_STATUS_OK);
    assert(find_record("theme") != NULL && find_record("theme")->value[0] == 1);
    fail_restore = 1;
    assert(set_record(&request) == KU_STATUS_IO_ERROR);
    assert(load_records() == KU_STATUS_OK);
    assert(find_record("theme") != NULL && find_record("theme")->value[0] == 1);
    fail_publish = fail_restore = 0;
    assert(set_record(&request) == KU_STATUS_OK);
    assert(load_records() == KU_STATUS_OK);
    assert(find_record("theme")->value[0] == 0);
    assert(delete_record("theme") == KU_STATUS_OK);
    assert(load_records() == KU_STATUS_OK && find_record("theme") == NULL);
    return 0;
}
