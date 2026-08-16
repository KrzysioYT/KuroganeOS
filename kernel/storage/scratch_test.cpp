#include "scratch_test.hpp"

#include <stddef.h>

namespace storage::scratch_test {
namespace {

constexpr char MAGIC[] = "KUROGANE_AHCI_SCRATCH_V1";
constexpr size_t MAGIC_LENGTH = sizeof(MAGIC) - 1U;
constexpr uint32_t FORMAT_VERSION = 1U;
constexpr uint32_t HEADER_SIZE = 64U;
constexpr uint32_t HEADER_GUARD = UINT32_C(0x4B535431);

uint32_t read_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) |
           (static_cast<uint32_t>(bytes[3]) << 24U);
}

uint64_t read_u64(const uint8_t* bytes) {
    return static_cast<uint64_t>(read_u32(bytes)) |
           (static_cast<uint64_t>(read_u32(bytes + 4U)) << 32U);
}

bool equal_bytes(const uint8_t* left, const uint8_t* right, size_t count) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (size_t index = 0U; index < count; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

bool has_magic(const uint8_t* header) {
    return header != nullptr &&
           equal_bytes(
               header,
               reinterpret_cast<const uint8_t*>(MAGIC),
               MAGIC_LENGTH);
}

void fill_pattern(uint8_t* output, size_t byte_count) {
    uint32_t state = UINT32_C(0x4B55524F);
    for (size_t index = 0U; index < byte_count; ++index) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        output[index] = static_cast<uint8_t>(
            (state ^ static_cast<uint32_t>(index)) & UINT32_C(0xFF));
    }
}

Status restore_original(
    const block::Device* device,
    Workspace* workspace,
    Result& result,
    size_t byte_count) {
    block::Status block_status = block::write_blocks(
        device,
        result.first_lba,
        result.sector_count,
        workspace->original,
        byte_count);
    if (block_status != block::Status::Ok) {
        result.block_status = block_status;
        return Status::RestoreWriteFailed;
    }
    block_status = block::flush(device);
    if (block_status != block::Status::Ok) {
        result.block_status = block_status;
        return Status::RestoreFlushFailed;
    }
    block_status = block::read_blocks(
        device,
        result.first_lba,
        result.sector_count,
        workspace->verification,
        byte_count);
    if (block_status != block::Status::Ok) {
        result.block_status = block_status;
        return Status::RestoreReadFailed;
    }
    if (!equal_bytes(
            workspace->original, workspace->verification, byte_count)) {
        result.block_status = block::Status::Ok;
        return Status::RestoreMismatch;
    }
    result.restored = true;
    return Status::Ok;
}

Result fail_after_write(
    const block::Device* device,
    Workspace* workspace,
    Result result,
    Status primary_status,
    block::Status block_status,
    size_t byte_count) {
    result.primary_status = primary_status;
    result.block_status = block_status;
    const Status recovery_status =
        restore_original(device, workspace, result, byte_count);
    result.status = recovery_status == Status::Ok
        ? primary_status
        : recovery_status;
    return result;
}

} // namespace

