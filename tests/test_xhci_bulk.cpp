#include <cassert>
#include <cstdint>
#include <cstdio>

#include "../kernel/drivers/usb/xhci_bulk.hpp"

using namespace drivers::usb::xhci::bulk;

namespace {

void test_endpoint_plan() {
    EndpointPlan plan{};
    assert(build_endpoint_plan(0x02U, 512U, &plan));
    assert(plan.endpoint_address == 0x02U);
    assert(plan.direction == EndpointDirection::Out);
    assert(plan.device_context_index == 4U);
    assert(plan.endpoint_type == 2U);
    assert(plan.maximum_packet_size == 512U);

    assert(build_endpoint_plan(0x81U, 1024U, &plan));
    assert(plan.direction == EndpointDirection::In);
    assert(plan.device_context_index == 3U);
    assert(plan.endpoint_type == 6U);

    const EndpointPlan sentinel{0x87U, EndpointDirection::In, 15U, 6U, 64U};
    plan = sentinel;
    assert(!build_endpoint_plan(0x80U, 64U, &plan));
    assert(plan.endpoint_address == sentinel.endpoint_address);
    assert(plan.device_context_index == sentinel.device_context_index);

    assert(!build_endpoint_plan(0x72U, 64U, &plan));
    assert(plan.endpoint_address == sentinel.endpoint_address);

    assert(!build_endpoint_plan(0x02U, 0U, &plan));
    assert(!build_endpoint_plan(0x02U, 1025U, &plan));
}

void test_endpoint_context() {
    EndpointPlan out_plan{};
    assert(build_endpoint_plan(0x02U, 512U, &out_plan));

    EndpointContextImage context{};
    assert(build_endpoint_context(
        out_plan, UINT64_C(0x0000001234500000), 512U, &context));
    assert(context.words[0] == 0U);
    assert(context.words[1] ==
        ((UINT32_C(3) << 1U) | (UINT32_C(2) << 3U) |
         (UINT32_C(512) << 16U)));
    assert(context.words[2] == UINT32_C(0x34500001));
    assert(context.words[3] == UINT32_C(0x00000012));
    assert(context.words[4] == 512U);

    EndpointPlan in_plan{};
    assert(build_endpoint_plan(0x81U, 1024U, &in_plan));
    assert(build_endpoint_context(
        in_plan, UINT64_C(0x00000000ABCDF000), 1024U, &context));
    assert(((context.words[1] >> 3U) & UINT32_C(0x7)) == 6U);
    assert((context.words[1] >> 16U) == 1024U);
    assert(context.words[2] == UINT32_C(0xABCDF001));

    const EndpointContextImage sentinel{{
        UINT32_C(1), UINT32_C(2), UINT32_C(3), UINT32_C(4), UINT32_C(5)}};
    context = sentinel;
    assert(!build_endpoint_context(
        in_plan, UINT64_C(0xABCDF008), 1024U, &context));
    for (size_t index = 0U; index < 5U; ++index) {
        assert(context.words[index] == sentinel.words[index]);
    }

    EndpointPlan mismatched = in_plan;
    mismatched.endpoint_type = 2U;
    assert(!build_endpoint_context(
        mismatched, UINT64_C(0xABCDF000), 1024U, &context));

    uint8_t entries = 0U;
    assert(extend_context_entries(1U, out_plan.device_context_index, &entries));
    assert(entries == out_plan.device_context_index);
    assert(extend_context_entries(entries, in_plan.device_context_index, &entries));
    assert(entries == out_plan.device_context_index);
    assert(!extend_context_entries(32U, 4U, &entries));
    assert(!extend_context_entries(1U, 0U, &entries));
}

void test_transfer_chunk() {
    TransferChunk chunk{};
    assert(plan_normal_trb_chunk(UINT64_C(0x100000), 4096U, &chunk));
    assert(chunk.physical_address == UINT64_C(0x100000));
    assert(chunk.length == 4096U);

    assert(plan_normal_trb_chunk(UINT64_C(0x10FFF0), 4096U, &chunk));
    assert(chunk.length == 16U);

    assert(plan_normal_trb_chunk(
        UINT64_C(0x200000), MAXIMUM_NORMAL_TRB_TRANSFER + 123U, &chunk));
    assert(chunk.length == 65536U);

    assert(plan_normal_trb_chunk(
        UINT64_C(0x210000), MAXIMUM_NORMAL_TRB_TRANSFER + 123U, &chunk));
    assert(chunk.length == 65536U);

    const TransferChunk sentinel{UINT64_C(0xABCDEF), 77U};
    chunk = sentinel;
    assert(!plan_normal_trb_chunk(UINT64_C(0x1000), 0U, &chunk));
    assert(chunk.physical_address == sentinel.physical_address);
    assert(chunk.length == sentinel.length);
}


void test_transfer_completion() {
    constexpr uint64_t expected_trb = UINT64_C(0x12345000);
    constexpr uint8_t slot = 7U;
    constexpr uint8_t dci = 5U;
    constexpr size_t requested = 512U;
    const uint32_t base_control =
        static_cast<uint32_t>(slot) << 24U |
        static_cast<uint32_t>(dci) << 16U |
        UINT32_C(1);

    TransferCompletion completion{99U, 0xEEU};
    auto status = classify_transfer_completion(
        expected_trb, slot, dci, requested,
        expected_trb, UINT32_C(1) << 24U, base_control, &completion);
    assert(status == TransferCompletionStatus::Complete);
    assert(completion.transferred == requested);
    assert(completion.completion_code == 1U);

    completion = {99U, 0xEEU};
    status = classify_transfer_completion(
        expected_trb, slot, dci, requested,
        expected_trb,
        (UINT32_C(13) << 24U) | UINT32_C(128),
        base_control,
        &completion);
    assert(status == TransferCompletionStatus::ShortPacket);
    assert(completion.transferred == 384U);
    assert(completion.completion_code == 13U);

    const TransferCompletion sentinel{77U, 0xAAU};
    auto expect_unchanged = [&](TransferCompletionStatus expected,
                                uint64_t parameter,
                                uint32_t event_status,
                                uint32_t control) {
        completion = sentinel;
        const auto result = classify_transfer_completion(
            expected_trb, slot, dci, requested,
            parameter, event_status, control, &completion);
        assert(result == expected);
        assert(completion.transferred == sentinel.transferred);
        assert(completion.completion_code == sentinel.completion_code);
    };

    expect_unchanged(
        TransferCompletionStatus::ForeignEvent,
        expected_trb + 16U,
        UINT32_C(1) << 24U,
        base_control);
    expect_unchanged(
        TransferCompletionStatus::ForeignEvent,
        expected_trb + 1U,
        UINT32_C(1) << 24U,
        base_control);
    expect_unchanged(
        TransferCompletionStatus::ForeignEvent,
        expected_trb,
        UINT32_C(1) << 24U,
        base_control | (UINT32_C(1) << 2U));
    expect_unchanged(
        TransferCompletionStatus::ForeignEvent,
        expected_trb,
        UINT32_C(1) << 24U,
        (static_cast<uint32_t>(slot + 1U) << 24U) |
            (static_cast<uint32_t>(dci) << 16U) | UINT32_C(1));
    expect_unchanged(
        TransferCompletionStatus::ForeignEvent,
        expected_trb,
        UINT32_C(1) << 24U,
        (static_cast<uint32_t>(slot) << 24U) |
            (static_cast<uint32_t>(dci + 1U) << 16U) | UINT32_C(1));
    expect_unchanged(
        TransferCompletionStatus::TransferFailed,
        expected_trb,
        UINT32_C(6) << 24U,
        base_control);
    expect_unchanged(
        TransferCompletionStatus::InvalidEvent,
        expected_trb,
        (UINT32_C(1) << 24U) | UINT32_C(1),
        base_control);
    expect_unchanged(
        TransferCompletionStatus::InvalidEvent,
        expected_trb,
        (UINT32_C(13) << 24U) | UINT32_C(513),
        base_control);

    completion = sentinel;
    assert(classify_transfer_completion(
        expected_trb + 1U, slot, dci, requested,
        expected_trb, UINT32_C(1) << 24U, base_control, &completion) ==
        TransferCompletionStatus::InvalidEvent);
    assert(completion.transferred == sentinel.transferred);
    assert(classify_transfer_completion(
        expected_trb, 0U, dci, requested,
        expected_trb, UINT32_C(1) << 24U, base_control, &completion) ==
        TransferCompletionStatus::InvalidEvent);
    assert(classify_transfer_completion(
        expected_trb, slot, 1U, requested,
        expected_trb, UINT32_C(1) << 24U, base_control, &completion) ==
        TransferCompletionStatus::InvalidEvent);
    assert(classify_transfer_completion(
        expected_trb, slot, dci, 0U,
        expected_trb, UINT32_C(1) << 24U, base_control, &completion) ==
        TransferCompletionStatus::InvalidEvent);
}

void test_full_span_progress() {
    uint64_t address = UINT64_C(0x30FFF0);
    size_t remaining = 200000U;
    size_t total = 0U;
    size_t iterations = 0U;

    while (remaining != 0U) {
        TransferChunk chunk{};
        assert(plan_normal_trb_chunk(address, remaining, &chunk));
        assert(chunk.length != 0U);
        assert(chunk.length <= MAXIMUM_NORMAL_TRB_TRANSFER);

        const uint64_t start_window =
            chunk.physical_address / TRB_BUFFER_BOUNDARY;
        const uint64_t end_window =
            (chunk.physical_address + chunk.length - 1U) / TRB_BUFFER_BOUNDARY;
        assert(start_window == end_window);

        address += chunk.length;
        remaining -= chunk.length;
        total += chunk.length;
        ++iterations;
        assert(iterations < 16U);
    }

    assert(total == 200000U);
    assert(iterations >= 4U);
}

} // namespace

int main() {
    test_endpoint_plan();
    test_endpoint_context();
    test_transfer_chunk();
    test_transfer_completion();
    test_full_span_progress();
    std::puts("xHCI bulk planning: PASS");
    return 0;
}
