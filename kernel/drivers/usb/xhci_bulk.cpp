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

} // namespace drivers::usb::xhci::bulk