Result run(const block::Device* device, Workspace* workspace) {
    Result result{
        Status::InvalidArgument,
        Status::InvalidArgument,
        block::Status::InvalidArgument,
        0U,
        0U,
        false,
        false,
        false};
    if (device == nullptr || workspace == nullptr) {
        return result;
    }

    const block::Status device_status = block::validate(device);
    if (device_status != block::Status::Ok) {
        result.block_status = device_status;
        return result;
    }
    if (device->sector_size != SCRATCH_SECTOR_SIZE) {
        result.status = Status::GeometryMismatch;
        result.primary_status = result.status;
        result.block_status = block::Status::InvalidGeometry;
        return result;
    }

    block::Status block_status = block::read_blocks(
        device, 0U, 1U, workspace->header, sizeof(workspace->header));
    if (block_status != block::Status::Ok) {
        result.status = Status::InitialReadFailed;
        result.primary_status = result.status;
        result.block_status = block_status;
        return result;
    }
    if (!has_magic(workspace->header)) {
        result.status = Status::NotTagged;
        result.primary_status = result.status;
        result.block_status = block::Status::Ok;
        return result;
    }
    result.tagged = true;

    for (size_t index = MAGIC_LENGTH; index < 32U; ++index) {
        if (workspace->header[index] != 0U) {
            result.status = Status::InvalidHeader;
            result.primary_status = result.status;
            result.block_status = block::Status::Ok;
            return result;
        }
    }
    const uint32_t version = read_u32(workspace->header + 32U);
    const uint32_t header_size = read_u32(workspace->header + 36U);
    const uint64_t declared_sectors = read_u64(workspace->header + 40U);
    result.first_lba = read_u64(workspace->header + 48U);
    result.sector_count = read_u32(workspace->header + 56U);
    const uint32_t guard = read_u32(workspace->header + 60U);
    if (version != FORMAT_VERSION || header_size != HEADER_SIZE ||
        guard != HEADER_GUARD) {
        result.status = Status::InvalidHeader;
        result.primary_status = result.status;
        result.block_status = block::Status::Ok;
        return result;
    }
    if (declared_sectors != device->sector_count) {
        result.status = Status::GeometryMismatch;
        result.primary_status = result.status;
        result.block_status = block::Status::InvalidGeometry;
        return result;
    }
    if (result.first_lba == 0U || result.sector_count == 0U ||
        result.sector_count > MAXIMUM_TEST_SECTORS ||
        result.first_lba >= device->sector_count ||
        static_cast<uint64_t>(result.sector_count) >
            device->sector_count - result.first_lba) {
        result.status = Status::RangeInvalid;
        result.primary_status = result.status;
        result.block_status = block::Status::OutOfRange;
        return result;
    }

    const size_t byte_count =
        static_cast<size_t>(result.sector_count) * SCRATCH_SECTOR_SIZE;
    block_status = block::read_blocks(
        device,
        result.first_lba,
        result.sector_count,
        workspace->original,
        byte_count);
    if (block_status != block::Status::Ok) {
        result.status = Status::InitialReadFailed;
        result.primary_status = result.status;
        result.block_status = block_status;
        return result;
    }
    fill_pattern(workspace->pattern, byte_count);

    result.write_attempted = true;
    block_status = block::write_blocks(
        device,
        result.first_lba,
        result.sector_count,
        workspace->pattern,
        byte_count);
    if (block_status != block::Status::Ok) {
        return fail_after_write(
            device,
            workspace,
            result,
            Status::PatternWriteFailed,
            block_status,
            byte_count);
    }
    block_status = block::flush(device);
    if (block_status != block::Status::Ok) {
        return fail_after_write(
            device,
            workspace,
            result,
            Status::PatternFlushFailed,
            block_status,
            byte_count);
    }
    block_status = block::read_blocks(
        device,
        result.first_lba,
        result.sector_count,
        workspace->verification,
        byte_count);
    if (block_status != block::Status::Ok) {
        return fail_after_write(
            device,
            workspace,
            result,
            Status::VerifyReadFailed,
            block_status,
            byte_count);
    }
    if (!equal_bytes(
            workspace->pattern, workspace->verification, byte_count)) {
        return fail_after_write(
            device,
            workspace,
            result,
            Status::VerifyMismatch,
            block::Status::Ok,
            byte_count);
    }

    result.primary_status = Status::Ok;
    result.block_status = block::Status::Ok;
    const Status recovery_status =
        restore_original(device, workspace, result, byte_count);
    result.status = recovery_status;
    return result;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotTagged: return "device is not a tagged scratch disk";
        case Status::InvalidArgument: return "invalid scratch-test argument";
        case Status::InvalidHeader: return "invalid scratch-disk header";
        case Status::GeometryMismatch: return "scratch geometry mismatch";
        case Status::RangeInvalid: return "scratch test range is invalid";
        case Status::InitialReadFailed: return "scratch initial read failed";
        case Status::PatternWriteFailed: return "scratch pattern write failed";
        case Status::PatternFlushFailed: return "scratch pattern flush failed";
        case Status::VerifyReadFailed: return "scratch verification read failed";
        case Status::VerifyMismatch: return "scratch readback mismatch";
        case Status::RestoreWriteFailed: return "scratch restore write failed";
        case Status::RestoreFlushFailed: return "scratch restore flush failed";
        case Status::RestoreReadFailed: return "scratch restore read failed";
        case Status::RestoreMismatch: return "scratch restore readback mismatch";
    }
    return "unknown scratch-test status";
}

} // namespace storage::scratch_test
