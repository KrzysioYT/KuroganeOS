#include "hda_protocol.hpp"

namespace drivers::audio::hda::protocol {
namespace {

bool ring_supported(uint8_t capabilities, uint8_t bit) {
    return (capabilities & static_cast<uint8_t>(UINT8_C(1) << bit)) != 0U;
}

bool pcm_bits_code(uint8_t bits, uint8_t* output) {
    if (output == nullptr) return false;
    switch (bits) {
        case 8U: *output = 0U; return true;
        case 16U: *output = 1U; return true;
        case 20U: *output = 2U; return true;
        case 24U: *output = 3U; return true;
        case 32U: *output = 4U; return true;
    }
    return false;
}

bool find_rate_fields(
    uint32_t sample_rate_hz,
    uint8_t* base,
    uint8_t* multiplier,
    uint8_t* divisor) {
    if (base == nullptr || multiplier == nullptr || divisor == nullptr ||
        sample_rate_hz == 0U) {
        return false;
    }

    const uint32_t bases[2] = {48000U, 44100U};
    for (uint8_t base_index = 0U; base_index < 2U; ++base_index) {
        for (uint8_t mult = 1U; mult <= 4U; ++mult) {
            for (uint8_t div = 1U; div <= 8U; ++div) {
                const uint64_t numerator =
                    static_cast<uint64_t>(bases[base_index]) * mult;
                if (numerator % div == 0U &&
                    numerator / div == sample_rate_hz) {
                    *base = base_index;
                    *multiplier = static_cast<uint8_t>(mult - 1U);
                    *divisor = static_cast<uint8_t>(div - 1U);
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

Status decode_global_capabilities(
    uint16_t raw_gcap,
    ControllerCapabilities* output) {
    if (output == nullptr) return Status::InvalidArgument;

    const uint8_t output_streams =
        static_cast<uint8_t>((raw_gcap >> 12U) & UINT16_C(0x0F));
    const uint8_t input_streams =
        static_cast<uint8_t>((raw_gcap >> 8U) & UINT16_C(0x0F));
    const uint8_t bidirectional =
        static_cast<uint8_t>((raw_gcap >> 3U) & UINT16_C(0x1F));
    const uint8_t nsdo =
        static_cast<uint8_t>((raw_gcap >> 1U) & UINT16_C(0x03));

    // NSDO=3 and BSS=31 are reserved encodings in the base HDA controller
    // contract and must not be published as usable capabilities.
    if (nsdo == 3U || bidirectional == 31U) {
        return Status::UnsupportedCapabilities;
    }

    const ControllerCapabilities staged{
        output_streams,
        input_streams,
        bidirectional,
        static_cast<uint8_t>(UINT8_C(1) << nsdo),
        (raw_gcap & UINT16_C(1)) != 0U,
    };
    *output = staged;
    return Status::Ok;
}

Status select_ring_size(
    uint8_t size_register,
    uint16_t maximum_entries,
    RingSize* output) {
    if (output == nullptr || maximum_entries < 2U) {
        return Status::InvalidArgument;
    }

    const uint8_t capabilities =
        static_cast<uint8_t>((size_register >> 4U) & UINT8_C(0x07));
    if (capabilities == 0U) return Status::UnsupportedRingSize;

    RingSize staged{};
    if (maximum_entries >= 256U && ring_supported(capabilities, 2U)) {
        staged = {256U, 2U};
    } else if (maximum_entries >= 16U && ring_supported(capabilities, 1U)) {
        staged = {16U, 1U};
    } else if (ring_supported(capabilities, 0U)) {
        staged = {2U, 0U};
    } else {
        return Status::UnsupportedRingSize;
    }

    *output = staged;
    return Status::Ok;
}

Status build_verb_12(
    uint8_t codec_address,
    uint8_t node_id,
    uint16_t verb,
    uint8_t payload,
    uint32_t* output) {
    if (output == nullptr || codec_address > 15U || verb > 0x0FFFU) {
        return Status::InvalidArgument;
    }

    const uint32_t staged =
        static_cast<uint32_t>(codec_address) << 28U |
        static_cast<uint32_t>(node_id) << 20U |
        static_cast<uint32_t>(verb) << 8U |
        static_cast<uint32_t>(payload);
    *output = staged;
    return Status::Ok;
}

Status parse_rirb_entry(
    uint64_t raw_entry,
    RirbResponse* output) {
    if (output == nullptr) return Status::InvalidArgument;

    const uint32_t extended = static_cast<uint32_t>(raw_entry >> 32U);
    if ((extended & ~UINT32_C(0x1F)) != 0U) {
        return Status::InvalidResponse;
    }

    const RirbResponse staged{
        static_cast<uint32_t>(raw_entry),
        static_cast<uint8_t>(extended & UINT32_C(0x0F)),
        (extended & UINT32_C(0x10)) != 0U,
    };
    *output = staged;
    return Status::Ok;
}

Status build_pcm_format(
    uint32_t sample_rate_hz,
    uint8_t bits_per_sample,
    uint8_t channels,
    uint16_t* output_format) {
    if (output_format == nullptr || channels == 0U || channels > 16U) {
        return Status::InvalidArgument;
    }

    uint8_t bits_code = 0U;
    uint8_t base = 0U;
    uint8_t multiplier = 0U;
    uint8_t divisor = 0U;
    if (!pcm_bits_code(bits_per_sample, &bits_code) ||
        !find_rate_fields(
            sample_rate_hz, &base, &multiplier, &divisor)) {
        return Status::UnsupportedPcmFormat;
    }

    const uint16_t staged =
        static_cast<uint16_t>(base) << 14U |
        static_cast<uint16_t>(multiplier) << 11U |
        static_cast<uint16_t>(divisor) << 8U |
        static_cast<uint16_t>(bits_code) << 4U |
        static_cast<uint16_t>(channels - 1U);
    *output_format = staged;
    return Status::Ok;
}

Status build_buffer_descriptor(
    uint64_t physical_address,
    uint32_t length,
    bool interrupt_on_completion,
    BufferDescriptor* output) {
    if (output == nullptr ||
        physical_address == 0U ||
        (physical_address & UINT64_C(0x7F)) != 0U ||
        length < 2U ||
        (length & 1U) != 0U) {
        return Status::InvalidBufferDescriptor;
    }

    const BufferDescriptor staged{
        physical_address,
        length,
        interrupt_on_completion ? UINT32_C(1) : UINT32_C(0),
    };
    *output = staged;
    return Status::Ok;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidArgument: return "invalid HDA protocol argument";
        case Status::UnsupportedCapabilities:
            return "unsupported HDA controller capabilities";
        case Status::UnsupportedRingSize:
            return "unsupported HDA CORB/RIRB size";
        case Status::UnsupportedPcmFormat:
            return "unsupported HDA PCM format";
        case Status::InvalidBufferDescriptor:
            return "invalid HDA buffer descriptor";
        case Status::InvalidResponse:
            return "invalid HDA RIRB response";
    }
    return "unknown HDA protocol status";
}

} // namespace drivers::audio::hda::protocol
