#include <assert.h>
#include <string.h>
#include <kurogane/filesystem.h>

struct fixture {
    const char* path;
    const char* data;
    size_t size;
    ku_status_t open_status;
    int read_error;
    ku_status_t close_status;
    size_t offset;
    int open;
};

static struct fixture files[2];
static ku_status_t first_run;
static ku_status_t install_ok;

static ku_result_t fixture_open(const char* path, size_t size, uint64_t flags) {
    assert(size == strlen(path) && flags == KU_OPEN_READ);
    for (size_t i = 0; i < 2; ++i) {
        if (strcmp(path, files[i].path) != 0) continue;
        struct fixture* file = &files[i];
        if (file->open_status != KU_STATUS_OK) return file->open_status;
        assert(!file->open);
        file->open = 1;
        file->offset = 0;
        return (ku_result_t)i + 1;
    }
    assert(0 && "unexpected config file");
    return KU_STATUS_NOT_FOUND;
}

static ku_result_t fixture_read(ku_handle_t fd, void* data, size_t capacity) {
    assert(fd == 1U || fd == 2U);
    struct fixture* file = &files[fd - 1U];
    assert(file->open);
    if (file->read_error && file->offset != 0U) return KU_STATUS_IO_ERROR;
    size_t count = file->size - file->offset;
    if (count > capacity) count = capacity;
    // Exercise legal short reads before EOF, including a late I/O failure.
    if (count > 13U) count = 13U;
    if (count) memcpy(data, file->data + file->offset, count);
    file->offset += count;
    return (ku_result_t)count;
}

static ku_status_t fixture_close(ku_handle_t fd) {
    assert(fd == 1U || fd == 2U);
    struct fixture* file = &files[fd - 1U];
    assert(file->open);
    file->open = 0;
    return file->close_status;
}

static ku_status_t fixture_stat(const char* path, size_t size, ku_file_stat* info) {
    assert(size == strlen(path) && info != NULL);
    if (strcmp(path, "/etc/first.run") == 0) return first_run;
    assert(strcmp(path, "/etc/install.ok") == 0);
    return install_ok;
}

#define ku_open fixture_open
#define ku_read fixture_read
#define ku_close fixture_close
#define ku_file_stat_path fixture_stat
#include "../userspace/runtime/dev_login_profile.h"

static const char user[] =
    "USERNAME=alice\nPASSWORD_REQUIRED=1\nPASSWORD_HASH=1234567890abcdef\n"
    "HASH_SCHEME=FNV1A64-DEV\n";
static const char no_password[] =
    "USERNAME=alice\nPASSWORD_REQUIRED=0\nPASSWORD_HASH=0000000000000000\n"
    "HASH_SCHEME=FNV1A64-DEV\n";
static const char locale[] = "LANG=pl-PL\n";

static void reset_fixture(void) {
    files[0] = (struct fixture){.path = "/etc/user.cfg",
                                .open_status = KU_STATUS_NOT_FOUND};
    files[1] = (struct fixture){.path = "/etc/locale.cfg",
                                .open_status = KU_STATUS_NOT_FOUND};
    first_run = KU_STATUS_NOT_FOUND;
    install_ok = KU_STATUS_NOT_FOUND;
}

static void set_file(size_t index, const char* data, size_t size) {
    files[index].data = data;
    files[index].size = size;
    files[index].open_status = KU_STATUS_OK;
}

static ku_dev_login_profile expect(ku_dev_login_kind kind) {
    ku_dev_login_profile profile;
    ku_dev_login_load_profile(&profile);
    assert(profile.kind == kind);
    assert(!files[0].open && !files[1].open);
    return profile;
}

int main(void) {
    reset_fixture();
    ku_dev_login_profile profile = expect(KU_DEV_LOGIN_LIVE);
    assert(strcmp(profile.credentials.username, "developer") == 0);
    assert(!profile.credentials.password_required);

    first_run = KU_STATUS_OK;
    expect(KU_DEV_LOGIN_BLOCKED);
    first_run = KU_STATUS_NOT_FOUND;
    install_ok = KU_STATUS_OK;
    expect(KU_DEV_LOGIN_BLOCKED);
    install_ok = KU_STATUS_IO_ERROR;
    expect(KU_DEV_LOGIN_BLOCKED);

    reset_fixture();
    files[0].open_status = KU_STATUS_IO_ERROR;
    expect(KU_DEV_LOGIN_BLOCKED);
    reset_fixture();
    set_file(1U, locale, sizeof(locale) - 1U);
    expect(KU_DEV_LOGIN_BLOCKED);  // user.cfg missing but this is not pristine media.
    set_file(0U, user, sizeof(user) - 1U);
    profile = expect(KU_DEV_LOGIN_INSTALLED);
    assert(strcmp(profile.credentials.username, "alice") == 0);
    assert(strcmp(profile.locale, "pl-PL") == 0);
    assert(profile.credentials.password_required);
    assert(profile.credentials.password_hash == UINT64_C(0x1234567890abcdef));
    set_file(0U, no_password, sizeof(no_password) - 1U);
    profile = expect(KU_DEV_LOGIN_INSTALLED);
    assert(!profile.credentials.password_required && profile.credentials.password_hash == 0);

    files[0].read_error = 1;
    expect(KU_DEV_LOGIN_BLOCKED);
    files[0].read_error = 0;
    files[0].close_status = KU_STATUS_IO_ERROR;
    expect(KU_DEV_LOGIN_BLOCKED);
    files[0].close_status = KU_STATUS_OK;
    set_file(0U, user, sizeof(user));  // Otherwise valid prefix with embedded NUL.
    expect(KU_DEV_LOGIN_BLOCKED);
    char oversized[384];
    memset(oversized, 'x', sizeof(oversized));
    memcpy(oversized, user, sizeof(user) - 1U);
    set_file(0U, oversized, sizeof(oversized));
    expect(KU_DEV_LOGIN_BLOCKED);
    set_file(0U, "", 0);
    expect(KU_DEV_LOGIN_BLOCKED);
    set_file(0U, user, sizeof(user) - 1U);
    files[1].open_status = KU_STATUS_NOT_FOUND;
    expect(KU_DEV_LOGIN_BLOCKED);
}
