#ifndef KUROGANE_SDK_FILESYSTEM_H
#define KUROGANE_SDK_FILESYSTEM_H

#include <kurogane/syscall.h>

typedef ku_handle_t ku_file_t;

static inline ku_result_t ku_file_open(const char* path, size_t size) {
    return ku_open(path, size, KU_OPEN_READ);
}
static inline ku_result_t ku_file_read(
    ku_file_t file, void* buffer, size_t size) {
    return ku_read(file, buffer, size);
}
static inline ku_status_t ku_file_close(ku_file_t file) {
    return ku_close(file);
}

#endif
