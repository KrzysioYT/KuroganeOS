#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::audio::ac97::detail {

constexpr size_t kDmaPageBytes = 4096U;
constexpr size_t kBytesPerFrame = 4U;
constexpr size_t kDescriptorCount = 32U;
constexpr size_t kFramesPerDescriptor = kDmaPageBytes / kBytesPerFrame;
constexpr size_t kMaximumFrames = kDescriptorCount * kFramesPerDescriptor;

constexpr bool frame_count_supported(size_t frame_count) {
    return frame_count > 0U && frame_count <= kMaximumFrames;
}

constexpr size_t descriptor_count_for_frames(size_t frame_count) {
    if (frame_count == 0U) return 0U;
    return (frame_count + kFramesPerDescriptor - 1U) / kFramesPerDescriptor;
}

constexpr size_t frames_for_descriptor(size_t frame_count, size_t descriptor_index) {
    const size_t offset = descriptor_index * kFramesPerDescriptor;
    if (offset >= frame_count) return 0U;
    const size_t remaining = frame_count - offset;
    return remaining < kFramesPerDescriptor ? remaining : kFramesPerDescriptor;
}

constexpr uint16_t sample_words_for_frames(size_t frame_count) {
    return static_cast<uint16_t>(frame_count * 2U);
}

static_assert(kDmaPageBytes % kBytesPerFrame == 0U,
              "AC97 DMA page must contain whole stereo frames");
static_assert(kFramesPerDescriptor * 2U <= UINT16_MAX,
              "AC97 descriptor sample count must fit the 16-bit length field");
static_assert(kDescriptorCount <= 32U,
              "Intel ICH AC97 PCM BDL supports at most 32 descriptors");

} // namespace drivers::audio::ac97::detail
