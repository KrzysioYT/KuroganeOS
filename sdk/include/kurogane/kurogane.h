#ifndef KUROGANE_SYSTEM_LIBRARY_H
#define KUROGANE_SYSTEM_LIBRARY_H

#include <kurogane/desktop.h>
#include <kurogane/filesystem.h>
#include <kurogane/memory.h>
#include <kurogane/network.h>
#include <kurogane/process.h>
#include <kurogane/system.h>

#ifdef __cplusplus
extern "C" {
#endif

ku_status_t kuro_sleep(uint64_t ticks);
ku_status_t kuro_yield(void);
ku_status_t kuro_spawn_wait(
    const char* path, size_t path_size, int32_t* exit_code);

static inline ku_status_t kuro_sleep_seconds(uint64_t seconds) {
    if (seconds == 0U ||
        seconds > UINT64_MAX / KU_SYSTEM_TICKS_PER_SECOND) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    return kuro_sleep(seconds * KU_SYSTEM_TICKS_PER_SECOND);
}

#ifdef __cplusplus
}
#endif
#endif
