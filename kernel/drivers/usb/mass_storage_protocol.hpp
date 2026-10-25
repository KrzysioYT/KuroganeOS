#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::usb::mass_storage {

constexpr uint8_t USB_CLASS_MASS_STORAGE = UINT8_C(0x08);
constexpr uint8_t USB_SUBCLASS_SCSI_TRANSPARENT = UINT8_C(0x06);
constexpr uint8_t USB_PROTOCOL_BULK_ONLY = UINT8_C(0x50);

constexpr uint8_t REQUEST_GET_MAX_LUN = UINT8_C(0xFE);
constexpr uint8_t REQUEST_BULK_ONLY_RESET = UINT8_C(0xFF);
constexpr uint8_t REQUEST_TYPE_GET_MAX_LUN = UINT8_C(0xA1);
constexpr uint8_t REQUEST_TYPE_BULK_ONLY_RESET = UINT8_C(0x21);

struct BulkOnlyInterface {
    uint8_t configuration_value;
    uint8_t interface_number;
    uint8_t bulk_in_endpoint;
    uint16_t bulk_in_maximum_packet_size;
    uint8_t bulk_out_endpoint;
    uint16_t bulk_out_maximum_packet_size;
};

// Finds one alternate-setting-zero Mass Storage / SCSI transparent / BOT
// interface with exactly one valid bulk-IN and one valid bulk-OUT endpoint.
// Transactional: output is unchanged when the descriptor stream is rejected.
bool find_bulk_only_scsi_interface(
    const uint8_t* descriptors,
    size_t length,
    BulkOnlyInterface* output);

constexpr size_t COMMAND_BLOCK_WRAPPER_SIZE = 31U;
constexpr size_t COMMAND_STATUS_WRAPPER_SIZE = 13U;
constexpr size_t MAXIMUM_COMMAND_BLOCK_SIZE = 16U;
constexpr uint32_t COMMAND_BLOCK_WRAPPER_SIGNATURE = UINT32_C(0x43425355);
constexpr uint32_t COMMAND_STATUS_WRAPPER_SIGNATURE = UINT32_C(0x53425355);

enum class DataDirection : uint8_t {
    None = 0,
    Out,
    In,
};

struct CommandBlockWrapper {
    uint32_t tag;
    uint32_t data_transfer_length;
    DataDirection direction;
    uint8_t logical_unit_number;
    uint8_t command_length;
    uint8_t command[MAXIMUM_COMMAND_BLOCK_SIZE];
};

// Encodes the 31-byte USB MSC Bulk-Only CBW without relying on packed structs.
// Zero-length transfers use DataDirection::None; data transfers require In/Out.
// Transactional: output is unchanged on validation failure.
bool encode_command_block_wrapper(
    const CommandBlockWrapper* wrapper,
    uint8_t* output,
    size_t output_capacity);

enum class CommandStatus : uint8_t {
    Passed = 0,
    Failed = 1,
    PhaseError = 2,
};

struct CommandStatusWrapper {
    uint32_t tag;
    uint32_t data_residue;
    CommandStatus status;
};

// Validates signature, tag, status and BOT residue rules for the exact 13-byte
// CSW. Transactional: output is unchanged on failure.
bool decode_command_status_wrapper(
    const uint8_t* bytes,
    size_t length,
    uint32_t expected_tag,
    uint32_t expected_transfer_length,
    CommandStatusWrapper* output);

namespace scsi {

constexpr size_t CDB6_SIZE = 6U;
constexpr size_t CDB10_SIZE = 10U;

bool build_test_unit_ready(uint8_t* output, size_t capacity);
bool build_inquiry(
    uint8_t allocation_length,
    uint8_t* output,
    size_t capacity);
bool build_request_sense(
    uint8_t allocation_length,
    uint8_t* output,
    size_t capacity);
bool build_read_capacity10(uint8_t* output, size_t capacity);
bool build_read10(
    uint32_t logical_block_address,
    uint16_t block_count,
    uint8_t* output,
    size_t capacity);
bool build_write10(
    uint32_t logical_block_address,
    uint16_t block_count,
    uint8_t* output,
    size_t capacity);
bool build_synchronize_cache10(uint8_t* output, size_t capacity);

struct ReadCapacity10Data {
    uint32_t last_logical_block_address;
    uint32_t block_size;
    uint64_t block_count;
};

// READ CAPACITY (10) returns an 8-byte big-endian payload. A last-LBA value of
// 0xFFFFFFFF requires READ CAPACITY (16), which this bounded foundation does
// not yet claim, so it is rejected rather than exposing truncated geometry.
bool parse_read_capacity10(
    const uint8_t* bytes,
    size_t length,
    ReadCapacity10Data* output);

} // namespace scsi

} // namespace drivers::usb::mass_storage
