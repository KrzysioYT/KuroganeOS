#include <assert.h>
#include <string.h>
#include <kurogane/desktop.h>
#include <kurogane/filesystem.h>

/* Deterministic in-memory FAT32 model: rename never overwrites a destination. */
typedef struct model_file {
    int exists;
    size_t size, cursor;
    uint8_t bytes[32];
} model_file;
static model_file storage[3];
static int fail_write, fail_publish;
static size_t read_chunk_limit;
static int fail_read_after_chunk;
static uint8_t pinned[8] = {1U};
static uint8_t order[8] = {0,1,2,3,4,5,6,7};

static int file_id(const char* name) {
    if (strcmp(name, "/home/desktop.cfg") == 0) return 0;
    if (strcmp(name, "/home/desktop.tmp") == 0) return 1;
    assert(strcmp(name, "/home/desktop.bak") == 0);
    return 2;
}
static ku_status_t fake_stat(const char* p, size_t n, ku_file_stat* out) {
    model_file* f = &storage[file_id(p)]; (void)n;
    if (!f->exists) return KU_STATUS_NOT_FOUND;
    out->type = KU_FILE_TYPE_REGULAR; out->size = f->size;
    return KU_STATUS_OK;
}
static ku_status_t fake_create(const char* p, size_t n) {
    model_file* f = &storage[file_id(p)]; (void)n;
    if (f->exists) return KU_STATUS_ALREADY_EXISTS;
    memset(f, 0, sizeof(*f)); f->exists = 1;
    return KU_STATUS_OK;
}
static ku_status_t fake_unlink(const char* p, size_t n) {
    model_file* f = &storage[file_id(p)]; (void)n;
    if (!f->exists) return KU_STATUS_NOT_FOUND;
    memset(f, 0, sizeof(*f)); return KU_STATUS_OK;
}
static ku_result_t fake_open(const char* p, size_t n) {
    const int id = file_id(p); (void)n;
    if (!storage[id].exists) return KU_STATUS_NOT_FOUND;
    storage[id].cursor = 0U; return id + 1;
}
static ku_result_t fake_open_ex(const char* p, size_t n, uint64_t flags) {
    (void)flags; return fake_open(p, n);
}
static ku_result_t fake_read(ku_file_t h, void* dst, size_t n) {
    model_file* f = &storage[h - 1U];
    if (fail_read_after_chunk && f->cursor != 0U) return KU_STATUS_IO_ERROR;
    if (n > f->size - f->cursor) n = f->size - f->cursor;
    if (read_chunk_limit != 0U && n > read_chunk_limit) n = read_chunk_limit;
    memcpy(dst, f->bytes + f->cursor, n); f->cursor += n;
    return (ku_result_t)n;
}
static ku_result_t fake_write(ku_file_t h, const void* src, size_t n) {
    model_file* f = &storage[h - 1U];
    if (fail_write) return KU_STATUS_IO_ERROR;
    assert(f->cursor + n <= sizeof(f->bytes));
    memcpy(f->bytes + f->cursor, src, n);
    f->cursor += n; f->size = f->cursor;
    return (ku_result_t)n;
}
static ku_status_t fake_close(ku_file_t h) { (void)h; return KU_STATUS_OK; }
static ku_status_t fake_sync(void) { return KU_STATUS_OK; }
static ku_status_t fake_rename(const char* a, size_t al, const char* b, size_t bl) {
    const int from = file_id(a), to = file_id(b); (void)al; (void)bl;
    if (!storage[from].exists) return KU_STATUS_NOT_FOUND;
    if (storage[to].exists) return KU_STATUS_ALREADY_EXISTS;
    if (from == 1 && to == 0 && fail_publish) return KU_STATUS_IO_ERROR;
    storage[to] = storage[from];
    memset(&storage[from], 0, sizeof(storage[from]));
    return KU_STATUS_OK;
}
static ku_status_t fake_pin(ku_desktop_pin_request* r) {
    if (r->app_id >= 8) return KU_STATUS_INVALID_ARGUMENT;
    const uint32_t id = r->app_id;
    if (r->action == KU_DESKTOP_PIN_QUERY) {
        r->pinned = pinned[id]; return KU_STATUS_OK;
    }
    if (r->action == KU_DESKTOP_PIN_SET) {
        pinned[id] = id == 0 ? 1U : r->value != 0U;
        r->pinned = pinned[id]; return KU_STATUS_OK;
    }
    if (r->action == KU_DESKTOP_PIN_ORDER_QUERY ||
        r->action == KU_DESKTOP_PIN_ORDER_SET) {
        uint32_t current = 0;
        while (current < 8 && order[current] != id) ++current;
        assert(current < 8);
        if (r->action == KU_DESKTOP_PIN_ORDER_QUERY) {
            r->value = current; return KU_STATUS_OK;
        }
        if (id == 0 || r->value == 0 || r->value >= 8)
            return KU_STATUS_INVALID_ARGUMENT;
        const uint32_t target = r->value;
        if (target < current) {
            for (uint32_t p = current; p > target; --p)
                order[p] = order[p - 1U];
        } else {
            for (uint32_t p = current; p < target; ++p)
                order[p] = order[p + 1U];
        }
        order[target] = (uint8_t)id;
        return KU_STATUS_OK;
    }
    return KU_STATUS_INVALID_ARGUMENT;
}

