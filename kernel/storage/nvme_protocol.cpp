#include "nvme_protocol.hpp"

namespace storage::nvme::protocol {
namespace {

constexpr uint8_t ADMIN_IDENTIFY = 0x06U;
constexpr uint8_t NVM_FLUSH = 0x00U;

bool power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

uint64_t read_le64(const uint8_t* bytes) {
    uint64_t value = 0U;
    for (size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

uint16_t read_le16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(bytes[1]) << 8U;
}

void clear_command(Command* command) {
    for (size_t index = 0U; index < COMMAND_DWORDS; ++index) {
        command->dwords[index] = 0U;
    }
}

void set_prp1(Command* command, uint64_t prp1) {
    command->dwords[6U] = static_cast<uint32_t>(prp1);
    command->dwords[7U] = static_cast<uint32_t>(prp1 >> 32U);
}

bool valid_identify_prp(uint64_t prp1) {
    return prp1 != 0U && (prp1 & UINT64_C(0xFFF)) == 0U;
}

} // namespace

Status decode_capabilities(uint64_t raw, Capabilities* output) {
    if (output == nullptr) return Status::InvalidArgument;

    const uint32_t maximum_queue_entries =
        static_cast<uint32_t>(raw & UINT64_C(0xFFFF)) + 1U;
    const uint8_t timeout =
        static_cast<uint8_t>((raw >> 24U) & UINT64_C(0xFF));
    const uint8_t doorbell_stride =
        static_cast<uint8_t>((raw >> 32U) & UINT64_C(0x0F));
    const uint8_t minimum_page_shift =
        static_cast<uint8_t>((raw >> 48U) & UINT64_C(0x0F));
    const uint8_t maximum_page_shift =
        static_cast<uint8_t>((raw >> 52U) & UINT64_C(0x0F));
    const bool nvm_supported = (raw & (UINT64_C(1) << 37U)) != 0U;

    if (maximum_queue_entries < 2U ||
        minimum_page_shift > maximum_page_shift ||
        !nvm_supported) {
        return Status::UnsupportedCapabilities;
    }

    const uint32_t minimum_page_size =
        UINT32_C(1) << (12U + minimum_page_shift);
    const uint32_t maximum_page_size =
        UINT32_C(1) << (12U + maximum_page_shift);
    const uint32_t doorbell_stride_bytes =
        UINT32_C(4) << doorbell_stride;

    const Capabilities staged{
        maximum_queue_entries,
        (raw & (UINT64_C(1) << 16U)) != 0U,
        timeout,
        doorbell_stride_bytes,
        nvm_supported,
        minimum_page_size,
        maximum_page_size,
    };
    *output = staged;
    return Status::Ok;
}

Status build_controller_configuration(
    const Capabilities& capabilities,
    uint32_t page_size,
    uint32_t* output_cc) {
    if (output_cc == nullptr) return Status::InvalidArgument;
    if (!power_of_two(page_size) || page_size < UINT32_C(4096) ||
        page_size < capabilities.minimum_page_size ||
        page_size > capabilities.maximum_page_size) {
        return Status::InvalidPageSize;
    }

    uint8_t log2 = 0U;
    uint32_t value = page_size;
    while (value > 1U) {
        value >>= 1U;
        ++log2;
    }
    if (log2 < 12U || log2 - 12U > 15U) {
        return Status::InvalidPageSize;
    }

    const uint32_t mps = static_cast<uint32_t>(log2 - 12U);
    // EN=1, CSS=NVM(0), MPS=selected page, AMS=round-robin(0),
    // IOSQES=6 (64 bytes), IOCQES=4 (16 bytes).
    const uint32_t staged =
        UINT32_C(1) |
        (mps << 7U) |
        (UINT32_C(6) << 16U) |
        (UINT32_C(4) << 20U);
    *output_cc = staged;
    return Status::Ok;
}

Status build_admin_queue_attributes(
    const Capabilities& capabilities,
    uint16_t queue_entries,
    uint32_t* output_aqa) {
    if (output_aqa == nullptr) return Status::InvalidArgument;
    if (queue_entries < 2U ||
        queue_entries > 4096U ||
        static_cast<uint32_t>(queue_entries) >
            capabilities.maximum_queue_entries) {
        return Status::InvalidQueueSize;
    }
    const uint32_t zero_based = static_cast<uint32_t>(queue_entries - 1U);
    *output_aqa = zero_based | (zero_based << 16U);
    return Status::Ok;
}

Status build_identify_controller(
    uint16_t command_id,
    uint64_t prp1,
    Command* output) {
    if (output == nullptr || !valid_identify_prp(prp1)) {
        return Status::InvalidArgument;
    }
    Command staged{};
    clear_command(&staged);
    staged.dwords[0U] =
        static_cast<uint32_t>(ADMIN_IDENTIFY) |
        (static_cast<uint32_t>(command_id) << 16U);
    set_prp1(&staged, prp1);
    staged.dwords[10U] = 1U; // CNS = Identify Controller
    *output = staged;
    return Status::Ok;
}

Status build_identify_namespace(
    uint16_t command_id,
    uint32_t namespace_id,
    uint64_t prp1,
    Command* output) {
    if (output == nullptr || namespace_id == 0U ||
        !valid_identify_prp(prp1)) {
        return Status::InvalidArgument;
    }
    Command staged{};
    clear_command(&staged);
    staged.dwords[0U] =
        static_cast<uint32_t>(ADMIN_IDENTIFY) |
        (static_cast<uint32_t>(command_id) << 16U);
    staged.dwords[1U] = namespace_id;
    set_prp1(&staged, prp1);
    staged.dwords[10U] = 0U; // CNS = Identify Namespace
    *output = staged;
    return Status::Ok;
}

Status build_flush(
    uint16_t command_id,
    uint32_t namespace_id,
    Command* output) {
    if (output == nullptr || namespace_id == 0U) {
        return Status::InvalidArgument;
    }
    Command staged{};
    clear_command(&staged);
    staged.dwords[0U] =
        static_cast<uint32_t>(NVM_FLUSH) |
        (static_cast<uint32_t>(command_id) << 16U);
    staged.dwords[1U] = namespace_id;
    *output = staged;
    return Status::Ok;
}

Status parse_identify_namespace(
    const uint8_t* bytes,
    size_t length,
    NamespaceInfo* output) {
    if (bytes == nullptr || output == nullptr || length < IDENTIFY_BYTES) {
        return Status::InvalidArgument;
    }

    const uint64_t size_blocks = read_le64(bytes + 0U);
    const uint64_t capacity_blocks = read_le64(bytes + 8U);
    const uint8_t lba_format_count_minus_one = bytes[25U];
    const uint8_t flbas = bytes[26U];
    const uint8_t selected = static_cast<uint8_t>(flbas & 0x0FU);

    if (size_blocks == 0U || capacity_blocks == 0U ||
        capacity_blocks > size_blocks) {
        return Status::InvalidNamespace;
    }
    // The bounded Steel backend initially supports the base 16 LBA formats
    // addressable directly by FLBAS[3:0].
    if (lba_format_count_minus_one > 15U ||
        selected > lba_format_count_minus_one ||
        (flbas & UINT8_C(0xE0)) != 0U) {
        return Status::UnsupportedLbaFormat;
    }

    const size_t format_offset = 128U + static_cast<size_t>(selected) * 4U;
    const uint16_t metadata_size = read_le16(bytes + format_offset);
    const uint8_t lba_data_size = bytes[format_offset + 2U];
    if (metadata_size != 0U ||
        lba_data_size < 9U || lba_data_size > 12U) {
        return Status::UnsupportedLbaFormat;
    }

    const uint32_t block_size = UINT32_C(1) << lba_data_size;
    if (block_size < 512U || block_size > 4096U ||
        (4096U % block_size) != 0U) {
        return Status::UnsupportedLbaFormat;
    }

    const NamespaceInfo staged{
        size_blocks,
        capacity_blocks,
        block_size,
        selected,
    };
    *output = staged;
    return Status::Ok;
}

Status parse_completion(
    const uint32_t dwords[COMPLETION_DWORDS],
    uint16_t expected_command_id,
    bool expected_phase,
    Completion* output) {
    if (dwords == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }

    const uint16_t command_id =
        static_cast<uint16_t>(dwords[3U] & UINT32_C(0xFFFF));
    const uint16_t raw_status =
        static_cast<uint16_t>(dwords[3U] >> 16U);
    const bool phase = (raw_status & UINT16_C(1)) != 0U;
    if (command_id != expected_command_id || phase != expected_phase) {
        return Status::InvalidCompletion;
    }

    const uint8_t status_code =
        static_cast<uint8_t>((raw_status >> 1U) & UINT16_C(0xFF));
    const uint8_t status_code_type =
        static_cast<uint8_t>((raw_status >> 9U) & UINT16_C(0x07));
    const Completion staged{
        dwords[0U],
        static_cast<uint16_t>(dwords[2U] & UINT32_C(0xFFFF)),
        static_cast<uint16_t>(dwords[2U] >> 16U),
        command_id,
        status_code,
        status_code_type,
        phase,
        (raw_status & (UINT16_C(1) << 15U)) != 0U,
        status_code == 0U && status_code_type == 0U,
    };
    *output = staged;
    return Status::Ok;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidArgument: return "invalid NVMe protocol argument";
        case Status::UnsupportedCapabilities:
            return "unsupported NVMe controller capabilities";
        case Status::InvalidPageSize: return "invalid NVMe controller page size";
        case Status::InvalidQueueSize: return "invalid NVMe queue size";
        case Status::InvalidNamespace: return "invalid NVMe namespace";
        case Status::UnsupportedLbaFormat:
            return "unsupported NVMe namespace LBA format";
        case Status::InvalidTransfer: return "invalid NVMe transfer";
        case Status::InvalidCompletion: return "invalid NVMe completion";
    }
    return "unknown NVMe protocol status";
}

} // namespace storage::nvme::protocol
