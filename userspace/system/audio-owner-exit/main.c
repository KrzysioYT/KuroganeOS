#include "../../runtime/user.h"

#include <kurogane/audio.h>

#define AUDIO_OWNER_ATTEMPTS 2000U

__attribute__((noreturn)) void _start(void) {
    int16_t samples[KU_AUDIO_PCM_MAX_FRAMES * KU_AUDIO_PCM_CHANNELS];
    ku_audio_state state;
    size_t sample;
    uint32_t attempt;

    for (sample = 0U; sample < sizeof(state); ++sample) {
        ((uint8_t*)&state)[sample] = 0U;
    }
    state.structure_size = sizeof(state);
    if (ku_audio_get_state(&state) != KU_STATUS_OK || state.available == 0U) {
        ku_exit(1);
    }

    for (sample = 0U;
         sample < KU_AUDIO_PCM_MAX_FRAMES * KU_AUDIO_PCM_CHANNELS;
         ++sample) {
        samples[sample] = (sample & 1U) == 0U ? INT16_C(1800) : INT16_C(-1800);
    }

    for (attempt = 0U; attempt < AUDIO_OWNER_ATTEMPTS; ++attempt) {
        const ku_status_t status = ku_audio_play_pcm16_stereo(
            samples, KU_AUDIO_PCM_MAX_FRAMES);
        if (status == KU_STATUS_OK) {
            (void)u_puts("[TEST] audio_owner_exit_armed: PASS\n");
            /* Exit while DMA is owned by this PID. Kernel cleanup must stop it. */
            ku_exit(0);
        }
        if (status != KU_STATUS_WOULD_BLOCK) ku_exit(2);
        (void)ku_sleep(1U);
    }

    ku_exit(3);
}
