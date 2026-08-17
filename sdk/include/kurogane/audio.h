#ifndef KUROGANE_SDK_AUDIO_H
#define KUROGANE_SDK_AUDIO_H

#include <stdint.h>
#include <kurogane/status.h>
#include <kurogane/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_AUDIO_STATE_VERSION UINT32_C(1)

typedef struct ku_audio_state {
    uint32_t structure_size;
    uint32_t version;
    uint32_t available;
    uint32_t muted;
    uint32_t volume_percent;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
} ku_audio_state;

typedef struct ku_audio_set_request {
    uint32_t structure_size;
    uint32_t volume_percent;
    uint32_t muted;
    uint32_t reserved;
} ku_audio_set_request;

static inline ku_status_t ku_audio_get_state(ku_audio_state* output) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_AUDIO_STATUS,
        (uint64_t)(uintptr_t)output,
        (uint64_t)sizeof(ku_audio_state),
        0U);
}

static inline ku_status_t ku_audio_set(const ku_audio_set_request* request) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_AUDIO_SET,
        (uint64_t)(uintptr_t)request,
        (uint64_t)sizeof(ku_audio_set_request),
        0U);
}

#ifdef __cplusplus
}
#endif
#endif
