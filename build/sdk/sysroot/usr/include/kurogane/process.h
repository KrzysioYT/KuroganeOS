#ifndef KUROGANE_SDK_PROCESS_H
#define KUROGANE_SDK_PROCESS_H

#include <kurogane/syscall.h>

typedef uint64_t ku_pid_t;
typedef uint64_t ku_tid_t;

static inline ku_pid_t ku_process_id(void) { return ku_getpid(); }
static inline ku_tid_t ku_thread_id(void) { return ku_gettid(); }
static inline ku_result_t ku_process_spawn(const char* path, size_t size) {
    return ku_spawn(path, size);
}
static inline ku_status_t ku_process_wait(ku_pid_t pid, int32_t* status) {
    return ku_wait(pid, status);
}

#endif
