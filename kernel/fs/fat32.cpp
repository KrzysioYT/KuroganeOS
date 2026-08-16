#include "fat32.hpp"

namespace fs::fat32 {
namespace {

static constexpr uint32_t FAT32_MIN_CLUSTERS = 65525U;
// 0x0ffffff0..0x0ffffff6 are reserved FAT32 values.  With cluster numbers
// starting at two, 0x0fffffef is the last usable cluster and the maximum
// count is therefore one less.
static constexpr uint32_t FAT32_MAX_CLUSTERS = 0x0FFFFFEEU;
static constexpr uint32_t FAT32_BAD_CLUSTER = 0x0FFFFFF7U;
static constexpr uint32_t FAT32_EOC_MIN = 0x0FFFFFF8U;
static constexpr uint32_t FAT32_VALUE_MASK = 0x0FFFFFFFU;
static constexpr uint8_t ATTRIBUTE_DIRECTORY = 0x10U;
static constexpr uint8_t ATTRIBUTE_VOLUME_LABEL = 0x08U;
static constexpr uint8_t ATTRIBUTE_LONG_NAME = 0x0FU;
static constexpr size_t DIRECTORY_ENTRY_SIZE = 32U;
static constexpr size_t ENTRIES_PER_SECTOR =
    SUPPORTED_SECTOR_SIZE / DIRECTORY_ENTRY_SIZE;

uint16_t read_u16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8U);
}

uint32_t read_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8U) |
        (static_cast<uint32_t>(bytes[2]) << 16U) |
        (static_cast<uint32_t>(bytes[3]) << 24U);
}

