#include "package.hpp"

#include "../libk/crc.hpp"

namespace install::package {
namespace {

constexpr size_t HEADER_SIZE = 64U;
constexpr size_t ENTRY_SIZE = 160U;
constexpr size_t PATH_FIELD_SIZE = 128U;
constexpr size_t MAXIMUM_PACKAGE_SIZE = 16U * 1024U * 1024U;
constexpr uint8_t MAGIC[8] = {'K', 'U', 'R', 'P', 'K', 'G', '1', 0};

uint32_t read_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0U]) |
        static_cast<uint32_t>(bytes[1U]) << 8U |
        static_cast<uint32_t>(bytes[2U]) << 16U |
        static_cast<uint32_t>(bytes[3U]) << 24U;
}

uint64_t read_u64(const uint8_t* bytes) {
    return static_cast<uint64_t>(read_u32(bytes)) |
        static_cast<uint64_t>(read_u32(bytes + 4U)) << 32U;
}

bool add_valid(uint64_t left, uint64_t right, uint64_t* result) {
    if (result == nullptr || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

bool valid_path(const uint8_t* path, char* output) {
    if (path == nullptr || output == nullptr || path[0U] != '/') return false;
    size_t length = 0U;
    bool component_has_character = false;
    size_t component_length = 0U;
    while (length < PATH_FIELD_SIZE && path[length] != 0U) {
        const uint8_t value = path[length];
        if (value < 0x20U || value > 0x7EU || value == '\\') return false;
        if (value == '/') {
            if (length != 0U && !component_has_character) return false;
            component_has_character = false;
            component_length = 0U;
        } else {
            component_has_character = true;
            ++component_length;
            if (component_length > 12U) return false;
        }
        output[length] = static_cast<char>(value);
        ++length;
    }
    if (length == 0U || length >= PATH_FIELD_SIZE ||
        !component_has_character) return false;
    output[length] = '\0';
    for (size_t index = length + 1U; index < PATH_FIELD_SIZE; ++index) {
        if (path[index] != 0U) return false;
    }
    return true;
}

} // namespace

Status file_at(const View& package, size_t index, File* output) {
    if (output == nullptr || package.bytes == nullptr ||
        package.entry_size != ENTRY_SIZE || index >= package.file_count) {
        return Status::InvalidArgument;
    }
    const uint64_t entry_offset = package.entries_offset + index * ENTRY_SIZE;
    if (entry_offset > package.size || ENTRY_SIZE > package.size - entry_offset) {
        return Status::InvalidLayout;
    }
    const uint8_t* entry = package.bytes + entry_offset;
    File result{};
    if (!valid_path(entry, result.path)) return Status::InvalidPath;
    const uint64_t file_offset = read_u64(entry + 128U);
    const uint64_t file_size = read_u64(entry + 136U);
    result.checksum = read_u32(entry + 144U);
    result.destination = read_u32(entry + 148U);
    if (result.destination != DESTINATION_ESP &&
        result.destination != DESTINATION_ROOT) {
        return Status::InvalidDestination;
    }
    uint64_t end = 0U;
    if (file_size > SIZE_MAX || !add_valid(file_offset, file_size, &end) ||
        file_offset < HEADER_SIZE || end > package.size) {
        return Status::InvalidFileRange;
    }
    result.data = package.bytes + file_offset;
    result.size = static_cast<size_t>(file_size);
    if (k_crc32(result.data, result.size) != result.checksum) {
        return Status::InvalidFileChecksum;
    }
    *output = result;
    return Status::Ok;
}

Status parse(const void* bytes, size_t size, View* output) {
    if (bytes == nullptr || output == nullptr) return Status::InvalidArgument;
    *output = {};
    if (size < HEADER_SIZE || size > MAXIMUM_PACKAGE_SIZE) {
        return Status::InvalidLayout;
    }
    const auto* raw = static_cast<const uint8_t*>(bytes);
    for (size_t index = 0U; index < sizeof(MAGIC); ++index) {
        if (raw[index] != MAGIC[index]) return Status::InvalidMagic;
    }
    if (read_u32(raw + 8U) != 1U) return Status::UnsupportedVersion;
    const uint32_t header_size = read_u32(raw + 12U);
    const uint64_t total_size = read_u64(raw + 16U);
    const uint32_t file_count = read_u32(raw + 24U);
    const uint32_t entry_size = read_u32(raw + 28U);
    const uint64_t entries_offset = read_u64(raw + 32U);
    const uint64_t data_offset = read_u64(raw + 40U);
    const uint32_t manifest_crc = read_u32(raw + 48U);
    if (header_size != HEADER_SIZE || total_size != size ||
        file_count == 0U || entry_size != ENTRY_SIZE ||
        entries_offset != HEADER_SIZE || data_offset > size ||
        (data_offset & 15U) != 0U) {
        return Status::InvalidLayout;
    }
    if (file_count > MAXIMUM_FILES) return Status::TooManyFiles;
    const uint64_t manifest_size = static_cast<uint64_t>(file_count) * ENTRY_SIZE;
    uint64_t manifest_end = 0U;
    if (!add_valid(entries_offset, manifest_size, &manifest_end) ||
        manifest_end > data_offset || manifest_size > SIZE_MAX) {
        return Status::InvalidLayout;
    }
    if (k_crc32(raw + entries_offset, static_cast<size_t>(manifest_size)) !=
        manifest_crc) {
        return Status::InvalidManifestChecksum;
    }
    View result{raw, size, file_count, entries_offset, entry_size};
    uint64_t previous_end = data_offset;
    for (size_t index = 0U; index < file_count; ++index) {
        File file{};
        const Status status = file_at(result, index, &file);
        if (status != Status::Ok) return status;
        const uint64_t file_offset = static_cast<uint64_t>(file.data - raw);
        if (file_offset < previous_end) return Status::InvalidFileRange;
        previous_end = file_offset + file.size;
        for (size_t earlier = 0U; earlier < index; ++earlier) {
            File candidate{};
            if (file_at(result, earlier, &candidate) != Status::Ok) {
                return Status::InvalidLayout;
            }
            bool same = candidate.destination == file.destination;
            for (size_t character = 0U; same; ++character) {
                if (candidate.path[character] != file.path[character]) same = false;
                if (candidate.path[character] == '\0' ||
                    file.path[character] == '\0') break;
            }
            if (same) return Status::InvalidPath;
        }
    }
    *output = result;
    return Status::Ok;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidArgument: return "invalid package argument";
        case Status::InvalidMagic: return "invalid installer package magic";
        case Status::UnsupportedVersion: return "unsupported package version";
        case Status::InvalidLayout: return "invalid package layout";
        case Status::TooManyFiles: return "too many package files";
        case Status::InvalidManifestChecksum: return "manifest checksum mismatch";
        case Status::InvalidPath: return "invalid or duplicate package path";
        case Status::InvalidDestination: return "invalid package destination";
        case Status::InvalidFileRange: return "invalid package file range";
        case Status::InvalidFileChecksum: return "package file checksum mismatch";
    }
    return "unknown package status";
}

} // namespace install::package
