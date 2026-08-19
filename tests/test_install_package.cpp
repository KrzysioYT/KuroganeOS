#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "../kernel/install/package.hpp"
#include "../kernel/libk/crc.hpp"

namespace {
void put32(uint8_t* bytes, uint32_t value) {
    for (size_t index = 0U; index < 4U; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8U));
    }
}
void put64(uint8_t* bytes, uint64_t value) {
    put32(bytes, static_cast<uint32_t>(value));
    put32(bytes + 4U, static_cast<uint32_t>(value >> 32U));
}
} // namespace

int main() {
    uint8_t bytes[240]{};
    std::memcpy(bytes, "KURPKG1", 7U);
    put32(bytes + 8U, 1U);
    put32(bytes + 12U, 64U);
    put64(bytes + 16U, sizeof(bytes));
    put32(bytes + 24U, 1U);
    put32(bytes + 28U, 160U);
    put64(bytes + 32U, 64U);
    put64(bytes + 40U, 224U);
    static constexpr char kBootPath[] = "/EFI/BOOT/BOOTX64.EFI";
    std::memcpy(bytes + 64U, kBootPath, sizeof(kBootPath) - 1U);
    put64(bytes + 64U + 128U, 224U);
    put64(bytes + 64U + 136U, 4U);
    std::memcpy(bytes + 224U, "BOOT", 4U);
    put32(bytes + 64U + 144U, k_crc32(bytes + 224U, 4U));
    put32(bytes + 64U + 148U, install::package::DESTINATION_ESP);
    put32(bytes + 48U, k_crc32(bytes + 64U, 160U));

    install::package::View package{};
    using install::package::Status;
    assert(install::package::parse(bytes, sizeof(bytes), &package) == Status::Ok);
    install::package::File file{};
    assert(install::package::file_at(package, 0U, &file) == Status::Ok);
    assert(std::strcmp(file.path, "/EFI/BOOT/BOOTX64.EFI") == 0);
    assert(file.size == 4U && std::memcmp(file.data, "BOOT", 4U) == 0);

    bytes[225U] ^= 1U;
    assert(install::package::parse(bytes, sizeof(bytes), &package) ==
           Status::InvalidFileChecksum);
    bytes[225U] ^= 1U;

    // Runtime preflight must enforce the same bounded FAT 8.3 mutation
    // contract as build-install-package.py. Otherwise a malformed package can
    // pass validation, erase the target disk, and only fail later in FAT32
    // create().
    std::memset(bytes + 64U, 0, 128U);
    static constexpr char kInvalidPath[] = "/TOOLONGNAME/FILE.TXT";
    std::memcpy(bytes + 64U, kInvalidPath, sizeof(kInvalidPath) - 1U);
    put32(bytes + 48U, k_crc32(bytes + 64U, 160U));
    assert(install::package::parse(bytes, sizeof(bytes), &package) ==
           Status::InvalidPath);

    std::cout << "installer package tests: PASS\n";
    return 0;
}