#pragma once

#include <stddef.h>
#include <stdint.h>

namespace install::package {

constexpr size_t MAXIMUM_FILES = 128U;
constexpr size_t MAXIMUM_PATH = 127U;
constexpr uint32_t DESTINATION_ESP = 1U;
constexpr uint32_t DESTINATION_ROOT = 2U;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidMagic,
    UnsupportedVersion,
    InvalidLayout,
    TooManyFiles,
    InvalidManifestChecksum,
    InvalidPath,
    InvalidDestination,
    InvalidFileRange,
    InvalidFileChecksum,
};

struct File {
    char path[MAXIMUM_PATH + 1U];
    const uint8_t* data;
    size_t size;
    uint32_t destination;
    uint32_t checksum;
};

struct View {
    const uint8_t* bytes;
    size_t size;
    uint32_t file_count;
    uint64_t entries_offset;
    uint32_t entry_size;
};

Status parse(const void* bytes, size_t size, View* output);
Status file_at(const View& package, size_t index, File* output);
const char* status_message(Status status);

} // namespace install::package
