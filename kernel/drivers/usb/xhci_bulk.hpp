#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::usb::xhci::bulk {

constexpr size_t MAXIMUM_NORMAL_TRB_TRANSFER = 131071U;
constexpr uint64_t TRB_BUFFER_BOUNDARY = UINT64_C(64) * 1024U;

enum class EndpointDirection : uint8_t {
    Out = 0,
    In,
};

struct EndpointPlan {
    uint8_t endpoint_address;
    EndpointDirection direction;
    uint8_t device_context_index;
    uint8_t endpoint_type;
    uint16_t maximum_packet_size;
};

bool build_endpoint_plan(
    uint8_t endpoint_address,
    uint16_t maximum_packet_size,
    EndpointPlan* output);

struct EndpointContextImage {
    uint32_t words[5];
};

// Builds the five xHCI endpoint-context DWORDs used by the current 32/64-byte
// context layouts. The transfer ring must be 16-byte aligned; DCS starts at 1.
bool build_endpoint_context(
    const EndpointPlan& plan,
    uint64_t transfer_ring_physical_address,
    uint16_t average_trb_length,
    EndpointContextImage* output);

bool extend_context_entries(
    uint8_t current_entries,
    uint8_t endpoint_device_context_index,
    uint8_t* output_entries);

struct TransferChunk {
    uint64_t physical_address;
    size_t length;
};

bool plan_normal_trb_chunk(
    uint64_t physical_address,
    size_t remaining_length,
    TransferChunk* output);

} // namespace drivers::usb::xhci::bulk
