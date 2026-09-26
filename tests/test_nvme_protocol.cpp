#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../kernel/storage/nvme_protocol.hpp"

using namespace storage::nvme::protocol;

namespace {

void put16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
}

void put64(uint8_t* bytes, uint64_t value) {
    for (size_t index = 0U; index < 8U; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8U));
    }
}

void test_capabilities() {
    const uint64_t cap =
        UINT64_C(63) |
        (UINT64_C(1) << 16U) |
        (UINT64_C(10) << 24U) |
        (UINT64_C(2) << 32U) |
        (UINT64_C(1) << 37U) |
        (UINT64_C(0) << 48U) |
        (UINT64_C(2) << 52U);

    Capabilities decoded{};
    assert(decode_capabilities(cap, &decoded) == Status::Ok);
    assert(decoded.maximum_queue_entries == 64U);
    assert(decoded.contiguous_queues_required);
    assert(decoded.timeout_500ms_units == 10U);
    assert(decoded.doorbell_stride_bytes == 16U);
    assert(decoded.nvm_command_set_supported);
    assert(decoded.minimum_page_size == 4096U);
    assert(decoded.maximum_page_size == 16384U);

    uint32_t cc = 0U;
    assert(build_controller_configuration(decoded, 4096U, &cc) == Status::Ok);
    assert((cc & 1U) != 0U);
    assert(((cc >> 7U) & 0xFU) == 0U);
    assert(((cc >> 16U) & 0xFU) == 6U);
    assert(((cc >> 20U) & 0xFU) == 4U);
    assert(build_controller_configuration(decoded, 8192U, &cc) == Status::Ok);
    assert(((cc >> 7U) & 0xFU) == 1U);
    assert(build_controller_configuration(decoded, 2048U, &cc) ==
        Status::InvalidPageSize);

    uint32_t aqa = 0U;
    assert(build_admin_queue_attributes(decoded, 32U, &aqa) == Status::Ok);
    assert((aqa & 0xFFFU) == 31U);
    assert(((aqa >> 16U) & 0xFFFU) == 31U);
    assert(build_admin_queue_attributes(decoded, 65U, &aqa) ==
        Status::InvalidQueueSize);

    Capabilities sentinel = decoded;
    const uint64_t no_nvm = cap & ~(UINT64_C(1) << 37U);
    assert(decode_capabilities(no_nvm, &decoded) ==
        Status::UnsupportedCapabilities);
    assert(decoded.maximum_queue_entries == sentinel.maximum_queue_entries);
}

void test_commands() {
    Command command{};
    assert(build_identify_controller(
        0x1234U, UINT64_C(0x0000000123400000), &command) == Status::Ok);
    assert((command.dwords[0] & 0xFFU) == 0x06U);
    assert((command.dwords[0] >> 16U) == 0x1234U);
    assert(command.dwords[1] == 0U);
    assert(command.dwords[6] == UINT32_C(0x23400000));
    assert(command.dwords[7] == 1U);
    assert(command.dwords[10] == 1U);

    assert(build_identify_namespace(
        9U, 7U, UINT64_C(0x200000), &command) == Status::Ok);
    assert(command.dwords[1] == 7U);
    assert(command.dwords[10] == 0U);

    const Command sentinel = command;
    assert(build_identify_namespace(
        9U, 0U, UINT64_C(0x200000), &command) == Status::InvalidArgument);
    assert(std::memcmp(&command, &sentinel, sizeof(command)) == 0);
    assert(build_identify_controller(
        1U, UINT64_C(0x200123), &command) == Status::InvalidArgument);

    assert(build_flush(0x55AAU, 3U, &command) == Status::Ok);
    assert((command.dwords[0] & 0xFFU) == 0U);
    assert((command.dwords[0] >> 16U) == 0x55AAU);
    assert(command.dwords[1] == 3U);
}

void test_namespace() {
    uint8_t bytes[IDENTIFY_BYTES]{};
    put64(bytes + 0U, UINT64_C(0x100000));
    put64(bytes + 8U, UINT64_C(0x0F0000));
    bytes[25U] = 1U;
    bytes[26U] = 1U;
    put16(bytes + 128U, 0U);
    bytes[130U] = 9U;
    put16(bytes + 132U, 0U);
    bytes[134U] = 12U;

    NamespaceInfo info{};
    assert(parse_identify_namespace(bytes, sizeof(bytes), &info) == Status::Ok);
    assert(info.size_blocks == UINT64_C(0x100000));
    assert(info.capacity_blocks == UINT64_C(0x0F0000));
    assert(info.block_size == 4096U);
    assert(info.lba_format_index == 1U);

    const NamespaceInfo sentinel{
        UINT64_C(1), UINT64_C(2), 512U, 0U};
    info = sentinel;
    bytes[26U] = 2U;
    assert(parse_identify_namespace(bytes, sizeof(bytes), &info) ==
        Status::UnsupportedLbaFormat);
    assert(info.block_size == sentinel.block_size);

    bytes[26U] = 1U;
    put16(bytes + 132U, 8U);
    assert(parse_identify_namespace(bytes, sizeof(bytes), &info) ==
        Status::UnsupportedLbaFormat);

    put16(bytes + 132U, 0U);
    put64(bytes + 8U, UINT64_C(0x200000));
    assert(parse_identify_namespace(bytes, sizeof(bytes), &info) ==
        Status::InvalidNamespace);
}

void test_completion() {
    const uint32_t success[COMPLETION_DWORDS] = {
        UINT32_C(0xAABBCCDD),
        0U,
        UINT32_C(0x00070009),
        UINT32_C(0x00011234),
    };
    Completion completion{};
    assert(parse_completion(success, 0x1234U, true, &completion) == Status::Ok);
    assert(completion.result == UINT32_C(0xAABBCCDD));
    assert(completion.submission_queue_head == 9U);
    assert(completion.submission_queue_id == 7U);
    assert(completion.command_id == 0x1234U);
    assert(completion.phase);
    assert(completion.success);

    uint32_t failed[COMPLETION_DWORDS]{};
    std::memcpy(failed, success, sizeof(failed));
    const uint16_t status =
        static_cast<uint16_t>(
            1U |
            (UINT16_C(0x80) << 1U) |
            (UINT16_C(2) << 9U) |
            (UINT16_C(1) << 15U));
    failed[3U] = UINT32_C(0x1234) |
        (static_cast<uint32_t>(status) << 16U);
    assert(parse_completion(failed, 0x1234U, true, &completion) == Status::Ok);
    assert(!completion.success);
    assert(completion.status_code == 0x80U);
    assert(completion.status_code_type == 2U);
    assert(completion.do_not_retry);

    const Completion sentinel = completion;
    assert(parse_completion(success, 0x9999U, true, &completion) ==
        Status::InvalidCompletion);
    assert(completion.command_id == sentinel.command_id);
    assert(parse_completion(success, 0x1234U, false, &completion) ==
        Status::InvalidCompletion);
}

} // namespace

int main() {
    test_capabilities();
    test_commands();
    test_namespace();
    test_completion();
    std::puts("NVMe protocol foundation: PASS");
    return 0;
}
