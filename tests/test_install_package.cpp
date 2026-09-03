#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "../kernel/install/package.hpp"
#include "../kernel/libk/crc.hpp"

namespace {
constexpr size_t HEADER_SIZE = 64U;
constexpr size_t ENTRY_SIZE = 160U;
constexpr size_t PATH_SIZE = 128U;

void put32(uint8_t* bytes, uint32_t value) {
    for (size_t index = 0U; index < 4U; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8U));
    }
}

void put64(uint8_t* bytes, uint64_t value) {
    put32(bytes, static_cast<uint32_t>(value));
    put32(bytes + 4U, static_cast<uint32_t>(value >> 32U));
}

void update_manifest_crc(std::vector<uint8_t>* bytes, size_t count) {
    assert(bytes != nullptr);
    put32(
        bytes->data() + 48U,
        k_crc32(bytes->data() + HEADER_SIZE, count * ENTRY_SIZE));
}

std::vector<uint8_t> make_package(size_t count, bool with_payload = false) {
    const size_t manifest_end = HEADER_SIZE + count * ENTRY_SIZE;
    const size_t data_offset = (manifest_end + 15U) & ~size_t{15U};
    const size_t payload_size = with_payload ? 4U : 0U;
    std::vector<uint8_t> bytes(data_offset + payload_size, 0U);
    std::memcpy(bytes.data(), "KURPKG1", 7U);
    put32(bytes.data() + 8U, 1U);
    put32(bytes.data() + 12U, static_cast<uint32_t>(HEADER_SIZE));
    put64(bytes.data() + 16U, bytes.size());
    put32(bytes.data() + 24U, static_cast<uint32_t>(count));
    put32(bytes.data() + 28U, static_cast<uint32_t>(ENTRY_SIZE));
    put64(bytes.data() + 32U, HEADER_SIZE);
    put64(bytes.data() + 40U, data_offset);

    if (with_payload) std::memcpy(bytes.data() + data_offset, "DATA", 4U);
    for (size_t index = 0U; index < count; ++index) {
        uint8_t* const entry = bytes.data() + HEADER_SIZE + index * ENTRY_SIZE;
        char path[PATH_SIZE]{};
        const int length = std::snprintf(
            path, sizeof(path), "/FILES/F%07zu.BIN", index);
        assert(length > 0 && static_cast<size_t>(length) < sizeof(path));
        std::memcpy(entry, path, static_cast<size_t>(length));
        const uint64_t file_offset = data_offset +
            ((with_payload && index != 0U) ? payload_size : 0U);
        const uint64_t file_size = with_payload && index == 0U
            ? payload_size : 0U;
        put64(entry + 128U, file_offset);
        put64(entry + 136U, file_size);
        put32(
            entry + 144U,
            k_crc32(bytes.data() + file_offset, static_cast<size_t>(file_size)));
        put32(entry + 148U, install::package::DESTINATION_ROOT);
    }
    update_manifest_crc(&bytes, count);
    return bytes;
}

install::package::Status parse(
    std::vector<uint8_t>& bytes, install::package::View* output = nullptr) {
    install::package::View local{};
    return install::package::parse(
        bytes.data(), bytes.size(), output == nullptr ? &local : output);
}
} // namespace

int main() {
    using install::package::Status;

    std::vector<uint8_t> basic = make_package(1U, true);
    install::package::View package{};
    assert(parse(basic, &package) == Status::Ok);
    assert(package.file_count == 1U);
    install::package::File file{};
    assert(install::package::file_at(package, 0U, &file) == Status::Ok);
    assert(std::strcmp(file.path, "/FILES/F0000000.BIN") == 0);
    assert(file.size == 4U && std::memcmp(file.data, "DATA", 4U) == 0);

    basic.back() ^= 1U;
    assert(parse(basic) == Status::InvalidFileChecksum);
    basic.back() ^= 1U;

    std::vector<uint8_t> zero = make_package(1U);
    put32(zero.data() + 24U, 0U);
    assert(parse(zero) == Status::InvalidLayout);

    std::vector<uint8_t> maximum = make_package(
        install::package::MAXIMUM_FILES);
    assert(parse(maximum, &package) == Status::Ok);
    assert(package.file_count == install::package::MAXIMUM_FILES);

    std::vector<uint8_t> excessive = make_package(
        install::package::MAXIMUM_FILES + 1U);
    assert(parse(excessive) == Status::TooManyFiles);

    std::vector<uint8_t> duplicate = make_package(2U);
    std::memcpy(
        duplicate.data() + HEADER_SIZE + ENTRY_SIZE,
        duplicate.data() + HEADER_SIZE,
        PATH_SIZE);
    update_manifest_crc(&duplicate, 2U);
    assert(parse(duplicate) == Status::InvalidPath);

    std::vector<uint8_t> destination = make_package(1U);
    put32(destination.data() + HEADER_SIZE + 148U, 99U);
    update_manifest_crc(&destination, 1U);
    assert(parse(destination) == Status::InvalidDestination);

    std::vector<uint8_t> oversized_path = make_package(1U);
    std::memset(oversized_path.data() + HEADER_SIZE, 'A', PATH_SIZE);
    update_manifest_crc(&oversized_path, 1U);
    assert(parse(oversized_path) == Status::InvalidPath);

    std::vector<uint8_t> overflow = make_package(1U);
    put64(overflow.data() + HEADER_SIZE + 128U, UINT64_MAX - 1U);
    put64(overflow.data() + HEADER_SIZE + 136U, 4U);
    update_manifest_crc(&overflow, 1U);
    assert(parse(overflow) == Status::InvalidFileRange);

    std::vector<uint8_t> corrupt_count = make_package(1U);
    put32(corrupt_count.data() + 24U, 2U);
    assert(parse(corrupt_count) == Status::InvalidLayout);

    std::vector<uint8_t> truncated_table = make_package(2U);
    truncated_table.resize(HEADER_SIZE + ENTRY_SIZE + ENTRY_SIZE - 1U);
    put64(truncated_table.data() + 16U, truncated_table.size());
    assert(parse(truncated_table) == Status::InvalidLayout);

    std::vector<uint8_t> truncated_data = make_package(1U, true);
    truncated_data.pop_back();
    put64(truncated_data.data() + 16U, truncated_data.size());
    assert(parse(truncated_data) == Status::InvalidFileRange);

    // Runtime preflight must enforce the same bounded FAT 8.3 mutation
    // contract as build-install-package.py.
    std::vector<uint8_t> long_component = make_package(1U);
    std::memset(long_component.data() + HEADER_SIZE, 0, PATH_SIZE);
    static constexpr char kInvalidPath[] = "/TOOLONGNAME/FILE.TXT";
    std::memcpy(
        long_component.data() + HEADER_SIZE,
        kInvalidPath,
        sizeof(kInvalidPath) - 1U);
    update_manifest_crc(&long_component, 1U);
    assert(parse(long_component) == Status::InvalidPath);

    std::cout << "installer package tests: PASS (max="
              << install::package::MAXIMUM_FILES << ")\n";
    return 0;
}
