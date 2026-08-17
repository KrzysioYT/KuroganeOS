#ifndef KUROGANE_SDK_AUDIO_H
#define KUROGANE_SDK_AUDIO_H

#include <stddef.h>
#include <stdint.h>
#include <kurogane/status.h>
#include <kurogane/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_AUDIO_STATE_VERSION UINT32_C(1)
#define KU_AUDIO_PCM_SAMPLE_RATE UINT32_C(48000)
#define KU_AUDIO_PCM_CHANNELS UINT32_C(2)
#define KU_AUDIO_PCM_BITS_PER_SAMPLE UINT32_C(16)
#define KU_AUDIO_PCM_MAX_FRAMES ((size_t)1024U)

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

/*
 * Submit one bounded interleaved signed-16 stereo buffer at 48 kHz. The kernel
 * copies accepted samples into its DMA page before returning. KU_STATUS_WOULD_BLOCK
 * means the exclusive playback engine is still busy; call ku_audio_poll().
 */
static inline ku_status_t ku_audio_play_pcm16_stereo(
    const int16_t* samples,
    size_t frame_count) {
    if (samples == NULL || frame_count == 0U) return KU_STATUS_INVALID_ARGUMENT;
    if (frame_count > KU_AUDIO_PCM_MAX_FRAMES) return KU_STATUS_OUT_OF_RANGE;
    return (ku_status_t)ku_syscall3(
        KU_SYS_AUDIO_PLAY_PCM16,
        (uint64_t)(uintptr_t)samples,
        (uint64_t)frame_count,
        0U);
}

/* Returns KU_STATUS_WOULD_BLOCK while this process' submitted buffer is active. */
static inline ku_status_t ku_audio_poll(void) {
    return (ku_status_t)ku_syscall3(KU_SYS_AUDIO_POLL, 0U, 0U, 0U);
}

/* Stop playback owned by the calling process. */
static inline ku_status_t ku_audio_stop(void) {
    return (ku_status_t)ku_syscall3(KU_SYS_AUDIO_STOP, 0U, 0U, 0U);
}

#ifdef __cplusplus
}
#endif
#endif
