#include <cassert>
#include <cstdint>
#include <cstdio>

#include "../kernel/drivers/audio/hda_protocol.hpp"

using namespace drivers::audio::hda::protocol;

namespace {

void test_capabilities() {
    const uint16_t raw =
        static_cast<uint16_t>(
            (4U << 12U) |
            (3U << 8U) |
            (5U << 3U) |
            (2U << 1U) |
            1U);

    ControllerCapabilities capabilities{};
    assert(decode_global_capabilities(raw, &capabilities) == Status::Ok);
    assert(capabilities.output_stream_count == 4U);
    assert(capabilities.input_stream_count == 3U);
    assert(capabilities.bidirectional_stream_count == 5U);
    assert(capabilities.serial_data_out_count == 4U);
    assert(capabilities.supports_64_bit_addressing);

    const ControllerCapabilities sentinel = capabilities;
    assert(decode_global_capabilities(
        static_cast<uint16_t>(raw | (3U << 1U)),
        &capabilities) == Status::UnsupportedCapabilities);
    assert(capabilities.output_stream_count == sentinel.output_stream_count);

    assert(decode_global_capabilities(
        static_cast<uint16_t>((31U << 3U) | 1U),
        &capabilities) == Status::UnsupportedCapabilities);
}

void test_ring_size() {
    RingSize ring{};
    assert(select_ring_size(0x70U, 256U, &ring) == Status::Ok);
    assert(ring.entry_count == 256U && ring.selector == 2U);

    assert(select_ring_size(0x30U, 256U, &ring) == Status::Ok);
    assert(ring.entry_count == 16U && ring.selector == 1U);

    assert(select_ring_size(0x50U, 15U, &ring) == Status::Ok);
    assert(ring.entry_count == 2U && ring.selector == 0U);

    const RingSize sentinel{77U, 3U};
    ring = sentinel;
    assert(select_ring_size(0x20U, 15U, &ring) ==
        Status::UnsupportedRingSize);
    assert(ring.entry_count == sentinel.entry_count);
    assert(select_ring_size(0x00U, 256U, &ring) ==
        Status::UnsupportedRingSize);
}

void test_verb_and_response() {
    uint32_t verb = 0U;
    assert(build_verb_12(
        2U, 0x1BU, 0xF00U, 0x04U, &verb) == Status::Ok);
    assert(verb == UINT32_C(0x21BF0004));

    const uint32_t sentinel_verb = UINT32_C(0xAABBCCDD);
    verb = sentinel_verb;
    assert(build_verb_12(
        16U, 0U, 0xF00U, 0U, &verb) == Status::InvalidArgument);
    assert(verb == sentinel_verb);
    assert(build_verb_12(
        0U, 0U, 0x1000U, 0U, &verb) == Status::InvalidArgument);

    RirbResponse response{};
    const uint64_t solicited =
        UINT64_C(0x00000003) << 32U | UINT64_C(0xDEADBEEF);
    assert(parse_rirb_entry(solicited, &response) == Status::Ok);
    assert(response.response == UINT32_C(0xDEADBEEF));
    assert(response.codec_address == 3U);
    assert(!response.unsolicited);

    const uint64_t unsolicited =
        UINT64_C(0x00000014) << 32U | UINT64_C(0x12345678);
    assert(parse_rirb_entry(unsolicited, &response) == Status::Ok);
    assert(response.codec_address == 4U);
    assert(response.unsolicited);

    const RirbResponse sentinel{
        UINT32_C(0x11111111), 7U, true};
    response = sentinel;
    assert(parse_rirb_entry(
        UINT64_C(0x00000020) << 32U, &response) ==
        Status::InvalidResponse);
    assert(response.response == sentinel.response);
}

void test_pcm_format() {
    uint16_t format = 0U;
    assert(build_pcm_format(48000U, 16U, 2U, &format) == Status::Ok);
    assert(format == UINT16_C(0x0011));

    assert(build_pcm_format(44100U, 16U, 2U, &format) == Status::Ok);
    assert(format == UINT16_C(0x4011));

    assert(build_pcm_format(96000U, 24U, 6U, &format) == Status::Ok);
    assert(format == UINT16_C(0x0835));

    assert(build_pcm_format(32000U, 16U, 2U, &format) == Status::Ok);
    assert(format == UINT16_C(0x0A11));

    const uint16_t sentinel = format;
    assert(build_pcm_format(12345U, 16U, 2U, &format) ==
        Status::UnsupportedPcmFormat);
    assert(format == sentinel);
    assert(build_pcm_format(48000U, 12U, 2U, &format) ==
        Status::UnsupportedPcmFormat);
    assert(build_pcm_format(48000U, 16U, 17U, &format) ==
        Status::InvalidArgument);
}

void test_bdl() {
    BufferDescriptor descriptor{};
    assert(build_buffer_descriptor(
        UINT64_C(0x12345000), 4096U, true, &descriptor) == Status::Ok);
    assert(descriptor.address == UINT64_C(0x12345000));
    assert(descriptor.length == 4096U);
    assert(descriptor.flags == 1U);

    const BufferDescriptor sentinel{
        UINT64_C(0x11111180), 128U, 0U};
    descriptor = sentinel;
    assert(build_buffer_descriptor(
        UINT64_C(0x12345040), 4096U, false, &descriptor) ==
        Status::InvalidBufferDescriptor);
    assert(descriptor.address == sentinel.address);
    assert(build_buffer_descriptor(
        UINT64_C(0x12345000), 1U, false, &descriptor) ==
        Status::InvalidBufferDescriptor);
    assert(build_buffer_descriptor(
        UINT64_C(0x12345000), 3U, false, &descriptor) ==
        Status::InvalidBufferDescriptor);
}

} // namespace

int main() {
    test_capabilities();
    test_ring_size();
    test_verb_and_response();
    test_pcm_format();
    test_bdl();
    std::puts("Intel HDA protocol foundation: PASS");
    return 0;
}
