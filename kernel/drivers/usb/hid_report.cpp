#include "hid_report.hpp"

#include <limits.h>

namespace drivers::usb::hid {
namespace {

constexpr size_t MAXIMUM_LOCAL_USAGES = 16U;

uint32_t read_unsigned(const uint8_t* bytes, size_t size) {
    uint32_t value = 0U;
    for (size_t index = 0U; index < size; ++index) {
        value |= static_cast<uint32_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

int32_t read_signed(const uint8_t* bytes, size_t size) {
    const uint32_t value = read_unsigned(bytes, size);
    if (size == 0U) return 0;
    const uint32_t bits = static_cast<uint32_t>(size * 8U);
    if (bits >= 32U) return static_cast<int32_t>(value);
    const uint32_t sign = UINT32_C(1) << (bits - 1U);
    if ((value & sign) == 0U) return static_cast<int32_t>(value);
    return static_cast<int32_t>(value | (~UINT32_C(0) << bits));
}

uint32_t extract_bits(
    const uint8_t* bytes,
    size_t length,
    uint32_t bit_offset,
    uint8_t bit_size,
    bool* ok) {
    if (ok == nullptr || bytes == nullptr || bit_size == 0U ||
        bit_size > 32U ||
        static_cast<uint64_t>(bit_offset) + bit_size >
            static_cast<uint64_t>(length) * 8U) {
        if (ok != nullptr) *ok = false;
        return 0U;
    }
    uint32_t value = 0U;
    for (uint8_t bit = 0U; bit < bit_size; ++bit) {
        const uint32_t absolute = bit_offset + bit;
        const uint8_t byte = bytes[absolute / 8U];
        if ((byte & static_cast<uint8_t>(1U << (absolute % 8U))) != 0U) {
            value |= UINT32_C(1) << bit;
        }
    }
    *ok = true;
    return value;
}

int32_t field_value(
    const Field& field,
    const uint8_t* report,
    size_t report_length,
    uint32_t prefix_bits,
    bool* ok) {
    const uint32_t raw = extract_bits(
        report, report_length, prefix_bits + field.bit_offset,
        field.bit_size, ok);
    if (ok == nullptr || !*ok) return 0;
    if (field.logical_minimum < 0 && field.bit_size < 32U) {
        const uint32_t sign = UINT32_C(1) << (field.bit_size - 1U);
        if ((raw & sign) != 0U) {
            const uint32_t extended =
                raw | (~UINT32_C(0) << field.bit_size);
            return static_cast<int32_t>(extended);
        }
    }
    return static_cast<int32_t>(raw);
}

uint16_t usage_for_index(
    const uint16_t* usages,
    size_t usage_count,
    bool has_usage_range,
    uint16_t usage_minimum,
    uint16_t usage_maximum,
    uint32_t index) {
    if (index < usage_count) return usages[index];
    if (has_usage_range) {
        const uint32_t candidate =
            static_cast<uint32_t>(usage_minimum) + index;
        if (candidate <= usage_maximum) {
            return static_cast<uint16_t>(candidate);
        }
    }
    return 0U;
}

bool record_axis(
    Field* field,
    uint32_t bit_offset,
    uint8_t bit_size,
    int32_t logical_minimum,
    int32_t logical_maximum,
    bool relative) {
    if (field == nullptr || field->present || bit_offset > UINT16_MAX ||
        bit_size == 0U || bit_size > 32U ||
        logical_maximum <= logical_minimum) {
        return false;
    }
    *field = {
        static_cast<uint16_t>(bit_offset),
        bit_size,
        logical_minimum,
        logical_maximum,
        relative,
        true,
    };
    return true;
}

} // namespace

bool parse_pointer_report_descriptor(
    const uint8_t* descriptor,
    size_t length,
    PointerReportLayout* output) {
    if (descriptor == nullptr || output == nullptr || length == 0U ||
        length > 4096U) {
        return false;
    }

    PointerReportLayout layout{};
    uint16_t usage_page = 0U;
    int32_t logical_minimum = 0;
    int32_t logical_maximum = 0;
    uint32_t report_size = 0U;
    uint32_t report_count = 0U;
    uint32_t bit_offset = 0U;
    uint8_t active_report_id = 0U;
    bool report_id_selected = false;
    bool unsupported_multiple_reports = false;
    bool saw_pointer_collection = false;

    uint16_t usages[MAXIMUM_LOCAL_USAGES]{};
    size_t usage_count = 0U;
    uint16_t usage_minimum = 0U;
    uint16_t usage_maximum = 0U;
    bool has_usage_range = false;

    auto clear_local = [&]() {
        usage_count = 0U;
        usage_minimum = 0U;
        usage_maximum = 0U;
        has_usage_range = false;
    };

    for (size_t offset = 0U; offset < length;) {
        const uint8_t prefix = descriptor[offset++];
        if (prefix == 0xFEU) {
            if (offset + 2U > length) return false;
            const size_t data_size = descriptor[offset];
            offset += 2U;
            if (data_size > length - offset) return false;
            offset += data_size;
            continue;
        }

        const uint8_t size_code = static_cast<uint8_t>(prefix & 0x03U);
        const size_t data_size = size_code == 3U ? 4U : size_code;
        const uint8_t type = static_cast<uint8_t>((prefix >> 2U) & 0x03U);
        const uint8_t tag = static_cast<uint8_t>((prefix >> 4U) & 0x0FU);
        if (data_size > length - offset) return false;
        const uint8_t* data = descriptor + offset;
        offset += data_size;

        if (type == 1U) {
            switch (tag) {
                case 0U:
                    usage_page = static_cast<uint16_t>(
                        read_unsigned(data, data_size) & UINT16_MAX);
                    break;
                case 1U:
                    logical_minimum = read_signed(data, data_size);
                    break;
                case 2U:
                    logical_maximum = logical_minimum < 0
                        ? read_signed(data, data_size)
                        : static_cast<int32_t>(read_unsigned(data, data_size));
                    break;
                case 7U:
                    report_size = read_unsigned(data, data_size);
                    break;
                case 8U: {
                    const uint32_t value = read_unsigned(data, data_size);
                    if (value == 0U || value > UINT8_MAX) return false;
                    const uint8_t next_id = static_cast<uint8_t>(value);
                    if (!report_id_selected) {
                        active_report_id = next_id;
                        layout.report_id = next_id;
                        layout.has_report_id = true;
                        report_id_selected = true;
                        bit_offset = 0U;
                    } else if (next_id != active_report_id) {
                        unsupported_multiple_reports = true;
                    }
                    break;
                }
                case 9U:
                    report_count = read_unsigned(data, data_size);
                    break;
                default:
                    break;
            }
            continue;
        }

        if (type == 2U) {
            switch (tag) {
                case 0U:
                    if (usage_count < MAXIMUM_LOCAL_USAGES) {
                        usages[usage_count++] = static_cast<uint16_t>(
                            read_unsigned(data, data_size) & UINT16_MAX);
                    }
                    break;
                case 1U:
                    usage_minimum = static_cast<uint16_t>(
                        read_unsigned(data, data_size) & UINT16_MAX);
                    has_usage_range = true;
                    break;
                case 2U:
                    usage_maximum = static_cast<uint16_t>(
                        read_unsigned(data, data_size) & UINT16_MAX);
                    has_usage_range = true;
                    break;
                default:
                    break;
            }
            continue;
        }

        if (type != 0U) continue;

        if (tag == 10U) {
            if (usage_page == 0x01U && usage_count != 0U &&
                (usages[0U] == 0x01U || usages[0U] == 0x02U)) {
                saw_pointer_collection = true;
            }
            clear_local();
            continue;
        }
        if (tag == 12U) {
            clear_local();
            continue;
        }
        if (tag != 8U) {
            clear_local();
            continue;
        }

        if (report_size == 0U || report_size > 32U ||
            report_count == 0U || report_count > 64U) {
            return false;
        }
        const uint64_t bits_added =
            static_cast<uint64_t>(report_size) * report_count;
        if (bits_added > UINT16_MAX ||
            static_cast<uint64_t>(bit_offset) + bits_added > UINT16_MAX) {
            return false;
        }

        const uint32_t flags = read_unsigned(data, data_size);
        const bool constant = (flags & UINT32_C(1)) != 0U;
        const bool variable = (flags & UINT32_C(2)) != 0U;
        const bool relative = (flags & UINT32_C(4)) != 0U;

        if (!unsupported_multiple_reports && !constant && variable) {
            if (usage_page == 0x09U && report_size == 1U &&
                layout.button_count == 0U && report_count <= 8U) {
                layout.buttons_bit_offset =
                    static_cast<uint16_t>(bit_offset);
                layout.button_count = static_cast<uint8_t>(report_count);
            } else if (usage_page == 0x01U) {
                for (uint32_t field_index = 0U;
                     field_index < report_count; ++field_index) {
                    const uint16_t usage = usage_for_index(
                        usages, usage_count, has_usage_range,
                        usage_minimum, usage_maximum, field_index);
                    const uint32_t field_offset =
                        bit_offset + field_index * report_size;
                    if (usage == 0x30U) {
                        static_cast<void>(record_axis(
                            &layout.x, field_offset,
                            static_cast<uint8_t>(report_size),
                            logical_minimum, logical_maximum, relative));
                    } else if (usage == 0x31U) {
                        static_cast<void>(record_axis(
                            &layout.y, field_offset,
                            static_cast<uint8_t>(report_size),
                            logical_minimum, logical_maximum, relative));
                    } else if (usage == 0x38U) {
                        static_cast<void>(record_axis(
                            &layout.wheel, field_offset,
                            static_cast<uint8_t>(report_size),
                            logical_minimum, logical_maximum, relative));
                    }
                }
            }
        }

        bit_offset += static_cast<uint32_t>(bits_added);
        clear_local();
    }

    if (unsupported_multiple_reports || !saw_pointer_collection ||
        !layout.x.present || !layout.y.present ||
        layout.x.relative != layout.y.relative) {
        return false;
    }

    const uint32_t payload_bytes = (bit_offset + 7U) / 8U;
    const uint32_t total_bytes =
        payload_bytes + (layout.has_report_id ? 1U : 0U);
    if (total_bytes == 0U || total_bytes > UINT16_MAX) return false;
    layout.report_bytes = static_cast<uint16_t>(total_bytes);
    layout.valid = true;
    *output = layout;
    return true;
}

void reset_pointer_decoder(PointerDecoder* decoder) {
    if (decoder != nullptr) *decoder = {};
}

bool decode_pointer_report(
    const PointerReportLayout& layout,
    PointerDecoder* decoder,
    const uint8_t* report,
    size_t report_length,
    PointerReport* output) {
    if (!layout.valid || decoder == nullptr || report == nullptr ||
        output == nullptr || report_length < layout.report_bytes) {
        return false;
    }
    uint32_t prefix_bits = 0U;
    if (layout.has_report_id) {
        if (report[0U] != layout.report_id) return false;
        prefix_bits = 8U;
    }

    bool ok = false;
    const int32_t x = field_value(
        layout.x, report, report_length, prefix_bits, &ok);
    if (!ok) return false;
    const int32_t y = field_value(
        layout.y, report, report_length, prefix_bits, &ok);
    if (!ok) return false;

    int8_t wheel = 0;
    if (layout.wheel.present) {
        const int32_t value = field_value(
            layout.wheel, report, report_length, prefix_bits, &ok);
        if (!ok) return false;
        wheel = static_cast<int8_t>(
            value < INT8_MIN ? INT8_MIN :
            value > INT8_MAX ? INT8_MAX : value);
    }

    uint8_t buttons = 0U;
    for (uint8_t index = 0U; index < layout.button_count; ++index) {
        const uint32_t value = extract_bits(
            report, report_length,
            prefix_bits + layout.buttons_bit_offset + index,
            1U, &ok);
        if (!ok) return false;
        if (value != 0U) buttons |= static_cast<uint8_t>(1U << index);
    }

    *output = {
        !layout.x.relative,
        x,
        y,
        layout.x.logical_minimum,
        layout.x.logical_maximum,
        layout.y.logical_minimum,
        layout.y.logical_maximum,
        wheel,
        buttons,
        static_cast<uint8_t>(buttons ^ decoder->previous_buttons),
    };
    decoder->previous_buttons = buttons;
    return true;
}

} // namespace drivers::usb::hid
