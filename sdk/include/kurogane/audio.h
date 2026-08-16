#ifndef KUROGANE_SDK_AUDIO_H
#define KUROGANE_SDK_AUDIO_H

#include <kurogane/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_AUDIO_ABI_VERSION UINT32_C(1)

enum ku_audio_flags {
    KU_AUDIO_OUTPUT_READY = UINT32_C(1) << 0,
    KU_AUDIO_OUTPUT_BUSY = UINT32_C(1) << 1
};

typedef struct ku_audio_info {
    uint32_t structure_size;
    uint32_t flags;
    uint32_t sample_rate;
    uint32_t maximum_frames_per_buffer;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t reserved;
} ku_audio_info;

static inline ku_status_t ku_audio_get_info(ku_audio_info* info) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_AUDIO_INFO,
        (uint64_t)(uintptr_t)info,
        sizeof(ku_audio_info),
        0);
}

/*
 * Submit interleaved signed PCM16 stereo frames at the rate advertised by
 * ku_audio_get_info(). The kernel copies the validated user buffer into its
 * own DMA memory before returning.
 */
static inline ku_status_t ku_audio_play_pcm16_stereo(
    const int16_t* samples,
    size_t frame_count) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_AUDIO_PLAY_PCM16,
        (uint64_t)(uintptr_t)samples,
        (uint64_t)frame_count,
        0);
}

static inline ku_status_t ku_audio_stop(void) {
    return (ku_status_t)ku_syscall3(KU_SYS_AUDIO_STOP, 0, 0, 0);
}

#if defined(__cplusplus)
static_assert(sizeof(ku_audio_info) == 24, "audio info ABI mismatch");
#else
_Static_assert(sizeof(ku_audio_info) == 24, "audio info ABI mismatch");
#endif

#ifdef __cplusplus
}
#endif

#endif
