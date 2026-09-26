#pragma once

#include <stddef.h>
#include <stdint.h>

namespace storage::nvme::protocol {

constexpr size_t COMMAND_DWORDS = 16U;
constexpr size_t COMPLETION_DWORDS = 4U;
constexpr size_t IDENTIFY_BYTES = 4096U;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    UnsupportedCapabilities,
    InvalidPageSize,
    InvalidQueueSize,
    InvalidNamespace,
    UnsupportedLbaFormat,
    InvalidTransfer,
    InvalidCompletion,
};

struct Capabilities {
    uint32_t maximum_queue_entries;
    bool contiguous_queues_required;
    uint8_t timeout_500ms_units;
    uint32_t doorbell_stride_bytes;
    bool nvm_command_set_supported;
    uint32_t minimum_page_size;
    uint32_t maximum_page_size;
};

Status decode_capabilities(uint64_t raw, Capabilities* output);

Status build_controller_configuration(
    const Capabilities& capabilities,
    uint32_t page_size,
    uint32_t* output_cc);

Status build_admin_queue_attributes(
    const Capabilities& capabilities,
    uint16_t queue_entries,
    uint32_t* output_aqa);

struct Command {
    uint32_t dwords[COMMAND_DWORDS];
};

Status build_identify_controller(
    uint16_t command_id,
    uint64_t prp1,
    Command* output);

Status build_identify_namespace(
    uint16_t command_id,
    uint32_t namespace_id,
    uint64_t prp1,
    Command* output);

Status build_flush(
    uint16_t command_id,
    uint32_t namespace_id,
    Command* output);

struct NamespaceInfo {
    uint64_t size_blocks;
    uint64_t capacity_blocks;
    uint32_t block_size;
    uint8_t lba_format_index;
};

Status parse_identify_namespace(
    const uint8_t* bytes,
    size_t length,
    NamespaceInfo* output);

struct Completion {
    uint32_t result;
    uint16_t submission_queue_head;
    uint16_t submission_queue_id;
    uint16_t command_id;
    uint8_t status_code;
    uint8_t status_code_type;
    bool phase;
    bool do_not_retry;
    bool success;
};

Status parse_completion(
    const uint32_t dwords[COMPLETION_DWORDS],
    uint16_t expected_command_id,
    bool expected_phase,
    Completion* output);

const char* status_message(Status status);

} // namespace storage::nvme::protocol
