#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../kernel/drivers/usb/mass_storage_protocol.hpp"

using namespace drivers::usb::mass_storage;

namespace {

bool same_interface(const BulkOnlyInterface& left, const BulkOnlyInterface& right) {
    return left.configuration_value == right.configuration_value &&
        left.interface_number == right.interface_number &&
        left.bulk_in_endpoint == right.bulk_in_endpoint &&
        left.bulk_in_maximum_packet_size == right.bulk_in_maximum_packet_size &&
        left.bulk_out_endpoint == right.bulk_out_endpoint &&
        left.bulk_out_maximum_packet_size == right.bulk_out_maximum_packet_size;
}

bool same_csw(const CommandStatusWrapper& left, const CommandStatusWrapper& right) {
    return left.tag == right.tag &&
        left.data_residue == right.data_residue &&
        left.status == right.status;
}

bool same_capacity(
    const scsi::ReadCapacity10Data& left,
    const scsi::ReadCapacity10Data& right) {
    return left.last_logical_block_address == right.last_logical_block_address &&
        left.block_size == right.block_size &&
        left.block_count == right.block_count;
}

void test_interface_parser() {
    const uint8_t configuration[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 0x08, 0x06, 0x50, 0,
        7, 5, 0x02, 0x02, 0x00, 0x02, 0,
        7, 5, 0x81, 0x02, 0x00, 0x02, 0,
    };
    BulkOnlyInterface found{};
    assert(find_bulk_only_scsi_interface(
        configuration, sizeof(configuration), &found));
    assert(found.configuration_value == 1U);
    assert(found.interface_number == 0U);
    assert(found.bulk_in_endpoint == 0x81U);
    assert(found.bulk_in_maximum_packet_size == 512U);
    assert(found.bulk_out_endpoint == 0x02U);
    assert(found.bulk_out_maximum_packet_size == 512U);

    const BulkOnlyInterface sentinel{9U, 8U, 0x87U, 64U, 0x06U, 32U};
    uint8_t malformed[sizeof(configuration)]{};
    std::memcpy(malformed, configuration, sizeof(configuration));

    malformed[15] = 0x05U;
    found = sentinel;
    assert(!find_bulk_only_scsi_interface(
        malformed, sizeof(malformed), &found));
    assert(same_interface(found, sentinel));

    std::memcpy(malformed, configuration, sizeof(configuration));
    malformed[12] = 1U;
    found = sentinel;
    assert(!find_bulk_only_scsi_interface(
        malformed, sizeof(malformed), &found));
    assert(same_interface(found, sentinel));

    std::memcpy(malformed, configuration, sizeof(configuration));
    malformed[21] = 0x03U;
    found = sentinel;
    assert(!find_bulk_only_scsi_interface(
        malformed, sizeof(malformed), &found));
    assert(same_interface(found, sentinel));

    std::memcpy(malformed, configuration, sizeof(configuration));
    malformed[20] = 0x82U;
    found = sentinel;
    assert(!find_bulk_only_scsi_interface(
        malformed, sizeof(malformed), &found));
    assert(same_interface(found, sentinel));

    std::memcpy(malformed, configuration, sizeof(configuration));
    malformed[4] = 0U;
    found = sentinel;
    assert(!find_bulk_only_scsi_interface(
        malformed, sizeof(malformed), &found));
    assert(same_interface(found, sentinel));
}

void test_cbw() {
    uint8_t cdb[scsi::CDB10_SIZE]{};
    assert(scsi::build_read_capacity10(cdb, sizeof(cdb)));

    CommandBlockWrapper wrapper{};
    wrapper.tag = UINT32_C(0x11223344);
    wrapper.data_transfer_length = 8U;
    wrapper.direction = DataDirection::In;
    wrapper.logical_unit_number = 2U;
    wrapper.command_length = static_cast<uint8_t>(sizeof(cdb));
    std::memcpy(wrapper.command, cdb, sizeof(cdb));

    uint8_t encoded[COMMAND_BLOCK_WRAPPER_SIZE]{};
    assert(encode_command_block_wrapper(&wrapper, encoded, sizeof(encoded)));
    const uint8_t prefix[] = {
        0x55, 0x53, 0x42, 0x43,
        0x44, 0x33, 0x22, 0x11,
        0x08, 0x00, 0x00, 0x00,
        0x80, 0x02, 0x0A,
    };
    assert(std::memcmp(encoded, prefix, sizeof(prefix)) == 0);
    assert(std::memcmp(encoded + 15U, cdb, sizeof(cdb)) == 0);
    for (size_t index = 15U + sizeof(cdb);
         index < COMMAND_BLOCK_WRAPPER_SIZE; ++index) {
        assert(encoded[index] == 0U);
    }

    uint8_t untouched[COMMAND_BLOCK_WRAPPER_SIZE];
    std::memset(untouched, 0xA5, sizeof(untouched));
    uint8_t before[sizeof(untouched)];
    std::memcpy(before, untouched, sizeof(before));

    wrapper.logical_unit_number = 16U;
    assert(!encode_command_block_wrapper(&wrapper, untouched, sizeof(untouched)));
    assert(std::memcmp(untouched, before, sizeof(before)) == 0);
    wrapper.logical_unit_number = 0U;

    wrapper.command_length = 0U;
    assert(!encode_command_block_wrapper(&wrapper, untouched, sizeof(untouched)));
    assert(std::memcmp(untouched, before, sizeof(before)) == 0);
    wrapper.command_length = static_cast<uint8_t>(sizeof(cdb));

    wrapper.data_transfer_length = 0U;
    assert(!encode_command_block_wrapper(&wrapper, untouched, sizeof(untouched)));
    assert(std::memcmp(untouched, before, sizeof(before)) == 0);
    wrapper.direction = DataDirection::None;
    assert(encode_command_block_wrapper(&wrapper, untouched, sizeof(untouched)));
    assert(untouched[12] == 0U);
}

void test_csw() {
    const uint8_t passed[] = {
        0x55, 0x53, 0x42, 0x53,
        0x44, 0x33, 0x22, 0x11,
        0x04, 0x00, 0x00, 0x00,
        0x00,
    };
    CommandStatusWrapper parsed{};
    assert(decode_command_status_wrapper(
        passed, sizeof(passed), UINT32_C(0x11223344), 8U, &parsed));
    assert(parsed.tag == UINT32_C(0x11223344));
    assert(parsed.data_residue == 4U);
    assert(parsed.status == CommandStatus::Passed);

    const CommandStatusWrapper sentinel{
        UINT32_C(0xAABBCCDD), 77U, CommandStatus::Failed};
    parsed = sentinel;
    uint8_t invalid[sizeof(passed)]{};
    std::memcpy(invalid, passed, sizeof(passed));
    invalid[8] = 9U;
    assert(!decode_command_status_wrapper(
        invalid, sizeof(invalid), UINT32_C(0x11223344), 8U, &parsed));
    assert(same_csw(parsed, sentinel));

    std::memcpy(invalid, passed, sizeof(passed));
    invalid[12] = 3U;
    assert(!decode_command_status_wrapper(
        invalid, sizeof(invalid), UINT32_C(0x11223344), 8U, &parsed));
    assert(same_csw(parsed, sentinel));

    std::memcpy(invalid, passed, sizeof(passed));
    invalid[12] = 2U;
    invalid[8] = 0xFFU;
    invalid[9] = 0xFFU;
    invalid[10] = 0xFFU;
    invalid[11] = 0xFFU;
    assert(decode_command_status_wrapper(
        invalid, sizeof(invalid), UINT32_C(0x11223344), 8U, &parsed));
    assert(parsed.status == CommandStatus::PhaseError);
    assert(parsed.data_residue == UINT32_MAX);

    parsed = sentinel;
    assert(!decode_command_status_wrapper(
        passed, sizeof(passed) - 1U, UINT32_C(0x11223344), 8U, &parsed));
    assert(same_csw(parsed, sentinel));
    assert(!decode_command_status_wrapper(
        passed, sizeof(passed), UINT32_C(0x55667788), 8U, &parsed));
    assert(same_csw(parsed, sentinel));
}

void test_scsi_cdbs() {
    uint8_t cdb[16]{};
    assert(scsi::build_test_unit_ready(cdb, sizeof(cdb)));
    for (size_t index = 0U; index < scsi::CDB6_SIZE; ++index) {
        assert(cdb[index] == 0U);
    }

    std::memset(cdb, 0xCC, sizeof(cdb));
    assert(scsi::build_inquiry(36U, cdb, sizeof(cdb)));
    const uint8_t inquiry[] = {0x12, 0, 0, 0, 36, 0};
    assert(std::memcmp(cdb, inquiry, sizeof(inquiry)) == 0);

    std::memset(cdb, 0xCC, sizeof(cdb));
    assert(scsi::build_request_sense(18U, cdb, sizeof(cdb)));
    const uint8_t request_sense[] = {0x03, 0, 0, 0, 18, 0};
    assert(std::memcmp(cdb, request_sense, sizeof(request_sense)) == 0);

    std::memset(cdb, 0xCC, sizeof(cdb));
    assert(scsi::build_read_capacity10(cdb, sizeof(cdb)));
    assert(cdb[0] == 0x25U);
    for (size_t index = 1U; index < scsi::CDB10_SIZE; ++index) {
        assert(cdb[index] == 0U);
    }

    std::memset(cdb, 0, sizeof(cdb));
    assert(scsi::build_read10(
        UINT32_C(0x12345678), UINT16_C(0x0203), cdb, sizeof(cdb)));
    const uint8_t read10[] = {
        0x28, 0x00, 0x12, 0x34, 0x56, 0x78, 0x00, 0x02, 0x03, 0x00};
    assert(std::memcmp(cdb, read10, sizeof(read10)) == 0);

    std::memset(cdb, 0, sizeof(cdb));
    assert(scsi::build_write10(
        UINT32_C(0x89ABCDEF), UINT16_C(0x0102), cdb, sizeof(cdb)));
    const uint8_t write10[] = {
        0x2A, 0x00, 0x89, 0xAB, 0xCD, 0xEF, 0x00, 0x01, 0x02, 0x00};
    assert(std::memcmp(cdb, write10, sizeof(write10)) == 0);

    std::memset(cdb, 0xAA, sizeof(cdb));
    uint8_t before[sizeof(cdb)];
    std::memcpy(before, cdb, sizeof(cdb));
    assert(!scsi::build_read10(0U, 0U, cdb, sizeof(cdb)));
    assert(std::memcmp(cdb, before, sizeof(cdb)) == 0);
    assert(!scsi::build_inquiry(0U, cdb, sizeof(cdb)));
    assert(std::memcmp(cdb, before, sizeof(cdb)) == 0);

    std::memset(cdb, 0, sizeof(cdb));
    assert(scsi::build_synchronize_cache10(cdb, sizeof(cdb)));
    assert(cdb[0] == 0x35U);
}

void test_read_capacity() {
    const uint8_t capacity[] = {
        0x00, 0x0F, 0xFF, 0xFF,
        0x00, 0x00, 0x02, 0x00,
    };
    scsi::ReadCapacity10Data parsed{};
    assert(scsi::parse_read_capacity10(capacity, sizeof(capacity), &parsed));
    assert(parsed.last_logical_block_address == UINT32_C(0x000FFFFF));
    assert(parsed.block_size == 512U);
    assert(parsed.block_count == UINT64_C(0x00100000));

    const scsi::ReadCapacity10Data sentinel{
        UINT32_C(0x11111111), UINT32_C(0x22222222), UINT64_C(0x33333333)};
    parsed = sentinel;
    uint8_t unsupported[sizeof(capacity)] = {
        0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x02, 0x00,
    };
    assert(!scsi::parse_read_capacity10(
        unsupported, sizeof(unsupported), &parsed));
    assert(same_capacity(parsed, sentinel));

    uint8_t zero_block[sizeof(capacity)] = {
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
    };
    assert(!scsi::parse_read_capacity10(
        zero_block, sizeof(zero_block), &parsed));
    assert(same_capacity(parsed, sentinel));
}

} // namespace

int main() {
    test_interface_parser();
    test_cbw();
    test_csw();
    test_scsi_cdbs();
    test_read_capacity();
    std::puts("USB Mass Storage BOT/SCSI protocol: PASS");
    return 0;
}