void write_u16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value & 0xFFU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value & 0xFFU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    bytes[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    bytes[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

bool bytes_equal(const uint8_t* left, const uint8_t* right, size_t size) {
    for (size_t index = 0U; index < size; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

bool power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

bool add_u64(uint64_t left, uint64_t right, uint64_t* output) {
    if (output == nullptr || right > UINT64_MAX - left) {
        return false;
    }
    *output = left + right;
    return true;
}

bool multiply_u64(uint64_t left, uint64_t right, uint64_t* output) {
    if (output == nullptr || (left != 0U && right > UINT64_MAX / left)) {
        return false;
    }
    *output = left * right;
    return true;
}

Status read_device_sector(
    const storage::block::Device* device,
    uint64_t sector,
    uint8_t* destination) {
    if (destination == nullptr) {
        return Status::InvalidArgument;
    }
    const storage::block::Status status = storage::block::read_blocks(
        device,
        sector,
        1U,
        destination,
        SUPPORTED_SECTOR_SIZE);
    return status == storage::block::Status::Ok
        ? Status::Ok
        : Status::BlockDeviceError;
}

Status write_device_sector(
    const storage::block::Device* device,
    uint64_t sector,
    const uint8_t* source) {
    if (source == nullptr) return Status::InvalidArgument;
    const storage::block::Status status = storage::block::write_blocks(
        device, sector, 1U, source, SUPPORTED_SECTOR_SIZE);
    if (status == storage::block::Status::ReadOnly) return Status::ReadOnly;
    return status == storage::block::Status::Ok
        ? Status::Ok
        : Status::BlockDeviceError;
}

Status require_mounted(const FileSystem* filesystem) {
    if (filesystem == nullptr) {
        return Status::InvalidArgument;
    }
    return filesystem->is_mounted && filesystem->device != nullptr
        ? Status::Ok
        : Status::NotMounted;
}

Status read_sector(
    FileSystem* filesystem,
    uint64_t sector,
    uint8_t* destination) {
    const Status mount_status = require_mounted(filesystem);
    if (mount_status != Status::Ok) {
        return mount_status;
    }
    if (sector >= filesystem->geometry.total_sectors) {
        return Status::CorruptChain;
    }
    return read_device_sector(filesystem->device, sector, destination);
}

Status write_sector(
    FileSystem* filesystem,
    uint64_t sector,
    const uint8_t* source) {
    const Status mount_status = require_mounted(filesystem);
    if (mount_status != Status::Ok) {
        return mount_status;
    }
    if (source == nullptr || sector >= filesystem->geometry.total_sectors) {
        return source == nullptr ? Status::InvalidArgument : Status::CorruptChain;
    }
    const storage::block::Status status = storage::block::write_blocks(
        filesystem->device,
        sector,
        1U,
        source,
        SUPPORTED_SECTOR_SIZE);
    if (status == storage::block::Status::ReadOnly) {
        return Status::ReadOnly;
    }
    return status == storage::block::Status::Ok
        ? Status::Ok
        : Status::BlockDeviceError;
}

bool valid_ascii_name_character(char character) {
    const uint8_t value = static_cast<uint8_t>(character);
    return value >= 0x20U && value <= 0x7EU && character != '/' &&
        character != '\\';
}

bool valid_long_name_character(char character) {
    if (!valid_ascii_name_character(character)) {
        return false;
    }
    switch (character) {
        case '"':
        case '*':
        case ':':
        case '<':
        case '>':
        case '?':
        case '|':
            return false;
        default:
            return true;
    }
}

bool valid_short_name_byte(uint8_t value) {
    if (value < 0x21U || value > 0x7EU) {
        return false;
    }
    switch (static_cast<char>(value)) {
        case '"':
        case '*':
        case '+':
        case ',':
        case '.':
        case '/':
        case ':':
        case ';':
        case '<':
        case '=':
        case '>':
        case '?':
        case '[':
        case '\\':
        case ']':
        case '|':
            return false;
        default:
            return true;
    }
}

char ascii_lower(char character) {
    if (character >= 'A' && character <= 'Z') {
        return static_cast<char>(character + ('a' - 'A'));
    }
    return character;
}

bool ascii_case_equal(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    size_t index = 0U;
    while (left[index] != '\0' && right[index] != '\0') {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
        ++index;
    }
    return left[index] == right[index];
}

struct NormalizedPath {
    char components[MAX_PATH_DEPTH][MAX_NAME_LENGTH + 1U];
    size_t depth;
};

Status normalize_path(const char* path, NormalizedPath* output) {
    if (path == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }

    size_t length = 0U;
    while (length <= MAX_PATH_LENGTH && path[length] != '\0') {
        ++length;
    }
    if (length > MAX_PATH_LENGTH) {
        return Status::PathTooLong;
    }
    if (length == 0U) {
        return Status::InvalidPath;
    }

    NormalizedPath normalized{};
    size_t cursor = 0U;
    while (cursor < length) {
        while (cursor < length && path[cursor] == '/') {
            ++cursor;
        }
        if (cursor == length) {
            break;
        }

        const size_t start = cursor;
        while (cursor < length && path[cursor] != '/') {
            if (!valid_ascii_name_character(path[cursor])) {
                const uint8_t value = static_cast<uint8_t>(path[cursor]);
                return value > 0x7FU
                    ? Status::UnsupportedNameEncoding
                    : Status::InvalidPath;
            }
            ++cursor;
        }
        const size_t component_length = cursor - start;
        if (component_length > MAX_NAME_LENGTH) {
            return Status::NameTooLong;
        }

        if (component_length == 1U && path[start] == '.') {
            continue;
        }
        if (component_length == 2U && path[start] == '.' &&
            path[start + 1U] == '.') {
            if (normalized.depth == 0U) {
                return Status::PathEscapesRoot;
            }
            --normalized.depth;
            continue;
        }
        if (normalized.depth >= MAX_PATH_DEPTH) {
            return Status::PathTooDeep;
        }

        char* component = normalized.components[normalized.depth];
        for (size_t index = 0U; index < component_length; ++index) {
            component[index] = path[start + index];
        }
        component[component_length] = '\0';
        ++normalized.depth;
    }

    *output = normalized;
    return Status::Ok;
}

Status cluster_first_sector(
    const FileSystem* filesystem,
    uint32_t cluster,
    uint64_t* output) {
    if (filesystem == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    if (cluster < 2U || cluster > filesystem->geometry.max_cluster) {
        return Status::CorruptChain;
    }

    uint64_t cluster_offset = 0U;
    if (!multiply_u64(
            static_cast<uint64_t>(cluster - 2U),
            static_cast<uint64_t>(filesystem->geometry.sectors_per_cluster),
            &cluster_offset) ||
        !add_u64(
            filesystem->geometry.first_data_sector,
            cluster_offset,
            output)) {
        return Status::ArithmeticOverflow;
    }
    if (*output >= filesystem->geometry.total_sectors ||
        filesystem->geometry.sectors_per_cluster >
            filesystem->geometry.total_sectors - *output) {
        return Status::CorruptChain;
    }
    return Status::Ok;
}

Status read_raw_fat_entry(
    FileSystem* filesystem,
    uint32_t entry_index,
    uint32_t* output) {
    if (filesystem == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }

    const uint64_t byte_offset = static_cast<uint64_t>(entry_index) * 4U;
    const uint64_t sector_offset = byte_offset / SUPPORTED_SECTOR_SIZE;
    const size_t offset_in_sector = static_cast<size_t>(
        byte_offset % SUPPORTED_SECTOR_SIZE);
    if (sector_offset >= filesystem->geometry.sectors_per_fat) {
        return Status::CorruptFat;
    }

    uint8_t reference[SUPPORTED_SECTOR_SIZE]{};
    uint64_t active_offset = 0U;
    if (!multiply_u64(
            static_cast<uint64_t>(filesystem->geometry.active_fat),
            static_cast<uint64_t>(filesystem->geometry.sectors_per_fat),
            &active_offset)) {
        return Status::ArithmeticOverflow;
    }
    uint64_t active_sector = 0U;
    if (!add_u64(
            filesystem->geometry.first_fat_sector,
            active_offset,
            &active_sector) ||
        !add_u64(active_sector, sector_offset, &active_sector)) {
        return Status::ArithmeticOverflow;
    }
    Status status = read_sector(filesystem, active_sector, reference);
    if (status != Status::Ok) {
        return status;
    }

    if (filesystem->geometry.fat_mirroring &&
        filesystem->geometry.fat_count > 1U) {
        uint8_t mirror[SUPPORTED_SECTOR_SIZE]{};
        for (uint32_t copy = 0U;
             copy < filesystem->geometry.fat_count;
             ++copy) {
            if (copy == filesystem->geometry.active_fat) {
                continue;
            }
            uint64_t copy_offset = 0U;
            uint64_t copy_sector = 0U;
            if (!multiply_u64(
                    static_cast<uint64_t>(copy),
                    static_cast<uint64_t>(filesystem->geometry.sectors_per_fat),
                    &copy_offset) ||
                !add_u64(
                    filesystem->geometry.first_fat_sector,
                    copy_offset,
                    &copy_sector) ||
                !add_u64(copy_sector, sector_offset, &copy_sector)) {
                return Status::ArithmeticOverflow;
            }
            status = read_sector(filesystem, copy_sector, mirror);
            if (status != Status::Ok) {
                return status;
            }
            if (!bytes_equal(reference, mirror, SUPPORTED_SECTOR_SIZE)) {
                return Status::FatMirrorMismatch;
            }
        }
    }

    *output = read_u32(reference + offset_in_sector) & FAT32_VALUE_MASK;
    return Status::Ok;
}

Status write_raw_fat_entry(
    FileSystem* filesystem,
    uint32_t entry_index,
    uint32_t value) {
    if (filesystem == nullptr || entry_index > filesystem->geometry.max_cluster ||
        (value & ~FAT32_VALUE_MASK) != 0U) {
        return Status::InvalidArgument;
    }
    const uint64_t byte_offset = static_cast<uint64_t>(entry_index) * 4U;
    const uint64_t sector_offset = byte_offset / SUPPORTED_SECTOR_SIZE;
    const size_t offset_in_sector = static_cast<size_t>(
        byte_offset % SUPPORTED_SECTOR_SIZE);
    if (sector_offset >= filesystem->geometry.sectors_per_fat) {
        return Status::CorruptFat;
    }

    const uint32_t first_copy = filesystem->geometry.fat_mirroring
        ? 0U
        : filesystem->geometry.active_fat;
    const uint32_t copy_count = filesystem->geometry.fat_mirroring
        ? filesystem->geometry.fat_count
        : first_copy + 1U;
    for (uint32_t copy = first_copy; copy < copy_count; ++copy) {
        uint64_t copy_offset = 0U;
        uint64_t sector = 0U;
        if (!multiply_u64(
                static_cast<uint64_t>(copy),
                filesystem->geometry.sectors_per_fat,
                &copy_offset) ||
            !add_u64(filesystem->geometry.first_fat_sector, copy_offset, &sector) ||
            !add_u64(sector, sector_offset, &sector)) {
            return Status::ArithmeticOverflow;
        }
        uint8_t bytes[SUPPORTED_SECTOR_SIZE]{};
        Status status = read_sector(filesystem, sector, bytes);
        if (status != Status::Ok) {
            return status;
        }
        const uint32_t preserved = read_u32(bytes + offset_in_sector) &
            ~FAT32_VALUE_MASK;
        write_u32(bytes + offset_in_sector, preserved | value);
        status = write_sector(filesystem, sector, bytes);
        if (status != Status::Ok) {
            return status;
        }
    }
    return Status::Ok;
}

Status invalidate_fs_info_hints(FileSystem* filesystem) {
    if (filesystem->fs_info_hints_invalidated) {
        return Status::Ok;
    }
    uint8_t primary[SUPPORTED_SECTOR_SIZE]{};
    Status status = read_sector(
        filesystem,
        filesystem->geometry.fs_info_sector,
        primary);
    if (status != Status::Ok) {
        return status;
    }
    write_u32(primary + 488U, 0xFFFFFFFFU);
    write_u32(primary + 492U, 0xFFFFFFFFU);
    status = write_sector(
        filesystem,
        filesystem->geometry.fs_info_sector,
        primary);
    if (status != Status::Ok) {
        return status;
    }

    const uint32_t backup_info =
        static_cast<uint32_t>(filesystem->geometry.backup_boot_sector) +
        filesystem->geometry.fs_info_sector;
    if (backup_info < filesystem->geometry.reserved_sectors) {
        status = write_sector(filesystem, backup_info, primary);
    }
    if (status == Status::Ok) {
        filesystem->fs_info_hints_invalidated = true;
    }
    return status;
}

Status zero_cluster(FileSystem* filesystem, uint32_t cluster) {
    uint64_t first_sector = 0U;
    Status status = cluster_first_sector(filesystem, cluster, &first_sector);
    if (status != Status::Ok) {
        return status;
    }
    const uint8_t zero[SUPPORTED_SECTOR_SIZE]{};
    for (uint32_t index = 0U;
         index < filesystem->geometry.sectors_per_cluster;
         ++index) {
        status = write_sector(filesystem, first_sector + index, zero);
        if (status != Status::Ok) {
            return status;
        }
    }
    return Status::Ok;
}

Status allocate_cluster(FileSystem* filesystem, uint32_t* output) {
    if (filesystem == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    uint32_t cluster = filesystem->allocation_hint;
    if (cluster < 2U || cluster > filesystem->geometry.max_cluster) {
        cluster = 2U;
    }
    for (uint32_t scanned = 0U;
         scanned < filesystem->geometry.cluster_count;
         ++scanned) {
        uint32_t value = 0U;
        Status status = read_raw_fat_entry(filesystem, cluster, &value);
        if (status != Status::Ok) {
            return status;
        }
        if (value == 0U) {
            status = invalidate_fs_info_hints(filesystem);
            if (status != Status::Ok) {
                return status;
            }
            status = write_raw_fat_entry(
                filesystem, cluster, FAT32_EOC_MIN);
            if (status != Status::Ok) {
                return status;
            }
            status = zero_cluster(filesystem, cluster);
            if (status != Status::Ok) {
                static_cast<void>(write_raw_fat_entry(
                    filesystem, cluster, 0U));
                return status;
            }
            filesystem->allocation_hint =
                cluster == filesystem->geometry.max_cluster
                    ? 2U
                    : cluster + 1U;
            *output = cluster;
            return Status::Ok;
        }
        cluster = cluster == filesystem->geometry.max_cluster
            ? 2U
            : cluster + 1U;
    }
    return Status::NoSpace;
}

Status fat_next(
    FileSystem* filesystem,
    uint32_t cluster,
    uint32_t* next,
    bool* end_of_chain) {
    if (next == nullptr || end_of_chain == nullptr) {
        return Status::InvalidArgument;
    }
    if (cluster < 2U || cluster > filesystem->geometry.max_cluster) {
        return Status::CorruptChain;
    }

    uint32_t value = 0U;
    const Status status = read_raw_fat_entry(filesystem, cluster, &value);
    if (status != Status::Ok) {
        return status;
    }
    if (value >= FAT32_EOC_MIN) {
        *next = 0U;
        *end_of_chain = true;
        return Status::Ok;
    }
    if (value == 0U || value == 1U || value == FAT32_BAD_CLUSTER ||
        (value >= 0x0FFFFFF0U && value <= 0x0FFFFFF6U) ||
        value < 2U || value > filesystem->geometry.max_cluster) {
        return Status::CorruptChain;
    }

    *next = value;
    *end_of_chain = false;
    return Status::Ok;
}

struct ChainWalker {
    uint32_t current;
    uint32_t tortoise;
    uint64_t power;
    uint64_t lambda;
    uint64_t hops;
};

Status initialize_walker(
    const FileSystem* filesystem,
    uint32_t start_cluster,
    ChainWalker* output) {
    if (filesystem == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    if (start_cluster < 2U ||
        start_cluster > filesystem->geometry.max_cluster) {
        return Status::CorruptChain;
    }
    output->current = start_cluster;
    output->tortoise = start_cluster;
    output->power = 1U;
    output->lambda = 0U;
    output->hops = 0U;
    return Status::Ok;
}

// Brent cycle detection reuses the next cluster already needed by the caller;
// unlike tortoise/hare look-ahead it never performs I/O beyond the requested
// point in a chain.
Status advance_walker(
    FileSystem* filesystem,
    ChainWalker* walker,
    bool* end_of_chain) {
    if (walker == nullptr || end_of_chain == nullptr) {
        return Status::InvalidArgument;
    }

    uint32_t next = 0U;
    bool end = false;
    const Status status = fat_next(
        filesystem,
        walker->current,
        &next,
        &end);
    if (status != Status::Ok) {
        return status;
    }
    if (end) {
        *end_of_chain = true;
        return Status::Ok;
    }

    ++walker->hops;
    ++walker->lambda;
    walker->current = next;
    if (walker->current == walker->tortoise ||
        walker->hops >= filesystem->geometry.cluster_count) {
        return Status::ChainCycle;
    }
    if (walker->lambda == walker->power) {
        walker->tortoise = walker->current;
        walker->lambda = 0U;
        if (walker->power <= UINT64_MAX / 2U) {
            walker->power *= 2U;
        }
    }

    *end_of_chain = false;
    return Status::Ok;
}

uint8_t short_name_checksum(const uint8_t* short_name) {
    uint8_t checksum = 0U;
    for (size_t index = 0U; index < 11U; ++index) {
        checksum = static_cast<uint8_t>(
            ((checksum & 1U) != 0U ? 0x80U : 0U) +
            (checksum >> 1U) + short_name[index]);
    }
    return checksum;
}

struct LongNameState {
    uint16_t units[260];
    uint8_t checksum;
    uint8_t expected_sequence;
    uint8_t sequence_count;
    bool active;
};

void reset_long_name(LongNameState* state) {
    if (state == nullptr) {
        return;
    }
    state->checksum = 0U;
    state->expected_sequence = 0U;
    state->sequence_count = 0U;
    state->active = false;
}

void initialize_long_name_units(LongNameState* state) {
    for (size_t index = 0U; index < 260U; ++index) {
        state->units[index] = 0xFFFFU;
    }
}

void consume_long_name_entry(
    const uint8_t* entry,
    LongNameState* state) {
    const uint8_t ordinal = entry[0];
    const uint8_t sequence = static_cast<uint8_t>(ordinal & 0x1FU);
    const bool last = (ordinal & 0x40U) != 0U;
    if ((ordinal & 0xA0U) != 0U || sequence == 0U || sequence > 20U ||
        entry[12] != 0U || read_u16(entry + 26U) != 0U) {
        reset_long_name(state);
        return;
    }

    if (last) {
        initialize_long_name_units(state);
        state->active = true;
        state->checksum = entry[13];
        state->expected_sequence = sequence;
        state->sequence_count = sequence;
    } else if (!state->active || state->expected_sequence != sequence ||
               state->checksum != entry[13]) {
        reset_long_name(state);
        return;
    }

    if (!state->active || state->expected_sequence != sequence) {
        reset_long_name(state);
        return;
    }

    static constexpr uint8_t OFFSETS[13] = {
        1U, 3U, 5U, 7U, 9U,
        14U, 16U, 18U, 20U, 22U, 24U,
        28U, 30U
    };
    const size_t base = static_cast<size_t>(sequence - 1U) * 13U;
    for (size_t index = 0U; index < 13U; ++index) {
        state->units[base + index] = read_u16(entry + OFFSETS[index]);
    }
    --state->expected_sequence;
}

enum class LongNameResult : uint8_t {
    MissingOrMalformed = 0,
    Valid,
    UnsupportedEncoding
};

LongNameResult decode_long_name(
    const LongNameState* state,
    const uint8_t* short_entry,
    char* output,
    size_t* output_length) {
    if (state == nullptr || short_entry == nullptr || output == nullptr ||
        output_length == nullptr || !state->active ||
        state->expected_sequence != 0U ||
        state->checksum != short_name_checksum(short_entry)) {
        return LongNameResult::MissingOrMalformed;
    }

    const size_t unit_count = static_cast<size_t>(state->sequence_count) * 13U;
    bool terminated = false;
    bool unsupported = false;
    size_t length = 0U;
    for (size_t index = 0U; index < unit_count; ++index) {
        const uint16_t unit = state->units[index];
        if (terminated) {
            if (unit != 0xFFFFU) {
                return LongNameResult::MissingOrMalformed;
            }
            continue;
        }
        if (unit == 0U) {
            terminated = true;
            continue;
        }
        if (unit == 0xFFFFU || unit == 0xFFFEU) {
            return LongNameResult::MissingOrMalformed;
        }
        if (length >= MAX_NAME_LENGTH) {
            return LongNameResult::MissingOrMalformed;
        }

        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            if (index + 1U >= unit_count) {
                return LongNameResult::MissingOrMalformed;
            }
            const uint16_t low = state->units[index + 1U];
            if (low < 0xDC00U || low > 0xDFFFU) {
                return LongNameResult::MissingOrMalformed;
            }
            unsupported = true;
            ++index;
            ++length;
            continue;
        }
        if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            return LongNameResult::MissingOrMalformed;
        }
        if (unit > 0x7FU) {
            unsupported = true;
            ++length;
            continue;
        }

        const char character = static_cast<char>(unit);
        if (!valid_long_name_character(character)) {
            return LongNameResult::MissingOrMalformed;
        }
        output[length] = character;
        ++length;
    }

    if (length == 0U || (!terminated && unit_count > MAX_NAME_LENGTH)) {
        return LongNameResult::MissingOrMalformed;
    }
    output[length] = '\0';
    *output_length = length;
    return unsupported
        ? LongNameResult::UnsupportedEncoding
        : LongNameResult::Valid;
}

Status decode_short_name(
    const uint8_t* entry,
    char* output,
    size_t* output_length) {
    if (entry == nullptr || output == nullptr || output_length == nullptr) {
        return Status::InvalidArgument;
    }

    size_t base_length = 8U;
    while (base_length > 0U && entry[base_length - 1U] == ' ') {
        --base_length;
    }
    size_t extension_length = 3U;
    while (extension_length > 0U && entry[8U + extension_length - 1U] == ' ') {
        --extension_length;
    }
    if (base_length == 0U) {
        return Status::CorruptDirectory;
    }

    const bool dot_entry =
        (base_length == 1U && entry[0] == '.') ||
        (base_length == 2U && entry[0] == '.' && entry[1] == '.');
    if (dot_entry && extension_length != 0U) {
        return Status::CorruptDirectory;
    }

    size_t length = 0U;
    for (size_t index = 0U; index < base_length; ++index) {
        uint8_t value = entry[index];
        if (index == 0U && value == 0x05U) {
            value = 0xE5U;
        }
        if ((!dot_entry && !valid_short_name_byte(value)) ||
            (dot_entry && value != '.')) {
            return value > 0x7FU
                ? Status::UnsupportedNameEncoding
                : Status::CorruptDirectory;
        }
        char character = static_cast<char>(value);
        if ((entry[12] & 0x08U) != 0U && character >= 'A' && character <= 'Z') {
            character = ascii_lower(character);
        }
        output[length++] = character;
    }
    if (extension_length > 0U) {
        output[length++] = '.';
        for (size_t index = 0U; index < extension_length; ++index) {
            const uint8_t value = entry[8U + index];
            if (!valid_short_name_byte(value)) {
                return value > 0x7FU
                    ? Status::UnsupportedNameEncoding
                    : Status::CorruptDirectory;
            }
            char character = static_cast<char>(value);
            if ((entry[12] & 0x10U) != 0U && character >= 'A' &&
                character <= 'Z') {
                character = ascii_lower(character);
            }
            output[length++] = character;
        }
    }
    output[length] = '\0';
    *output_length = length;
    return Status::Ok;
}

Status node_from_directory_entry(const uint8_t* entry, Node* output) {
    if (entry == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    Node node{};
    node.attributes = entry[11];
    node.type = (node.attributes & ATTRIBUTE_DIRECTORY) != 0U
        ? EntryType::Directory
        : EntryType::File;
    node.first_cluster =
        (static_cast<uint32_t>(read_u16(entry + 20U)) << 16U) |
        static_cast<uint32_t>(read_u16(entry + 26U));
    node.size = static_cast<uint64_t>(read_u32(entry + 28U));
    *output = node;
    return Status::Ok;
}

struct ScannedEntry {
    char name[MAX_NAME_LENGTH + 1U];
    size_t name_length;
    char short_name[13];
    Node node;
    bool unsupported_long_name;
};

Status build_scanned_entry(
    const uint8_t* directory_entry,
    const LongNameState* long_name,
    ScannedEntry* output) {
    if (directory_entry == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }

    ScannedEntry result{};
    size_t short_length = 0U;
    const Status short_status = decode_short_name(
        directory_entry,
        result.short_name,
        &short_length);
    if (short_status != Status::Ok) {
        return short_status;
    }

    size_t long_length = 0U;
    const LongNameResult long_result = decode_long_name(
        long_name,
        directory_entry,
        result.name,
        &long_length);
    if (long_result == LongNameResult::Valid) {
        result.name_length = long_length;
    } else {
        for (size_t index = 0U; index <= short_length; ++index) {
            result.name[index] = result.short_name[index];
        }
        result.name_length = short_length;
        result.unsupported_long_name =
            long_result == LongNameResult::UnsupportedEncoding;
    }

    const Status node_status = node_from_directory_entry(
        directory_entry,
        &result.node);
    if (node_status != Status::Ok) {
        return node_status;
    }
    *output = result;
    return Status::Ok;
}

enum class ScanMode : uint8_t {
    Lookup = 0,
    VisibleIndex
};

Status scan_directory(
    FileSystem* filesystem,
    const Node* directory,
    ScanMode mode,
    const char* lookup_name,
    uint64_t desired_index,
    ScannedEntry* output,
    bool* selected_unsupported_name) {
    if (filesystem == nullptr || directory == nullptr || output == nullptr ||
        selected_unsupported_name == nullptr) {
        return Status::InvalidArgument;
    }
    if (directory->type != EntryType::Directory) {
        return Status::NotDirectory;
    }

    ChainWalker walker{};
    Status status = initialize_walker(
        filesystem,
        directory->first_cluster,
        &walker);
    if (status != Status::Ok) {
        return status;
    }

    LongNameState long_name{};
    reset_long_name(&long_name);
    uint64_t visible_index = 0U;
    uint8_t sector[SUPPORTED_SECTOR_SIZE]{};
    for (;;) {
        uint64_t first_sector = 0U;
        status = cluster_first_sector(
            filesystem,
            walker.current,
            &first_sector);
        if (status != Status::Ok) {
            return status;
        }

        for (uint32_t sector_index = 0U;
             sector_index < filesystem->geometry.sectors_per_cluster;
             ++sector_index) {
            status = read_sector(
                filesystem,
                first_sector + static_cast<uint64_t>(sector_index),
                sector);
            if (status != Status::Ok) {
                return status;
            }

            for (size_t entry_index = 0U;
                 entry_index < ENTRIES_PER_SECTOR;
                 ++entry_index) {
                const uint8_t* entry = sector +
                    entry_index * DIRECTORY_ENTRY_SIZE;
                if (entry[0] == 0U) {
                    return mode == ScanMode::Lookup
                        ? Status::NotFound
                        : Status::EndOfDirectory;
                }
                if (entry[0] == 0xE5U) {
                    reset_long_name(&long_name);
                    continue;
                }
                if (entry[11] == ATTRIBUTE_LONG_NAME) {
                    consume_long_name_entry(entry, &long_name);
                    continue;
                }
                if ((entry[11] & ATTRIBUTE_VOLUME_LABEL) != 0U) {
                    reset_long_name(&long_name);
                    continue;
                }

                ScannedEntry scanned{};
                status = build_scanned_entry(entry, &long_name, &scanned);
                reset_long_name(&long_name);
                if (status != Status::Ok) {
                    return status;
                }
                const bool dot_entry =
                    scanned.short_name[0] == '.' &&
                    scanned.short_name[1] == '\0';
                const bool dot_dot_entry =
                    scanned.short_name[0] == '.' &&
                    scanned.short_name[1] == '.' &&
                    scanned.short_name[2] == '\0';
                if (dot_entry) {
                    if (scanned.node.type != EntryType::Directory ||
                        scanned.node.size != 0U ||
                        scanned.node.first_cluster != directory->first_cluster) {
                        return Status::CorruptDirectory;
                    }
                    // Special navigation entries are filesystem metadata, not
                    // children exposed by lookup/readdir.
                    continue;
                }
                if (dot_dot_entry) {
                    if (scanned.node.type != EntryType::Directory ||
                        scanned.node.size != 0U ||
                        (scanned.node.first_cluster != 0U &&
                         (scanned.node.first_cluster < 2U ||
                          scanned.node.first_cluster >
                              filesystem->geometry.max_cluster))) {
                        return Status::CorruptDirectory;
                    }
                    // FAT32 encodes a parent that is the root directory as
                    // cluster zero. Path normalization resolves '..' before
                    // traversal, so this entry is never a lookup target.
                    continue;
                }
                if (scanned.node.type == EntryType::Directory) {
                    if (scanned.node.first_cluster < 2U ||
                        scanned.node.first_cluster >
                            filesystem->geometry.max_cluster) {
                        return Status::CorruptDirectory;
                    }
                } else if (scanned.node.size > 0U &&
                           (scanned.node.first_cluster < 2U ||
                            scanned.node.first_cluster >
                                filesystem->geometry.max_cluster)) {
                    return Status::CorruptDirectory;
                }

                bool selected = false;
                if (mode == ScanMode::Lookup) {
                    selected = ascii_case_equal(scanned.name, lookup_name) ||
                        ascii_case_equal(scanned.short_name, lookup_name);
                } else {
                    selected = visible_index == desired_index;
                }
                if (selected) {
                    *output = scanned;
                    *selected_unsupported_name =
                        scanned.unsupported_long_name;
                    return Status::Ok;
                }
                if (visible_index == UINT64_MAX) {
                    return Status::ArithmeticOverflow;
                }
                ++visible_index;
            }
        }

        bool end = false;
        status = advance_walker(filesystem, &walker, &end);
        if (status != Status::Ok) {
            return status;
        }
        if (end) {
            return mode == ScanMode::Lookup
                ? Status::NotFound
                : Status::EndOfDirectory;
        }
    }
}

Status lookup_normalized(
    FileSystem* filesystem,
    const NormalizedPath* path,
    Node* output) {
    if (filesystem == nullptr || path == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    Node current{};
    current.type = EntryType::Directory;
    current.attributes = ATTRIBUTE_DIRECTORY;
    current.first_cluster = filesystem->geometry.root_cluster;
    current.size = 0U;

    for (size_t depth = 0U; depth < path->depth; ++depth) {
        if (current.type != EntryType::Directory) {
            return Status::NotDirectory;
        }
        ScannedEntry selected{};
        bool unsupported = false;
        const Status status = scan_directory(
            filesystem,
            &current,
            ScanMode::Lookup,
            path->components[depth],
            0U,
            &selected,
            &unsupported);
        if (status != Status::Ok) {
            return status;
        }
        (void)unsupported;
        current = selected.node;
    }

    *output = current;
    return Status::Ok;
}

struct DirectorySlot {
    uint64_t sector;
    size_t offset;
    uint8_t preceding_long_entries;
};

Status split_path(
    FileSystem* filesystem,
    const char* path,
    NormalizedPath* normalized,
    Node* parent,
    const char** leaf) {
    if (normalized == nullptr || parent == nullptr || leaf == nullptr) {
        return Status::InvalidArgument;
    }
    Status status = normalize_path(path, normalized);
    if (status != Status::Ok) {
        return status;
    }
    if (normalized->depth == 0U) {
        return Status::InvalidPath;
    }
    NormalizedPath parent_path = *normalized;
    --parent_path.depth;
    status = lookup_normalized(filesystem, &parent_path, parent);
    if (status != Status::Ok) {
        return status;
    }
    if (parent->type != EntryType::Directory) {
        return Status::NotDirectory;
    }
    *leaf = normalized->components[normalized->depth - 1U];
    return Status::Ok;
}

Status locate_directory_entry(
    FileSystem* filesystem,
    const Node* directory,
    const char* name,
    Node* node,
    DirectorySlot* slot) {
    if (filesystem == nullptr || directory == nullptr || name == nullptr ||
        node == nullptr || slot == nullptr) {
        return Status::InvalidArgument;
    }
    ChainWalker walker{};
    Status status = initialize_walker(
        filesystem, directory->first_cluster, &walker);
    if (status != Status::Ok) {
        return status;
    }
    LongNameState long_name{};
    reset_long_name(&long_name);
    uint8_t long_count = 0U;
    uint8_t sector[SUPPORTED_SECTOR_SIZE]{};
    for (;;) {
        uint64_t first_sector = 0U;
        status = cluster_first_sector(filesystem, walker.current, &first_sector);
        if (status != Status::Ok) {
            return status;
        }
        for (uint32_t sector_index = 0U;
             sector_index < filesystem->geometry.sectors_per_cluster;
             ++sector_index) {
            const uint64_t absolute_sector = first_sector + sector_index;
            status = read_sector(filesystem, absolute_sector, sector);
            if (status != Status::Ok) {
                return status;
            }
            for (size_t entry_index = 0U;
                 entry_index < ENTRIES_PER_SECTOR;
                 ++entry_index) {
                const size_t offset = entry_index * DIRECTORY_ENTRY_SIZE;
                const uint8_t* entry = sector + offset;
                if (entry[0] == 0U) {
                    return Status::NotFound;
                }
                if (entry[0] == 0xE5U) {
                    reset_long_name(&long_name);
                    long_count = 0U;
                    continue;
                }
                if (entry[11] == ATTRIBUTE_LONG_NAME) {
                    consume_long_name_entry(entry, &long_name);
                    if (long_count != UINT8_MAX) {
                        ++long_count;
                    }
                    continue;
                }
                if ((entry[11] & ATTRIBUTE_VOLUME_LABEL) != 0U) {
                    reset_long_name(&long_name);
                    long_count = 0U;
                    continue;
                }
                ScannedEntry scanned{};
                status = build_scanned_entry(entry, &long_name, &scanned);
                reset_long_name(&long_name);
                if (status != Status::Ok) {
                    return status;
                }
                if (ascii_case_equal(scanned.name, name) ||
                    ascii_case_equal(scanned.short_name, name)) {
                    *node = scanned.node;
                    slot->sector = absolute_sector;
                    slot->offset = offset;
                    slot->preceding_long_entries = long_count;
                    return Status::Ok;
                }
                long_count = 0U;
            }
        }
        bool end = false;
        status = advance_walker(filesystem, &walker, &end);
        if (status != Status::Ok) {
            return status;
        }
        if (end) {
            return Status::NotFound;
        }
    }
}

Status locate_path_entry(
    FileSystem* filesystem,
    const char* path,
    Node* parent,
    Node* node,
    DirectorySlot* slot,
    NormalizedPath* normalized = nullptr) {
    NormalizedPath local{};
    NormalizedPath* parsed = normalized == nullptr ? &local : normalized;
    const char* leaf = nullptr;
    Status status = split_path(filesystem, path, parsed, parent, &leaf);
    if (status != Status::Ok) {
        return status;
    }
    return locate_directory_entry(filesystem, parent, leaf, node, slot);
}

char ascii_upper(char character) {
    return character >= 'a' && character <= 'z'
        ? static_cast<char>(character - ('a' - 'A'))
        : character;
}

Status encode_short_name(const char* name, uint8_t output[11]) {
    if (name == nullptr || output == nullptr || name[0] == '\0') {
        return Status::InvalidArgument;
    }
    size_t length = 0U;
    size_t dot = SIZE_MAX;
    while (name[length] != '\0') {
        if (name[length] == '.') {
            if (dot != SIZE_MAX) {
                return Status::Unsupported;
            }
            dot = length;
        }
        ++length;
        if (length > 12U) {
            return Status::Unsupported;
        }
    }
    const size_t base_length = dot == SIZE_MAX ? length : dot;
    const size_t extension_length = dot == SIZE_MAX ? 0U : length - dot - 1U;
    if (base_length == 0U || base_length > 8U || extension_length > 3U ||
        (dot != SIZE_MAX && extension_length == 0U)) {
        return Status::Unsupported;
    }
    for (size_t index = 0U; index < 11U; ++index) {
        output[index] = static_cast<uint8_t>(' ');
    }
    for (size_t index = 0U; index < base_length; ++index) {
        const char character = ascii_upper(name[index]);
        const uint8_t value = static_cast<uint8_t>(character);
        if (character == ' ' || !valid_short_name_byte(value)) {
            return Status::Unsupported;
        }
        output[index] = value;
    }
    for (size_t index = 0U; index < extension_length; ++index) {
        const char character = ascii_upper(name[dot + 1U + index]);
        const uint8_t value = static_cast<uint8_t>(character);
        if (character == ' ' || !valid_short_name_byte(value)) {
            return Status::Unsupported;
        }
        output[8U + index] = value;
    }
    return Status::Ok;
}

Status find_free_directory_slot(
    FileSystem* filesystem,
    const Node* directory,
    DirectorySlot* output) {
    if (filesystem == nullptr || directory == nullptr || output == nullptr ||
        directory->type != EntryType::Directory) {
        return Status::InvalidArgument;
    }
    ChainWalker walker{};
    Status status = initialize_walker(
        filesystem, directory->first_cluster, &walker);
    if (status != Status::Ok) {
        return status;
    }
    DirectorySlot deleted{};
    bool has_deleted = false;
    uint8_t sector[SUPPORTED_SECTOR_SIZE]{};
    for (;;) {
        uint64_t first_sector = 0U;
        status = cluster_first_sector(filesystem, walker.current, &first_sector);
        if (status != Status::Ok) {
            return status;
        }
        for (uint32_t sector_index = 0U;
             sector_index < filesystem->geometry.sectors_per_cluster;
             ++sector_index) {
            const uint64_t absolute_sector = first_sector + sector_index;
            status = read_sector(filesystem, absolute_sector, sector);
            if (status != Status::Ok) {
                return status;
            }
            for (size_t index = 0U; index < ENTRIES_PER_SECTOR; ++index) {
                const size_t offset = index * DIRECTORY_ENTRY_SIZE;
                if (sector[offset] == 0U) {
                    *output = has_deleted
                        ? deleted
                        : DirectorySlot{absolute_sector, offset, 0U};
                    return Status::Ok;
                }
                if (!has_deleted && sector[offset] == 0xE5U) {
                    deleted = {absolute_sector, offset, 0U};
                    has_deleted = true;
                }
            }
        }
        bool end = false;
        status = advance_walker(filesystem, &walker, &end);
        if (status != Status::Ok) {
            return status;
        }
        if (!end) {
            continue;
        }
        if (has_deleted) {
            *output = deleted;
            return Status::Ok;
        }
        uint32_t extension = 0U;
        status = allocate_cluster(filesystem, &extension);
        if (status != Status::Ok) {
            return status;
        }
        status = write_raw_fat_entry(filesystem, walker.current, extension);
        if (status != Status::Ok) {
            static_cast<void>(write_raw_fat_entry(filesystem, extension, 0U));
            return status;
        }
        uint64_t extension_sector = 0U;
        status = cluster_first_sector(filesystem, extension, &extension_sector);
        if (status != Status::Ok) {
            return status;
        }
        *output = {extension_sector, 0U, 0U};
        return Status::Ok;
    }
}

Status publish_directory_entry(
    FileSystem* filesystem,
    const DirectorySlot& slot,
    const uint8_t short_name[11],
    uint8_t attributes,
    uint32_t first_cluster,
    uint32_t size) {
    uint8_t sector[SUPPORTED_SECTOR_SIZE]{};
    Status status = read_sector(filesystem, slot.sector, sector);
    if (status != Status::Ok) {
        return status;
    }
    uint8_t* entry = sector + slot.offset;
    for (size_t index = 0U; index < DIRECTORY_ENTRY_SIZE; ++index) {
        entry[index] = 0U;
    }
    for (size_t index = 0U; index < 11U; ++index) {
        entry[index] = short_name[index];
    }
    entry[11] = attributes;
    // A deterministic, valid FAT date is preferable to zeroed month/day
    // fields when no wall-clock timestamp service is supplied to the backend.
    constexpr uint16_t minimum_fat_date = 0x0021U; // 1980-01-01
    write_u16(entry + 16U, minimum_fat_date);
    write_u16(entry + 18U, minimum_fat_date);
    write_u16(entry + 24U, minimum_fat_date);
    write_u16(entry + 20U, static_cast<uint16_t>(first_cluster >> 16U));
    write_u16(entry + 26U, static_cast<uint16_t>(first_cluster & 0xFFFFU));
    write_u32(entry + 28U, size);
    return write_sector(filesystem, slot.sector, sector);
}

Status chain_tail_and_count(
    FileSystem* filesystem,
    uint32_t first_cluster,
    uint32_t* tail,
    uint64_t* count) {
    if (tail == nullptr || count == nullptr) {
        return Status::InvalidArgument;
    }
    ChainWalker walker{};
    Status status = initialize_walker(filesystem, first_cluster, &walker);
    if (status != Status::Ok) {
        return status;
    }
    uint64_t clusters = 1U;
    for (;;) {
        bool end = false;
        status = advance_walker(filesystem, &walker, &end);
        if (status != Status::Ok) {
            return status;
        }
        if (end) {
            *tail = walker.current;
            *count = clusters;
            return Status::Ok;
        }
        if (clusters == UINT64_MAX) {
            return Status::ArithmeticOverflow;
        }
        ++clusters;
    }
}

Status release_chain(FileSystem* filesystem, uint32_t first_cluster) {
    if (first_cluster == 0U) {
        return Status::Ok;
    }
    ChainWalker walker{};
    Status status = initialize_walker(filesystem, first_cluster, &walker);
    if (status != Status::Ok) {
        return status;
    }
    status = invalidate_fs_info_hints(filesystem);
    if (status != Status::Ok) {
        return status;
    }
    for (;;) {
        const uint32_t current = walker.current;
        bool end = false;
        status = advance_walker(filesystem, &walker, &end);
        if (status != Status::Ok) {
            return status;
        }
        status = write_raw_fat_entry(filesystem, current, 0U);
        if (status != Status::Ok) {
            return status;
        }
        if (end) {
            return Status::Ok;
        }
    }
}

Status ensure_file_capacity(
    FileSystem* filesystem,
    Node* node,
    uint64_t required_size) {
    if (filesystem == nullptr || node == nullptr ||
        required_size > UINT32_MAX) {
        return required_size > UINT32_MAX
            ? Status::ArithmeticOverflow
            : Status::InvalidArgument;
    }
    if (required_size == 0U) {
        return Status::Ok;
    }
    const uint64_t cluster_size =
        static_cast<uint64_t>(filesystem->geometry.sectors_per_cluster) *
        SUPPORTED_SECTOR_SIZE;
    const uint64_t required_clusters =
        (required_size + cluster_size - 1U) / cluster_size;
    uint64_t existing_clusters = 0U;
    uint32_t existing_tail = 0U;
    if (node->first_cluster != 0U) {
        Status status = chain_tail_and_count(
            filesystem,
            node->first_cluster,
            &existing_tail,
            &existing_clusters);
        if (status != Status::Ok) {
            return status;
        }
    }
    if (existing_clusters >= required_clusters) {
        return Status::Ok;
    }

    uint32_t first_new = 0U;
    uint32_t new_tail = 0U;
    const uint64_t needed = required_clusters - existing_clusters;
    for (uint64_t index = 0U; index < needed; ++index) {
        uint32_t allocated = 0U;
        Status status = allocate_cluster(filesystem, &allocated);
        if (status != Status::Ok) {
            if (first_new != 0U) {
                static_cast<void>(release_chain(filesystem, first_new));
            }
            return status;
        }
        if (first_new == 0U) {
            first_new = allocated;
        } else {
            status = write_raw_fat_entry(filesystem, new_tail, allocated);
            if (status != Status::Ok) {
                static_cast<void>(write_raw_fat_entry(
                    filesystem, allocated, 0U));
                static_cast<void>(release_chain(filesystem, first_new));
                return status;
            }
        }
        new_tail = allocated;
    }

    if (node->first_cluster == 0U) {
        node->first_cluster = first_new;
        return Status::Ok;
    }
    const Status link_status = write_raw_fat_entry(
        filesystem, existing_tail, first_new);
    if (link_status != Status::Ok) {
        static_cast<void>(release_chain(filesystem, first_new));
        return link_status;
    }
    return Status::Ok;
}

Status write_chain_bytes(
    FileSystem* filesystem,
    const Node* node,
    uint64_t offset,
    const uint8_t* source,
    size_t size) {
    if (size == 0U) {
        return Status::Ok;
    }
    if (filesystem == nullptr || node == nullptr || node->first_cluster < 2U ||
        node->first_cluster > filesystem->geometry.max_cluster) {
        return Status::InvalidArgument;
    }
    const uint64_t cluster_size =
        static_cast<uint64_t>(filesystem->geometry.sectors_per_cluster) *
        SUPPORTED_SECTOR_SIZE;
    uint64_t clusters_to_skip = offset / cluster_size;
    uint64_t offset_in_cluster = offset % cluster_size;
    ChainWalker walker{};
    Status status = initialize_walker(filesystem, node->first_cluster, &walker);
    if (status != Status::Ok) {
        return status;
    }
    while (clusters_to_skip != 0U) {
        bool end = false;
        status = advance_walker(filesystem, &walker, &end);
        if (status != Status::Ok) {
            return status;
        }
        if (end) {
            return Status::TruncatedChain;
        }
        --clusters_to_skip;
    }

    size_t completed = 0U;
    uint8_t sector[SUPPORTED_SECTOR_SIZE]{};
    while (completed < size) {
        uint64_t first_sector = 0U;
        status = cluster_first_sector(filesystem, walker.current, &first_sector);
        if (status != Status::Ok) {
            return status;
        }
        const uint64_t sector_index = offset_in_cluster / SUPPORTED_SECTOR_SIZE;
        const size_t offset_in_sector = static_cast<size_t>(
            offset_in_cluster % SUPPORTED_SECTOR_SIZE);
        size_t chunk = SUPPORTED_SECTOR_SIZE - offset_in_sector;
        if (chunk > size - completed) {
            chunk = size - completed;
        }
        const uint64_t absolute_sector = first_sector + sector_index;
        if (offset_in_sector != 0U || chunk != SUPPORTED_SECTOR_SIZE) {
            status = read_sector(filesystem, absolute_sector, sector);
            if (status != Status::Ok) {
                return status;
            }
        }
        for (size_t index = 0U; index < chunk; ++index) {
            sector[offset_in_sector + index] = source == nullptr
                ? 0U
                : source[completed + index];
        }
        status = write_sector(filesystem, absolute_sector, sector);
        if (status != Status::Ok) {
            return status;
        }
        completed += chunk;
        offset_in_cluster += chunk;
        if (completed < size && offset_in_cluster == cluster_size) {
            bool end = false;
            status = advance_walker(filesystem, &walker, &end);
            if (status != Status::Ok) {
                return status;
            }
            if (end) {
                return Status::TruncatedChain;
            }
            offset_in_cluster = 0U;
        }
    }
    return Status::Ok;
}

Status update_directory_metadata(
    FileSystem* filesystem,
    const DirectorySlot& slot,
    const Node& node) {
    uint8_t sector[SUPPORTED_SECTOR_SIZE]{};
    Status status = read_sector(filesystem, slot.sector, sector);
    if (status != Status::Ok) {
        return status;
    }
    uint8_t* entry = sector + slot.offset;
    if (entry[0] == 0U || entry[0] == 0xE5U ||
        entry[11] == ATTRIBUTE_LONG_NAME) {
        return Status::CorruptDirectory;
    }
    write_u16(
        entry + 20U,
        static_cast<uint16_t>(node.first_cluster >> 16U));
    write_u16(
        entry + 26U,
        static_cast<uint16_t>(node.first_cluster & 0xFFFFU));
    write_u32(entry + 28U, static_cast<uint32_t>(node.size));
    return write_sector(filesystem, slot.sector, sector);
}

Status retire_directory_entry(
    FileSystem* filesystem,
    const DirectorySlot& slot) {
    if (slot.preceding_long_entries != 0U) {
        return Status::Unsupported;
    }
    uint8_t sector[SUPPORTED_SECTOR_SIZE]{};
    Status status = read_sector(filesystem, slot.sector, sector);
    if (status != Status::Ok) {
        return status;
    }
    uint8_t* entry = sector + slot.offset;
    if (entry[0] == 0U || entry[0] == 0xE5U) {
        return Status::NotFound;
    }
    entry[0] = 0xE5U;
    return write_sector(filesystem, slot.sector, sector);
}

Stat make_stat(const Node& node) {
    Stat info{};
    info.type = node.type;
    info.attributes = node.attributes;
    info.first_cluster = node.first_cluster;
    info.size = node.size;
    return info;
}

bool boot_geometry_matches(const uint8_t* primary, const uint8_t* backup) {
    // Compare the complete BPB/extended-BPB geometry through the backup-sector
    // pointer.  Bootstrap code, OEM text, and volume-identification text are
    // intentionally not geometry.
    return bytes_equal(primary + 11U, backup + 11U, 41U) &&
        backup[510] == 0x55U && backup[511] == 0xAAU;
}

} // namespace

Status format(
    const storage::block::Device* device,
    const char* volume_label,
    uint8_t sectors_per_cluster,
    uint32_t hidden_sectors) {
    if (device == nullptr || volume_label == nullptr ||
        storage::block::validate(device) != storage::block::Status::Ok) {
        return Status::InvalidArgument;
    }
    if (device->sector_size != SUPPORTED_SECTOR_SIZE) {
        return Status::UnsupportedSectorSize;
    }
    if (!power_of_two(sectors_per_cluster) || sectors_per_cluster > 128U ||
        device->sector_count > UINT32_MAX || device->sector_count < 66581U) {
        return Status::UnsupportedGeometry;
    }
    size_t label_length = 0U;
    while (volume_label[label_length] != '\0') {
        const uint8_t value = static_cast<uint8_t>(volume_label[label_length]);
        if (label_length >= 11U || value < 0x20U || value > 0x7EU) {
            return Status::InvalidArgument;
        }
        ++label_length;
    }

    constexpr uint32_t reserved_sectors = 32U;
    constexpr uint32_t fat_count = 2U;
    const uint32_t total_sectors = static_cast<uint32_t>(device->sector_count);
    uint32_t sectors_per_fat = 1U;
    uint32_t cluster_count = 0U;
    for (size_t iteration = 0U; iteration < 32U; ++iteration) {
        const uint64_t metadata = reserved_sectors +
            static_cast<uint64_t>(fat_count) * sectors_per_fat;
        if (metadata >= total_sectors) return Status::UnsupportedGeometry;
        cluster_count = static_cast<uint32_t>(
            (total_sectors - metadata) / sectors_per_cluster);
        const uint64_t fat_bytes =
            (static_cast<uint64_t>(cluster_count) + 2U) * 4U;
        const uint32_t required = static_cast<uint32_t>(
            (fat_bytes + SUPPORTED_SECTOR_SIZE - 1U) /
            SUPPORTED_SECTOR_SIZE);
        // A slightly oversized FAT is valid. Accept it instead of oscillating
        // forever for geometries whose exact fixed point alternates by one
        // sector (notably a 64 MiB ESP with one sector per cluster).
        if (required <= sectors_per_fat) break;
        sectors_per_fat = required;
        if (iteration == 31U) return Status::UnsupportedGeometry;
    }
    // Minimize the valid upper bound. This preserves the exact FAT32 minimum
    // geometry used by the host tests while still avoiding one-sector
    // oscillation for the 64 MiB ESP geometry.
    while (sectors_per_fat > 1U) {
        const uint32_t smaller = sectors_per_fat - 1U;
        const uint64_t metadata = reserved_sectors +
            static_cast<uint64_t>(fat_count) * smaller;
        if (metadata >= total_sectors) break;
        const uint32_t smaller_clusters = static_cast<uint32_t>(
            (total_sectors - metadata) / sectors_per_cluster);
        const uint32_t smaller_required = static_cast<uint32_t>(
            ((static_cast<uint64_t>(smaller_clusters) + 2U) * 4U +
             SUPPORTED_SECTOR_SIZE - 1U) / SUPPORTED_SECTOR_SIZE);
        if (smaller < smaller_required) break;
        sectors_per_fat = smaller;
    }
    const uint64_t first_data = reserved_sectors +
        static_cast<uint64_t>(fat_count) * sectors_per_fat;
    cluster_count = static_cast<uint32_t>(
        (total_sectors - first_data) / sectors_per_cluster);
    if (cluster_count < FAT32_MIN_CLUSTERS ||
        cluster_count > FAT32_MAX_CLUSTERS) {
        return Status::UnsupportedGeometry;
    }

    uint8_t zero[SUPPORTED_SECTOR_SIZE]{};
    for (uint32_t sector = 0U; sector < reserved_sectors; ++sector) {
        if (write_device_sector(device, sector, zero) != Status::Ok) {
            return Status::BlockDeviceError;
        }
    }
    for (uint32_t copy = 0U; copy < fat_count; ++copy) {
        const uint64_t fat_start = reserved_sectors +
            static_cast<uint64_t>(copy) * sectors_per_fat;
        for (uint32_t sector = 0U; sector < sectors_per_fat; ++sector) {
            if (write_device_sector(device, fat_start + sector, zero) !=
                Status::Ok) {
                return Status::BlockDeviceError;
            }
        }
    }
    for (uint8_t sector = 0U; sector < sectors_per_cluster; ++sector) {
        if (write_device_sector(device, first_data + sector, zero) !=
            Status::Ok) {
            return Status::BlockDeviceError;
        }
    }

    uint8_t boot[SUPPORTED_SECTOR_SIZE]{};
    boot[0U] = 0xEBU;
    boot[1U] = 0x58U;
    boot[2U] = 0x90U;
    constexpr char oem[] = "KUROGANE";
    for (size_t index = 0U; index < 8U; ++index) boot[3U + index] = oem[index];
    write_u16(boot + 11U, SUPPORTED_SECTOR_SIZE);
    boot[13U] = sectors_per_cluster;
    write_u16(boot + 14U, reserved_sectors);
    boot[16U] = fat_count;
    write_u16(boot + 17U, 0U);
    write_u16(boot + 19U, 0U);
    boot[21U] = 0xF8U;
    write_u16(boot + 22U, 0U);
    write_u16(boot + 24U, 63U);
    write_u16(boot + 26U, 255U);
    write_u32(boot + 28U, hidden_sectors);
    write_u32(boot + 32U, total_sectors);
    write_u32(boot + 36U, sectors_per_fat);
    write_u16(boot + 40U, 0U);
    write_u16(boot + 42U, 0U);
    write_u32(boot + 44U, 2U);
    write_u16(boot + 48U, 1U);
    write_u16(boot + 50U, 6U);
    boot[64U] = 0x80U;
    boot[66U] = 0x29U;
    write_u32(boot + 67U, UINT32_C(0x4B55524F) ^ total_sectors);
    for (size_t index = 0U; index < 11U; ++index) {
        boot[71U + index] = index < label_length
            ? static_cast<uint8_t>(volume_label[index])
            : static_cast<uint8_t>(' ');
    }
    constexpr char fat_name[] = "FAT32   ";
    for (size_t index = 0U; index < 8U; ++index) {
        boot[82U + index] = fat_name[index];
    }
    boot[510U] = 0x55U;
    boot[511U] = 0xAAU;
    if (write_device_sector(device, 0U, boot) != Status::Ok ||
        write_device_sector(device, 6U, boot) != Status::Ok) {
        return Status::BlockDeviceError;
    }

    uint8_t fs_info[SUPPORTED_SECTOR_SIZE]{};
    write_u32(fs_info, UINT32_C(0x41615252));
    write_u32(fs_info + 484U, UINT32_C(0x61417272));
    write_u32(fs_info + 488U, cluster_count - 1U);
    write_u32(fs_info + 492U, 3U);
    write_u32(fs_info + 508U, UINT32_C(0xAA550000));
    if (write_device_sector(device, 1U, fs_info) != Status::Ok ||
        write_device_sector(device, 7U, fs_info) != Status::Ok) {
        return Status::BlockDeviceError;
    }

    uint8_t fat_first[SUPPORTED_SECTOR_SIZE]{};
    write_u32(fat_first, UINT32_C(0x0FFFFFF8));
    write_u32(fat_first + 4U, UINT32_C(0x0FFFFFFF));
    write_u32(fat_first + 8U, UINT32_C(0x0FFFFFFF));
    for (uint32_t copy = 0U; copy < fat_count; ++copy) {
        const uint64_t first = reserved_sectors +
            static_cast<uint64_t>(copy) * sectors_per_fat;
        if (write_device_sector(device, first, fat_first) != Status::Ok) {
            return Status::BlockDeviceError;
        }
    }
    return storage::block::flush(device) == storage::block::Status::Ok
        ? Status::Ok
        : Status::BlockDeviceError;
}

Status mount(FileSystem* output, const storage::block::Device* device) {
    if (output == nullptr || device == nullptr) {
        return Status::InvalidArgument;
    }
    const storage::block::Status device_status = storage::block::validate(device);
    if (device_status != storage::block::Status::Ok) {
        return Status::BlockDeviceError;
    }
    if (device->sector_size != SUPPORTED_SECTOR_SIZE) {
        return Status::UnsupportedSectorSize;
    }

    uint8_t boot[SUPPORTED_SECTOR_SIZE]{};
    Status status = read_device_sector(device, 0U, boot);
    if (status != Status::Ok) {
        return status;
    }
    const bool valid_jump =
        boot[0] == 0xE9U || (boot[0] == 0xEBU && boot[2] == 0x90U);
    if (!valid_jump || boot[510] != 0x55U || boot[511] != 0xAAU) {
        return Status::InvalidBootSector;
    }

    const uint16_t bytes_per_sector = read_u16(boot + 11U);
    const uint8_t sectors_per_cluster = boot[13];
    const uint16_t reserved_sectors = read_u16(boot + 14U);
    const uint8_t fat_count = boot[16];
    const uint16_t root_entry_count = read_u16(boot + 17U);
    const uint16_t total_sectors_16 = read_u16(boot + 19U);
    const uint8_t media = boot[21];
    const uint16_t sectors_per_fat_16 = read_u16(boot + 22U);
    const uint32_t total_sectors_32 = read_u32(boot + 32U);
    const uint32_t sectors_per_fat_32 = read_u32(boot + 36U);
    const uint16_t extended_flags = read_u16(boot + 40U);
    const uint16_t filesystem_version = read_u16(boot + 42U);
    const uint32_t root_cluster = read_u32(boot + 44U);
    const uint16_t fs_info_sector = read_u16(boot + 48U);
    const uint16_t backup_boot_sector = read_u16(boot + 50U);

    if (bytes_per_sector != SUPPORTED_SECTOR_SIZE) {
        return Status::UnsupportedSectorSize;
    }
    if (!power_of_two(sectors_per_cluster) || sectors_per_cluster > 128U ||
        reserved_sectors == 0U || fat_count == 0U ||
        root_entry_count != 0U || total_sectors_16 != 0U ||
        sectors_per_fat_16 != 0U || total_sectors_32 == 0U ||
        sectors_per_fat_32 == 0U || filesystem_version != 0U) {
        return Status::UnsupportedGeometry;
    }
    if (media != 0xF0U && media < 0xF8U) {
        return Status::InvalidBootSector;
    }
    if (static_cast<uint64_t>(total_sectors_32) > device->sector_count) {
        return Status::UnsupportedGeometry;
    }

    uint64_t fat_area = 0U;
    uint64_t first_data_sector = 0U;
    if (!multiply_u64(
            static_cast<uint64_t>(fat_count),
            static_cast<uint64_t>(sectors_per_fat_32),
            &fat_area) ||
        !add_u64(
            static_cast<uint64_t>(reserved_sectors),
            fat_area,
            &first_data_sector)) {
        return Status::ArithmeticOverflow;
    }
    if (first_data_sector >= total_sectors_32) {
        return Status::UnsupportedGeometry;
    }
    const uint64_t data_sectors =
        static_cast<uint64_t>(total_sectors_32) - first_data_sector;
    const uint64_t cluster_count_64 = data_sectors / sectors_per_cluster;
    if (cluster_count_64 < FAT32_MIN_CLUSTERS ||
        cluster_count_64 > FAT32_MAX_CLUSTERS) {
        return Status::UnsupportedGeometry;
    }
    const uint32_t cluster_count = static_cast<uint32_t>(cluster_count_64);
    const uint32_t max_cluster = cluster_count + 1U;

    uint64_t fat_entry_capacity = 0U;
    if (!multiply_u64(
            static_cast<uint64_t>(sectors_per_fat_32),
            SUPPORTED_SECTOR_SIZE / 4U,
            &fat_entry_capacity)) {
        return Status::ArithmeticOverflow;
    }
    if (fat_entry_capacity <= max_cluster || root_cluster < 2U ||
        root_cluster > max_cluster) {
        return Status::UnsupportedGeometry;
    }
    if (fs_info_sector == 0U || fs_info_sector == 0xFFFFU ||
        fs_info_sector >= reserved_sectors || backup_boot_sector == 0U ||
        backup_boot_sector == 0xFFFFU ||
        backup_boot_sector >= reserved_sectors ||
        backup_boot_sector == fs_info_sector) {
        return Status::UnsupportedGeometry;
    }

    const bool mirroring = (extended_flags & 0x0080U) == 0U;
    const uint8_t active_fat = mirroring
        ? 0U
        : static_cast<uint8_t>(extended_flags & 0x000FU);
    if (active_fat >= fat_count) {
        return Status::UnsupportedGeometry;
    }

    uint8_t fs_info[SUPPORTED_SECTOR_SIZE]{};
    status = read_device_sector(device, fs_info_sector, fs_info);
    if (status != Status::Ok) {
        return status;
    }
    if (read_u32(fs_info) != 0x41615252U ||
        read_u32(fs_info + 484U) != 0x61417272U ||
        read_u32(fs_info + 508U) != 0xAA550000U) {
        return Status::CorruptFsInfo;
    }
    const uint32_t free_cluster_count = read_u32(fs_info + 488U);
    const uint32_t next_free_cluster = read_u32(fs_info + 492U);
    if ((free_cluster_count != 0xFFFFFFFFU &&
         free_cluster_count > cluster_count) ||
        (next_free_cluster != 0xFFFFFFFFU &&
         (next_free_cluster < 2U || next_free_cluster > max_cluster))) {
        return Status::CorruptFsInfo;
    }

    uint8_t backup[SUPPORTED_SECTOR_SIZE]{};
    status = read_device_sector(device, backup_boot_sector, backup);
    if (status != Status::Ok) {
        return status;
    }
    if (!boot_geometry_matches(boot, backup)) {
        return Status::CorruptBackupBoot;
    }

    FileSystem candidate{};
    candidate.device = device;
    candidate.geometry.bytes_per_sector = bytes_per_sector;
    candidate.geometry.sectors_per_cluster = sectors_per_cluster;
    candidate.geometry.reserved_sectors = reserved_sectors;
    candidate.geometry.fat_count = fat_count;
    candidate.geometry.sectors_per_fat = sectors_per_fat_32;
    candidate.geometry.total_sectors = total_sectors_32;
    candidate.geometry.first_fat_sector = reserved_sectors;
    candidate.geometry.first_data_sector = first_data_sector;
    candidate.geometry.root_cluster = root_cluster;
    candidate.geometry.cluster_count = cluster_count;
    candidate.geometry.max_cluster = max_cluster;
    candidate.geometry.fs_info_sector = fs_info_sector;
    candidate.geometry.backup_boot_sector = backup_boot_sector;
    candidate.geometry.fat_mirroring = mirroring;
    candidate.geometry.active_fat = active_fat;
    candidate.allocation_hint = next_free_cluster == 0xFFFFFFFFU
        ? 2U
        : next_free_cluster;
    candidate.fs_info_hints_invalidated = false;
    candidate.is_mounted = true;

    candidate.volume_label[0] = '\0';
    if (boot[66] == 0x29U) {
        size_t label_length = 11U;
        while (label_length > 0U && boot[71U + label_length - 1U] == ' ') {
            --label_length;
        }
        for (size_t index = 0U; index < label_length; ++index) {
            const uint8_t value = boot[71U + index];
            if (value < 0x20U || value > 0x7EU) {
                return Status::InvalidBootSector;
            }
            candidate.volume_label[index] = static_cast<char>(value);
        }
        candidate.volume_label[label_length] = '\0';
    }

    // Validate every sector of every enabled mirror before publication.  The
    // same comparison is repeated lazily for sectors touched after mount, so
    // post-mount divergence is not hidden.
    if (mirroring && fat_count > 1U) {
        uint8_t reference[SUPPORTED_SECTOR_SIZE]{};
        uint8_t mirror[SUPPORTED_SECTOR_SIZE]{};
        for (uint32_t fat_sector = 0U;
             fat_sector < sectors_per_fat_32;
             ++fat_sector) {
            status = read_device_sector(
                device,
                static_cast<uint64_t>(reserved_sectors) + fat_sector,
                reference);
            if (status != Status::Ok) {
                return status;
            }
            for (uint32_t copy = 1U; copy < fat_count; ++copy) {
                const uint64_t copy_sector =
                    static_cast<uint64_t>(reserved_sectors) +
                    static_cast<uint64_t>(copy) * sectors_per_fat_32 +
                    fat_sector;
                status = read_device_sector(device, copy_sector, mirror);
                if (status != Status::Ok) {
                    return status;
                }
                if (!bytes_equal(reference, mirror, SUPPORTED_SECTOR_SIZE)) {
                    return Status::FatMirrorMismatch;
                }
            }
        }
    }

    uint32_t fat_zero = 0U;
    uint32_t fat_one = 0U;
    status = read_raw_fat_entry(&candidate, 0U, &fat_zero);
    if (status != Status::Ok) {
        return status;
    }
    status = read_raw_fat_entry(&candidate, 1U, &fat_one);
    if (status != Status::Ok) {
        return status;
    }
    if ((fat_zero & 0xFFU) != media ||
        (fat_zero & 0x0FFFFFF8U) != 0x0FFFFFF8U ||
        fat_one < FAT32_EOC_MIN) {
        return Status::CorruptFat;
    }

    *output = candidate;
    return Status::Ok;
}

bool mounted(const FileSystem* filesystem) {
    return filesystem != nullptr && filesystem->is_mounted &&
        filesystem->device != nullptr;
}

Status get_geometry(const FileSystem* filesystem, Geometry* output) {
    if (output == nullptr) {
        return Status::InvalidArgument;
    }
    const Status status = require_mounted(filesystem);
    if (status != Status::Ok) {
        return status;
    }
    *output = filesystem->geometry;
    return Status::Ok;
}

const char* volume_label(const FileSystem* filesystem) {
    return mounted(filesystem) ? filesystem->volume_label : nullptr;
}

Status lookup(FileSystem* filesystem, const char* path, Node* output) {
    if (output == nullptr) {
        return Status::InvalidArgument;
    }
    const Status mount_status = require_mounted(filesystem);
    if (mount_status != Status::Ok) {
        return mount_status;
    }
    NormalizedPath normalized{};
    const Status path_status = normalize_path(path, &normalized);
    if (path_status != Status::Ok) {
        return path_status;
    }
    Node result{};
    const Status lookup_status = lookup_normalized(
        filesystem,
        &normalized,
        &result);
    if (lookup_status == Status::Ok) {
        *output = result;
    }
    return lookup_status;
}

Status stat(FileSystem* filesystem, const char* path, Stat* output) {
    if (output == nullptr) {
        return Status::InvalidArgument;
    }
    Node node{};
    const Status status = lookup(filesystem, path, &node);
    if (status != Status::Ok) {
        return status;
    }
    *output = make_stat(node);
    return Status::Ok;
}

Status read_node(
    FileSystem* filesystem,
    const Node* node,
    uint64_t offset,
    void* destination,
    size_t capacity,
    size_t* bytes_read) {
    if (node == nullptr || bytes_read == nullptr ||
        (capacity > 0U && destination == nullptr)) {
        return Status::InvalidArgument;
    }
    const Status mount_status = require_mounted(filesystem);
    if (mount_status != Status::Ok) {
        return mount_status;
    }
    if (node->type == EntryType::Directory) {
        return Status::IsDirectory;
    }

    *bytes_read = 0U;
    if (offset >= node->size || capacity == 0U) {
        return Status::Ok;
    }
    if (node->first_cluster < 2U ||
        node->first_cluster > filesystem->geometry.max_cluster) {
        return Status::CorruptChain;
    }

    const uint64_t cluster_size =
        static_cast<uint64_t>(filesystem->geometry.sectors_per_cluster) *
        SUPPORTED_SECTOR_SIZE;
    const uint64_t clusters_to_skip = offset / cluster_size;
    uint64_t offset_in_cluster = offset % cluster_size;
    ChainWalker walker{};
    Status status = initialize_walker(
        filesystem,
        node->first_cluster,
        &walker);
    if (status != Status::Ok) {
        return status;
    }
    for (uint64_t skipped = 0U; skipped < clusters_to_skip; ++skipped) {
        bool end = false;
        status = advance_walker(filesystem, &walker, &end);
        if (status != Status::Ok) {
            return status;
        }
        if (end) {
            return Status::TruncatedChain;
        }
    }

    const uint64_t available = node->size - offset;
    const size_t target = available < static_cast<uint64_t>(capacity)
        ? static_cast<size_t>(available)
        : capacity;
    uint8_t sector[SUPPORTED_SECTOR_SIZE]{};
    uint8_t* output_bytes = static_cast<uint8_t*>(destination);
    size_t copied = 0U;
    while (copied < target) {
        uint64_t cluster_sector = 0U;
        status = cluster_first_sector(
            filesystem,
            walker.current,
            &cluster_sector);
        if (status != Status::Ok) {
            *bytes_read = copied;
            return status;
        }

        const uint64_t sector_index =
            offset_in_cluster / SUPPORTED_SECTOR_SIZE;
        const size_t offset_in_sector = static_cast<size_t>(
            offset_in_cluster % SUPPORTED_SECTOR_SIZE);
        status = read_sector(
            filesystem,
            cluster_sector + sector_index,
            sector);
        if (status != Status::Ok) {
            *bytes_read = copied;
            return status;
        }

        size_t chunk = SUPPORTED_SECTOR_SIZE - offset_in_sector;
        if (chunk > target - copied) {
            chunk = target - copied;
        }
        const uint64_t cluster_remaining = cluster_size - offset_in_cluster;
        if (static_cast<uint64_t>(chunk) > cluster_remaining) {
            chunk = static_cast<size_t>(cluster_remaining);
        }
        for (size_t index = 0U; index < chunk; ++index) {
            output_bytes[copied + index] = sector[offset_in_sector + index];
        }
        copied += chunk;
        offset_in_cluster += static_cast<uint64_t>(chunk);

        if (copied < target && offset_in_cluster == cluster_size) {
            bool end = false;
            status = advance_walker(filesystem, &walker, &end);
            if (status != Status::Ok) {
                *bytes_read = copied;
                return status;
            }
            if (end) {
                *bytes_read = copied;
                return Status::TruncatedChain;
            }
            offset_in_cluster = 0U;
        }
    }
    *bytes_read = copied;

    // At logical EOF require the allocation chain to terminate at the cluster
    // containing the final byte.  This catches hidden cycles/bad values and
    // overlong chains instead of silently accepting damaged metadata.
    if (node->size - offset == static_cast<uint64_t>(copied)) {
        uint32_t next = 0U;
        bool end = false;
        status = fat_next(filesystem, walker.current, &next, &end);
        if (status != Status::Ok) {
            return status;
        }
        if (!end) {
            return Status::CorruptChain;
        }
    }
    return Status::Ok;
}

Status read(
    FileSystem* filesystem,
    const char* path,
    uint64_t offset,
    void* destination,
    size_t capacity,
    size_t* bytes_read) {
    Node node{};
    const Status status = lookup(filesystem, path, &node);
    if (status != Status::Ok) {
        return status;
    }
    return read_node(
        filesystem,
        &node,
        offset,
        destination,
        capacity,
        bytes_read);
}

Status readdir(
    FileSystem* filesystem,
    const char* directory_path,
    uint64_t* cookie,
    DirectoryEntry* output) {
    if (cookie == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    Node directory{};
    Status status = lookup(filesystem, directory_path, &directory);
    if (status != Status::Ok) {
        return status;
    }
    if (directory.type != EntryType::Directory) {
        return Status::NotDirectory;
    }

    ScannedEntry selected{};
    bool unsupported = false;
    status = scan_directory(
        filesystem,
        &directory,
        ScanMode::VisibleIndex,
        nullptr,
        *cookie,
        &selected,
        &unsupported);
    if (status != Status::Ok) {
        return status;
    }
    if (*cookie == UINT64_MAX) {
        return Status::ArithmeticOverflow;
    }
    ++(*cookie);
    if (unsupported) {
        return Status::UnsupportedNameEncoding;
    }

    DirectoryEntry result{};
    for (size_t index = 0U; index <= selected.name_length; ++index) {
        result.name[index] = selected.name[index];
    }
    result.name_length = selected.name_length;
    result.info = make_stat(selected.node);
    *output = result;
    return Status::Ok;
}

Status sync(FileSystem* filesystem) {
    const Status status = require_mounted(filesystem);
    if (status != Status::Ok) {
        return status;
    }
    const storage::block::Status flush_status =
        storage::block::flush(filesystem->device);
    if (flush_status == storage::block::Status::ReadOnly) {
        return Status::ReadOnly;
    }
    return flush_status == storage::block::Status::Ok
        ? Status::Ok
        : Status::BlockDeviceError;
}

Status create(FileSystem* filesystem, const char* path) {
    const Status mount_status = require_mounted(filesystem);
    if (mount_status != Status::Ok) {
        return mount_status;
    }
    NormalizedPath normalized{};
    Node parent{};
    const char* leaf = nullptr;
    Status status = split_path(
        filesystem, path, &normalized, &parent, &leaf);
    if (status != Status::Ok) {
        return status;
    }
    uint8_t short_name[11]{};
    status = encode_short_name(leaf, short_name);
    if (status != Status::Ok) {
        return status;
    }
    Node existing{};
    DirectorySlot existing_slot{};
    status = locate_directory_entry(
        filesystem, &parent, leaf, &existing, &existing_slot);
    if (status == Status::Ok) {
        return Status::AlreadyExists;
    }
    if (status != Status::NotFound) {
        return status;
    }
    DirectorySlot free_slot{};
    status = find_free_directory_slot(filesystem, &parent, &free_slot);
    if (status != Status::Ok) {
        return status;
    }
    return publish_directory_entry(
        filesystem, free_slot, short_name, 0x20U, 0U, 0U);
}

Status write(
    FileSystem* filesystem,
    const char* path,
    uint64_t offset,
    const void* source,
    size_t size) {
    const Status mount_status = require_mounted(filesystem);
    if (mount_status != Status::Ok) {
        return mount_status;
    }
    if (path == nullptr || (source == nullptr && size != 0U)) {
        return Status::InvalidArgument;
    }
    if (size > UINT64_MAX - offset || offset + size > UINT32_MAX) {
        return Status::ArithmeticOverflow;
    }
    if (size == 0U) {
        return Status::Ok;
    }

    Node parent{};
    Node node{};
    DirectorySlot slot{};
    Status status = locate_path_entry(
        filesystem, path, &parent, &node, &slot);
    if (status != Status::Ok) {
        return status;
    }
    if (node.type == EntryType::Directory) {
        return Status::IsDirectory;
    }
    const uint64_t end = offset + static_cast<uint64_t>(size);
    status = ensure_file_capacity(filesystem, &node, end);
    if (status != Status::Ok) {
        return status;
    }
    if (offset > node.size) {
        const uint64_t gap = offset - node.size;
        status = write_chain_bytes(
            filesystem,
            &node,
            node.size,
            nullptr,
            static_cast<size_t>(gap));
        if (status != Status::Ok) {
            return status;
        }
    }
    status = write_chain_bytes(
        filesystem,
        &node,
        offset,
        static_cast<const uint8_t*>(source),
        size);
    if (status != Status::Ok) {
        return status;
    }
    if (end > node.size) {
        node.size = end;
    }
    return update_directory_metadata(filesystem, slot, node);
}

Status unlink(FileSystem* filesystem, const char* path) {
    const Status mount_status = require_mounted(filesystem);
    if (mount_status != Status::Ok) {
        return mount_status;
    }
    Node parent{};
    Node node{};
    DirectorySlot slot{};
    Status status = locate_path_entry(
        filesystem, path, &parent, &node, &slot);
    if (status != Status::Ok) {
        return status;
    }
    if (node.type == EntryType::Directory) {
        return Status::IsDirectory;
    }
    status = retire_directory_entry(filesystem, slot);
    if (status != Status::Ok) {
        return status;
    }
    return release_chain(filesystem, node.first_cluster);
}

Status rename(
    FileSystem* filesystem,
    const char* source_path,
    const char* destination_path) {
    const Status mount_status = require_mounted(filesystem);
    if (mount_status != Status::Ok) {
        return mount_status;
    }
    Node source_parent{};
    Node source_node{};
    DirectorySlot source_slot{};
    NormalizedPath source_normalized{};
    Status status = locate_path_entry(
        filesystem,
        source_path,
        &source_parent,
        &source_node,
        &source_slot,
        &source_normalized);
    if (status != Status::Ok) {
        return status;
    }
    if (source_slot.preceding_long_entries != 0U) {
        return Status::Unsupported;
    }
    NormalizedPath destination_normalized{};
    Node destination_parent{};
    const char* destination_leaf = nullptr;
    status = split_path(
        filesystem,
        destination_path,
        &destination_normalized,
        &destination_parent,
        &destination_leaf);
    if (status != Status::Ok) {
        return status;
    }
    if (source_parent.first_cluster != destination_parent.first_cluster) {
        return Status::Unsupported;
    }
    Node existing{};
    DirectorySlot existing_slot{};
    status = locate_directory_entry(
        filesystem,
        &destination_parent,
        destination_leaf,
        &existing,
        &existing_slot);
    if (status == Status::Ok) {
        return Status::AlreadyExists;
    }
    if (status != Status::NotFound) {
        return status;
    }
    uint8_t short_name[11]{};
    status = encode_short_name(destination_leaf, short_name);
    if (status != Status::Ok) {
        return status;
    }
    uint8_t sector[SUPPORTED_SECTOR_SIZE]{};
    status = read_sector(filesystem, source_slot.sector, sector);
    if (status != Status::Ok) {
        return status;
    }
    for (size_t index = 0U; index < 11U; ++index) {
        sector[source_slot.offset + index] = short_name[index];
    }
    return write_sector(filesystem, source_slot.sector, sector);
}

Status mkdir(FileSystem* filesystem, const char* path) {
    const Status mount_status = require_mounted(filesystem);
    if (mount_status != Status::Ok) {
        return mount_status;
    }
    NormalizedPath normalized{};
    Node parent{};
    const char* leaf = nullptr;
    Status status = split_path(
        filesystem, path, &normalized, &parent, &leaf);
    if (status != Status::Ok) {
        return status;
    }
    uint8_t short_name[11]{};
    status = encode_short_name(leaf, short_name);
    if (status != Status::Ok) {
        return status;
    }
    Node existing{};
    DirectorySlot ignored{};
    status = locate_directory_entry(filesystem, &parent, leaf, &existing, &ignored);
    if (status == Status::Ok) {
        return Status::AlreadyExists;
    }
    if (status != Status::NotFound) {
        return status;
    }
    DirectorySlot parent_slot{};
    status = find_free_directory_slot(filesystem, &parent, &parent_slot);
    if (status != Status::Ok) {
        return status;
    }
    uint32_t cluster = 0U;
    status = allocate_cluster(filesystem, &cluster);
    if (status != Status::Ok) {
        return status;
    }
    uint64_t directory_sector = 0U;
    status = cluster_first_sector(filesystem, cluster, &directory_sector);
    if (status != Status::Ok) {
        static_cast<void>(release_chain(filesystem, cluster));
        return status;
    }
    const uint8_t dot[11] = {
        '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '
    };
    const uint8_t dot_dot[11] = {
        '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '
    };
    status = publish_directory_entry(
        filesystem,
        DirectorySlot{directory_sector, 0U, 0U},
        dot,
        ATTRIBUTE_DIRECTORY,
        cluster,
        0U);
    if (status == Status::Ok) {
        const uint32_t parent_cluster =
            parent.first_cluster == filesystem->geometry.root_cluster
                ? 0U
                : parent.first_cluster;
        status = publish_directory_entry(
            filesystem,
            DirectorySlot{directory_sector, DIRECTORY_ENTRY_SIZE, 0U},
            dot_dot,
            ATTRIBUTE_DIRECTORY,
            parent_cluster,
            0U);
    }
    if (status == Status::Ok) {
        status = publish_directory_entry(
            filesystem,
            parent_slot,
            short_name,
            ATTRIBUTE_DIRECTORY,
            cluster,
            0U);
    }
    if (status != Status::Ok) {
        static_cast<void>(release_chain(filesystem, cluster));
    }
    return status;
}

Status rmdir(FileSystem* filesystem, const char* path) {
    const Status mount_status = require_mounted(filesystem);
    if (mount_status != Status::Ok) {
        return mount_status;
    }
    Node parent{};
    Node node{};
    DirectorySlot slot{};
    Status status = locate_path_entry(
        filesystem, path, &parent, &node, &slot);
    if (status != Status::Ok) {
        return status;
    }
    if (node.type != EntryType::Directory) {
        return Status::NotDirectory;
    }
    ScannedEntry child{};
    bool unsupported = false;
    status = scan_directory(
        filesystem,
        &node,
        ScanMode::VisibleIndex,
        nullptr,
        0U,
        &child,
        &unsupported);
    if (status == Status::Ok || status == Status::UnsupportedNameEncoding) {
        return Status::DirectoryNotEmpty;
    }
    if (status != Status::EndOfDirectory) {
        return status;
    }
    status = retire_directory_entry(filesystem, slot);
    if (status != Status::Ok) {
        return status;
    }
    return release_chain(filesystem, node.first_cluster);
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotMounted: return "filesystem is not mounted";
        case Status::InvalidArgument: return "invalid argument";
        case Status::InvalidPath: return "invalid path";
        case Status::PathTooLong: return "path is too long";
        case Status::NameTooLong: return "path component is too long";
        case Status::PathTooDeep: return "path is too deep";
        case Status::PathEscapesRoot: return "path escapes the filesystem root";
        case Status::NotFound: return "entry was not found";
        case Status::NotDirectory: return "entry is not a directory";
        case Status::IsDirectory: return "entry is a directory";
        case Status::EndOfDirectory: return "end of directory";
        case Status::UnsupportedSectorSize: return "unsupported sector size";
        case Status::UnsupportedGeometry: return "unsupported FAT32 geometry";
        case Status::InvalidBootSector: return "invalid FAT32 boot sector";
        case Status::CorruptFsInfo: return "corrupt FAT32 FSInfo sector";
        case Status::CorruptBackupBoot: return "backup boot geometry mismatch";
        case Status::FatMirrorMismatch: return "FAT mirrors differ";
        case Status::CorruptFat: return "corrupt FAT metadata";
        case Status::CorruptDirectory: return "corrupt directory entry";
        case Status::CorruptChain: return "corrupt FAT cluster chain";
        case Status::ChainCycle: return "cycle in FAT cluster chain";
        case Status::TruncatedChain: return "cluster chain is shorter than file";
        case Status::UnsupportedNameEncoding:
            return "name uses unsupported Unicode encoding";
        case Status::ArithmeticOverflow: return "filesystem arithmetic overflow";
        case Status::BlockDeviceError: return "block-device I/O failed";
        case Status::ReadOnly: return "filesystem is read-only";
        case Status::AlreadyExists: return "entry already exists";
        case Status::DirectoryNotEmpty: return "directory is not empty";
        case Status::NoSpace: return "no free FAT32 cluster remains";
        case Status::Unsupported: return "operation is unsupported";
    }
    return "unknown FAT32 status";
}

} // namespace fs::fat32