/* Host libc on Linux does not provide the Kurogane userspace strlcpy. */
static size_t strlcpy(char* out, const char* in, size_t capacity) {
    const size_t length = strlen(in);
    if (capacity != 0U) {
        const size_t size = length < capacity - 1U ? length : capacity - 1U;
        memcpy(out, in, size);
        out[size] = 0;
    }
    return length;
}
#define ku_file_stat_path fake_stat
#define ku_file_create fake_create
#define ku_file_unlink fake_unlink
#define ku_file_open fake_open
#define ku_file_open_ex fake_open_ex
#define ku_file_read fake_read
#define ku_file_write fake_write
#define ku_file_close fake_close
#define ku_file_sync fake_sync
#define ku_file_rename fake_rename
#define ku_desktop_pin fake_pin
#define main launcher_unused_entry
#include "../userspace/gui/launcher/main.c"
#undef main

int main(void) {
    pinned[2] = 1U;
    assert(!load_pin_state());
    assert(save_pin_state());
    assert(storage[0].exists && storage[0].size == FLUX_DOCK_RECORD_BYTES);

    ku_desktop_pin_request move = {0};
    move.structure_size = sizeof(move);
    move.app_id = 2U;
    move.action = KU_DESKTOP_PIN_ORDER_SET;
    move.value = 1U;
    assert(fake_pin(&move) == KU_STATUS_OK);
    pinned[3] = 1U;
    assert(save_pin_state());
    assert(storage[2].exists && storage[2].size == FLUX_DOCK_RECORD_BYTES);
    order[1] = 1; order[2] = 2;
    pinned[2] = 0U; pinned[3] = 0U;
    assert(load_pin_state());
    assert(order[1] == 2 && order[2] == 1);
    assert(pinned[2] == 1U && pinned[3] == 1U);

    /* Positive short reads are normal; a truncated second chunk is not. */
    read_chunk_limit = 2U;
    order[1] = 1U; order[2] = 2U;
    pinned[2] = 0U;
    assert(load_pin_state());
    assert(order[1] == 2U && order[2] == 1U && pinned[2] == 1U);
    read_chunk_limit = 2U;
    fail_read_after_chunk = 1;
    assert(!load_pin_state());
    fail_read_after_chunk = 0;
    read_chunk_limit = 0U;
    assert(load_pin_state() && order[1] == 2U);

    fail_write = 1;
    assert(!save_pin_state());
    fail_write = 0;
    assert(load_pin_state());
    assert(order[1] == 2);

    /* Simulate an interrupted publish. The previous generation survives. */
    fail_publish = 1;
    assert(!save_pin_state());
    fail_publish = 0;
    assert(load_pin_state());
    assert(order[1] == 2);
    // A failed publish may restore the only backup into the primary path.
    // Make a new verified backup before injecting primary corruption.
    assert(save_pin_state());
    assert(storage[2].exists);

    /* A single flipped mask bit is structurally valid but must fail the
     * checksum and restore the verified previous generation. */
    storage[0].bytes[0] ^= (uint8_t)(UINT8_C(1) << 3U);
    assert(load_pin_state());
    assert(g_dock_recovered_from_backup == 1);
    assert(pinned[3] == 1U && order[1] == 2U);

    /* Corrupt current permutation; recover verified backup generation. */
    storage[0].bytes[2] = 8U;
    assert(load_pin_state());
    assert(g_dock_recovered_from_backup == 1);
    assert(order[1] == 2);

    fail_publish = 1;
    assert(!save_pin_state());
    assert(storage[2].exists && storage[2].bytes[2] == 2U);
    assert(load_pin_state());
    assert(g_dock_recovered_from_backup == 1);
    fail_publish = 0;
    assert(save_pin_state());
    assert(load_pin_state());
    assert(g_dock_recovered_from_backup == 0);

    /* One-byte old-format desktop.cfg must remain readable. */
    memset(storage, 0, sizeof(storage));
    storage[0].exists = 1; storage[0].size = 1; storage[0].bytes[0] = 3U;
    assert(load_pin_state());
    assert(pinned[1] == 1U && pinned[2] == 0U);
    assert(save_pin_state());
    assert(storage[0].size == FLUX_DOCK_RECORD_BYTES);
    assert(flux_dock_record_valid(storage[0].bytes));
    return 0;
}
