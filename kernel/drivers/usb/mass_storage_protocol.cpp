#include "mass_storage_protocol.hpp"

namespace drivers::usb::mass_storage {
namespace {

uint16_t read_le16(const uint8_t* bytes) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8U));
}

uint32_t read_le32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        static_cast<uint32_t>(bytes[1]) << 8U |
        static_cast<uint32_t>(bytes[2]) << 16U |
        static_cast<uint32_t>(bytes[3]) << 24U;
}

void write_le32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
    bytes[2] = static_cast<uint8_t>(value >> 16U);
    bytes[3] = static_cast<uint8_t>(value >> 24U);
}

uint32_t read_be32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) << 24U |
        static_cast<uint32_t>(bytes[1]) << 16U |
        static_cast<uint32_t>(bytes[2]) << 8U |
        static_cast<uint32_t>(bytes[3]);
}

void write_be32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 24U);
    bytes[1] = static_cast<uint8_t>(value >> 16U);
    bytes[2] = static_cast<uint8_t>(value >> 8U);
    bytes[3] = static_cast<uint8_t>(value);
}

void write_be16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 8U);
    bytes[1] = static_cast<uint8_t>(value);
}

void copy_bytes(uint8_t* destination, const uint8_t* source, size_t count) {
    for (size_t index = 0U; index < count; ++index) {
        destination[index] = source[index];
    }
}

bool publish_cdb(
    const uint8_t* staged,
    size_t staged_length,
    uint8_t* output,
    size_t capacity) {
    if (staged == nullptr || output == nullptr || capacity < staged_length) {
        return false;
    }
    copy_bytes(output, staged, staged_length);
    return true;
}

bool endpoint_is_valid(uint8_t address, uint8_t attributes, uint16_t packet) {
    const uint8_t number = static_cast<uint8_t>(address & UINT8_C(0x0F));
    return number != 0U &&
        (address & UINT8_C(0x70)) == 0U &&
        (attributes & UINT8_C(0x03)) == UINT8_C(0x02) &&
        packet != 0U && packet <= 1024U;
}

} // namespace

bool find_bulk_only_scsi_interface(
    const uint8_t* descriptors,
    size_t length,
    BulkOnlyInterface* output) {
    if (descriptors == nullptr || output == nullptr || length < 9U ||
        descriptors[0] < 9U || descriptors[1] != 2U) {
        return false;
    }

    const uint16_t total = read_le16(descriptors + 2U);
    if (total < 9U || static_cast<size_t>(total) > length ||
        descriptors[4] == 0U || descriptors[5] == 0U) {
        return false;
    }

    const uint8_t configuration_value = descriptors[5];
    bool matching = false;
    bool invalid_candidate = false;
    bool has_in = false;
    bool has_out = false;
    BulkOnlyInterface candidate{};

    auto publish_candidate = [&]() -> bool {
        if (!matching || invalid_candidate || !has_in || !has_out) {
            return false;
        }
        *output = candidate;
        return true;
    };

    for (size_t offset = 0U; offset < static_cast<size_t>(total);) {
        if (static_cast<size_t>(total) - offset < 2U) return false;
        const uint8_t descriptor_length = descriptors[offset];
        const uint8_t descriptor_type = descriptors[offset + 1U];
        if (descriptor_length < 2U ||
            static_cast<size_t>(descriptor_length) >
                static_cast<size_t>(total) - offset) {
            return false;
        }

        if (descriptor_type == 4U) {
            if (publish_candidate()) return true;
            if (descriptor_length < 9U) return false;

            matching = descriptors[offset + 3U] == 0U &&
                descriptors[offset + 4U] == 2U &&
                descriptors[offset + 5U] == USB_CLASS_MASS_STORAGE &&
                descriptors[offset + 6U] == USB_SUBCLASS_SCSI_TRANSPARENT &&
                descriptors[offset + 7U] == USB_PROTOCOL_BULK_ONLY;
            invalid_candidate = false;
            has_in = false;
            has_out = false;
            candidate = {};
            if (matching) {
                candidate.configuration_value = configuration_value;
                candidate.interface_number = descriptors[offset + 2U];
            }
        } else if (descriptor_type == 5U && matching) {
            if (descriptor_length < 7U) return false;
            const uint8_t address = descriptors[offset + 2U];
            const uint8_t attributes = descriptors[offset + 3U];
            const uint16_t raw_packet = read_le16(descriptors + offset + 4U);
            const uint16_t packet = static_cast<uint16_t>(
                raw_packet & UINT16_C(0x07FF));
            if (!endpoint_is_valid(address, attributes, packet)) {
                invalid_candidate = true;
            } else if ((address & UINT8_C(0x80)) != 0U) {
                if (has_in) {
                    invalid_candidate = true;
                } else {
                    has_in = true;
                    candidate.bulk_in_endpoint = address;
                    candidate.bulk_in_maximum_packet_size = packet;
                }
            } else {
                if (has_out) {
                    invalid_candidate = true;
                } else {
                    has_out = true;
                    candidate.bulk_out_endpoint = address;
                    candidate.bulk_out_maximum_packet_size = packet;
                }
            }
        }

        offset += static_cast<size_t>(descriptor_length);
    }

    return publish_candidate();
}

