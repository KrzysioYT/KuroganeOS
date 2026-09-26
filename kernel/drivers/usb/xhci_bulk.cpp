#include "xhci_bulk.hpp"

namespace drivers::usb::xhci::bulk {

bool build_endpoint_plan(
    uint8_t endpoint_address,
    uint16_t maximum_packet_size,
    EndpointPlan* output) {
    if (output == nullptr || maximum_packet_size == 0U ||
        maximum_packet_size > 1024U) {
        return false;
    }

    const uint8_t endpoint_number =
        static_cast<uint8_t>(endpoint_address & UINT8_C(0x0F));
    if (endpoint_number == 0U ||
        (endpoint_address & UINT8_C(0x70)) != 0U) {
        return false;
    }

    const bool in = (endpoint_address & UINT8_C(0x80)) != 0U;
    const uint8_t dci = static_cast<uint8_t>(
        endpoint_number * 2U + (in ? 1U : 0U));

    EndpointPlan staged{};
    staged.endpoint_address = endpoint_address;
    staged.direction = in ? EndpointDirection::In : EndpointDirection::Out;
    staged.device_context_index = dci;
    staged.endpoint_type = in ? 6U : 2U;
    staged.maximum_packet_size = maximum_packet_size;
    *output = staged;
    return true;
}

bool build_endpoint_context(
    const EndpointPlan& plan,
    uint64_t transfer_ring_physical_address,
    uint16_t average_trb_length,
    EndpointContextImage* output) {
    if (output == nullptr ||
        plan.device_context_index < 2U ||
        plan.device_context_index >= 32U ||
        plan.maximum_packet_size == 0U ||
        plan.maximum_packet_size > 1024U ||
        average_trb_length == 0U ||
        transfer_ring_physical_address == 0U ||
        (transfer_ring_physical_address & UINT64_C(0x0F)) != 0U) {
        return false;
    }

    const bool in = plan.direction == EndpointDirection::In;
    const uint8_t expected_type = in ? 6U : 2U;
    if (plan.endpoint_type != expected_type) return false;

    EndpointContextImage staged{};
    // CErr=3 for bulk endpoints. EP Type is 2 (Bulk OUT) or 6 (Bulk IN).
    staged.words[1] =
        (UINT32_C(3) << 1U) |
        (static_cast<uint32_t>(plan.endpoint_type) << 3U) |
        (static_cast<uint32_t>(plan.maximum_packet_size) << 16U);
    staged.words[2] =
        static_cast<uint32_t>(transfer_ring_physical_address) | UINT32_C(1);
    staged.words[3] =
        static_cast<uint32_t>(transfer_ring_physical_address >> 32U);
    staged.words[4] = static_cast<uint32_t>(average_trb_length);
    *output = staged;
    return true;
}

bool extend_context_entries(
    uint8_t current_entries,
    uint8_t endpoint_device_context_index,
    uint8_t* output_entries) {
    if (output_entries == nullptr ||
        current_entries > 31U ||
        endpoint_device_context_index < 2U ||
        endpoint_device_context_index > 31U) {
        return false;
    }
    *output_entries = current_entries > endpoint_device_context_index
        ? current_entries : endpoint_device_context_index;
    return true;
}

bool plan_normal_trb_chunk(
    uint64_t physical_address,
    size_t remaining_length,
    TransferChunk* output) {
    if (output == nullptr || remaining_length == 0U) return false;

    const uint64_t boundary_offset =
        physical_address & (TRB_BUFFER_BOUNDARY - UINT64_C(1));
    const size_t until_boundary = static_cast<size_t>(
        TRB_BUFFER_BOUNDARY - boundary_offset);

    size_t length = remaining_length;
    if (length > MAXIMUM_NORMAL_TRB_TRANSFER) {
        length = MAXIMUM_NORMAL_TRB_TRANSFER;
    }
    if (length > until_boundary) {
        length = until_boundary;
    }
    if (length == 0U) return false;

    const TransferChunk staged{physical_address, length};
    *output = staged;
    return true;
}


TransferCompletionStatus classify_transfer_completion(
    uint64_t expected_trb_physical_address,
    uint8_t expected_slot_id,
    uint8_t expected_device_context_index,
    size_t requested_length,
    uint64_t event_parameter,
    uint32_t event_status,
    uint32_t event_control,
    TransferCompletion* output) {
    if (output == nullptr ||
        expected_trb_physical_address == 0U ||
        (expected_trb_physical_address & UINT64_C(0x0F)) != 0U ||
        expected_slot_id == 0U ||
        expected_device_context_index < 2U ||
        expected_device_context_index > 31U ||
        requested_length == 0U ||
        requested_length > MAXIMUM_NORMAL_TRB_TRANSFER) {
        return TransferCompletionStatus::InvalidEvent;
    }

    const bool event_data = (event_control & (UINT32_C(1) << 2U)) != 0U;
    const uint8_t slot_id = static_cast<uint8_t>(event_control >> 24U);
    const uint8_t endpoint_id = static_cast<uint8_t>(
        (event_control >> 16U) & UINT32_C(0x1F));
    if (event_data ||
        event_parameter != expected_trb_physical_address ||
        (event_parameter & UINT64_C(0x0F)) != 0U ||
        slot_id != expected_slot_id ||
        endpoint_id != expected_device_context_index) {
        return TransferCompletionStatus::ForeignEvent;
    }

    const uint8_t completion_code = static_cast<uint8_t>(event_status >> 24U);
    const size_t residual = static_cast<size_t>(
        event_status & UINT32_C(0x00FFFFFF));
    if (residual > requested_length) {
        return TransferCompletionStatus::InvalidEvent;
    }

    constexpr uint8_t completion_success = 1U;
    constexpr uint8_t completion_short_packet = 13U;
    if (completion_code == completion_success) {
        if (residual != 0U) return TransferCompletionStatus::InvalidEvent;
        const TransferCompletion staged{requested_length, completion_code};
        *output = staged;
        return TransferCompletionStatus::Complete;
    }
    if (completion_code == completion_short_packet) {
        const TransferCompletion staged{
            requested_length - residual,
            completion_code,
        };
        *output = staged;
        return TransferCompletionStatus::ShortPacket;
    }
    return TransferCompletionStatus::TransferFailed;
}

} // namespace drivers::usb::xhci::bulk
