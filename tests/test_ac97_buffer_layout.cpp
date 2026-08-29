#include "../kernel/drivers/audio/ac97_buffer_layout.hpp"

#include <assert.h>
#include <stddef.h>

namespace {

void verify_layout(size_t frame_count) {
    using namespace drivers::audio::ac97::detail;

    assert(frame_count_supported(frame_count));
    const size_t descriptor_count = descriptor_count_for_frames(frame_count);
    assert(descriptor_count >= 1U);
    assert(descriptor_count <= kDescriptorCount);

    size_t reconstructed_frames = 0U;
    for (size_t index = 0U; index < descriptor_count; ++index) {
        const size_t descriptor_frames = frames_for_descriptor(frame_count, index);
        assert(descriptor_frames >= 1U);
        assert(descriptor_frames <= kFramesPerDescriptor);
        assert(static_cast<size_t>(sample_words_for_frames(descriptor_frames)) ==
               descriptor_frames * 2U);
        reconstructed_frames += descriptor_frames;
    }

    assert(reconstructed_frames == frame_count);
    assert(frames_for_descriptor(frame_count, descriptor_count) == 0U);
}

} // namespace

int main() {
    using namespace drivers::audio::ac97::detail;

    assert(!frame_count_supported(0U));
    assert(frame_count_supported(1U));
    assert(frame_count_supported(kMaximumFrames));
    assert(!frame_count_supported(kMaximumFrames + 1U));

    assert(descriptor_count_for_frames(0U) == 0U);
    assert(descriptor_count_for_frames(1U) == 1U);
    assert(descriptor_count_for_frames(kFramesPerDescriptor) == 1U);
    assert(descriptor_count_for_frames(kFramesPerDescriptor + 1U) == 2U);
    assert(descriptor_count_for_frames(kMaximumFrames) == kDescriptorCount);

    verify_layout(1U);
    verify_layout(kFramesPerDescriptor);
    verify_layout(kFramesPerDescriptor + 1U);
    verify_layout((kFramesPerDescriptor * 7U) + 19U);
    verify_layout(kMaximumFrames);

    return 0;
}