bool encode_command_block_wrapper(
    const CommandBlockWrapper* wrapper,
    uint8_t* output,
    size_t output_capacity) {
    if (wrapper == nullptr || output == nullptr ||
        output_capacity < COMMAND_BLOCK_WRAPPER_SIZE ||
        wrapper->logical_unit_number > 15U ||
        wrapper->command_length == 0U ||
        wrapper->command_length > MAXIMUM_COMMAND_BLOCK_SIZE) {
        return false;
    }

    if ((wrapper->data_transfer_length == 0U &&
         wrapper->direction != DataDirection::None) ||
        (wrapper->data_transfer_length != 0U &&
         wrapper->direction == DataDirection::None)) {
        return false;
    }
    if (wrapper->direction != DataDirection::None &&
        wrapper->direction != DataDirection::Out &&
        wrapper->direction != DataDirection::In) {
        return false;
    }

    uint8_t staged[COMMAND_BLOCK_WRAPPER_SIZE]{};
    write_le32(staged, COMMAND_BLOCK_WRAPPER_SIGNATURE);
    write_le32(staged + 4U, wrapper->tag);
    write_le32(staged + 8U, wrapper->data_transfer_length);
    staged[12U] = wrapper->direction == DataDirection::In
        ? UINT8_C(0x80) : UINT8_C(0x00);
    staged[13U] = wrapper->logical_unit_number;
    staged[14U] = wrapper->command_length;
    for (size_t index = 0U; index < wrapper->command_length; ++index) {
        staged[15U + index] = wrapper->command[index];
    }
    copy_bytes(output, staged, sizeof(staged));
    return true;
}

bool decode_command_status_wrapper(
    const uint8_t* bytes,
    size_t length,
    uint32_t expected_tag,
    uint32_t expected_transfer_length,
    CommandStatusWrapper* output) {
    if (bytes == nullptr || output == nullptr ||
        length != COMMAND_STATUS_WRAPPER_SIZE ||
        read_le32(bytes) != COMMAND_STATUS_WRAPPER_SIGNATURE ||
        read_le32(bytes + 4U) != expected_tag) {
        return false;
    }

    const uint32_t residue = read_le32(bytes + 8U);
    const uint8_t raw_status = bytes[12U];
    if (raw_status > static_cast<uint8_t>(CommandStatus::PhaseError)) {
        return false;
    }
    if (raw_status != static_cast<uint8_t>(CommandStatus::PhaseError) &&
        residue > expected_transfer_length) {
        return false;
    }

    const CommandStatusWrapper staged{
        expected_tag,
        residue,
        static_cast<CommandStatus>(raw_status),
    };
    *output = staged;
    return true;
}

namespace scsi {

bool build_test_unit_ready(uint8_t* output, size_t capacity) {
    uint8_t staged[CDB6_SIZE]{};
    staged[0] = UINT8_C(0x00);
    return publish_cdb(staged, sizeof(staged), output, capacity);
}

bool build_inquiry(
    uint8_t allocation_length,
    uint8_t* output,
    size_t capacity) {
    if (allocation_length == 0U) return false;
    uint8_t staged[CDB6_SIZE]{};
    staged[0] = UINT8_C(0x12);
    staged[4] = allocation_length;
    return publish_cdb(staged, sizeof(staged), output, capacity);
}

bool build_request_sense(
    uint8_t allocation_length,
    uint8_t* output,
    size_t capacity) {
    if (allocation_length == 0U) return false;
    uint8_t staged[CDB6_SIZE]{};
    staged[0] = UINT8_C(0x03);
    staged[4] = allocation_length;
    return publish_cdb(staged, sizeof(staged), output, capacity);
}

bool build_read_capacity10(uint8_t* output, size_t capacity) {
    uint8_t staged[CDB10_SIZE]{};
    staged[0] = UINT8_C(0x25);
    return publish_cdb(staged, sizeof(staged), output, capacity);
}

namespace {

bool build_read_write10(
    uint8_t opcode,
    uint32_t logical_block_address,
    uint16_t block_count,
    uint8_t* output,
    size_t capacity) {
    if (block_count == 0U) return false;
    uint8_t staged[CDB10_SIZE]{};
    staged[0] = opcode;
    write_be32(staged + 2U, logical_block_address);
    write_be16(staged + 7U, block_count);
    return publish_cdb(staged, sizeof(staged), output, capacity);
}

} // namespace

bool build_read10(
    uint32_t logical_block_address,
    uint16_t block_count,
    uint8_t* output,
    size_t capacity) {
    return build_read_write10(
        UINT8_C(0x28),
        logical_block_address,
        block_count,
        output,
        capacity);
}

bool build_write10(
    uint32_t logical_block_address,
    uint16_t block_count,
    uint8_t* output,
    size_t capacity) {
    return build_read_write10(
        UINT8_C(0x2A),
        logical_block_address,
        block_count,
        output,
        capacity);
}

bool build_synchronize_cache10(uint8_t* output, size_t capacity) {
    uint8_t staged[CDB10_SIZE]{};
    staged[0] = UINT8_C(0x35);
    return publish_cdb(staged, sizeof(staged), output, capacity);
}

bool parse_read_capacity10(
    const uint8_t* bytes,
    size_t length,
    ReadCapacity10Data* output) {
    if (bytes == nullptr || output == nullptr || length < 8U) {
        return false;
    }
    const uint32_t last_lba = read_be32(bytes);
    const uint32_t block_size = read_be32(bytes + 4U);
    if (last_lba == UINT32_MAX || block_size == 0U) return false;

    const ReadCapacity10Data staged{
        last_lba,
        block_size,
        static_cast<uint64_t>(last_lba) + UINT64_C(1),
    };
    *output = staged;
    return true;
}

} // namespace scsi

} // namespace drivers::usb::mass_storage
