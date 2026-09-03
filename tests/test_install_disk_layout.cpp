#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_map>

#include "../kernel/fs/fat32.hpp"
#include "../kernel/install/disk_layout.hpp"
#include "../kernel/install/fat32_reliable_file.hpp"
#include "../kernel/storage/gpt.hpp"
#include "../kernel/storage/partition_device.hpp"

namespace {
using Sector = std::array<uint8_t, 512>;
struct SparseDisk {
    std::unordered_map<uint64_t, Sector> sectors;
    unsigned flushes = 0;
};

storage::block::Status read_blocks(
    void* context, uint64_t first, uint64_t count, void* destination) {
    auto& disk = *static_cast<SparseDisk*>(context);
    auto* output = static_cast<uint8_t*>(destination);
    for (uint64_t index = 0; index < count; ++index) {
        const auto found = disk.sectors.find(first + index);
        const Sector empty{};
        const Sector& sector = found == disk.sectors.end() ? empty : found->second;
        for (size_t byte = 0; byte < sector.size(); ++byte) {
            output[index * sector.size() + byte] = sector[byte];
        }
    }
    return storage::block::Status::Ok;
}

storage::block::Status write_blocks(
    void* context, uint64_t first, uint64_t count, const void* source) {
    auto& disk = *static_cast<SparseDisk*>(context);
    const auto* input = static_cast<const uint8_t*>(source);
    for (uint64_t index = 0; index < count; ++index) {
        Sector& sector = disk.sectors[first + index];
        for (size_t byte = 0; byte < sector.size(); ++byte) {
            sector[byte] = input[index * sector.size() + byte];
        }
    }
    return storage::block::Status::Ok;
}

storage::block::Status flush(void* context) {
    ++static_cast<SparseDisk*>(context)->flushes;
    return storage::block::Status::Ok;
}

void qualify_reliable_state_replace(storage::partition::Device* root) {
    fs::fat32::FileSystem filesystem{};
    assert(fs::fat32::mount(
        &filesystem, storage::partition::as_block_device(root)) ==
        fs::fat32::Status::Ok);
    assert(fs::fat32::mkdir(&filesystem, "/etc") == fs::fat32::Status::Ok);

    constexpr char old_profile[] =
        "USERNAME=old\nPASSWORD_REQUIRED=0\nPASSWORD_HASH=0000000000000000\n";
    assert(fs::fat32::create(&filesystem, "/etc/user.cfg") ==
           fs::fat32::Status::Ok);
    assert(fs::fat32::write(
        &filesystem,
        "/etc/user.cfg",
        0U,
        old_profile,
        sizeof(old_profile) - 1U) == fs::fat32::Status::Ok);
    assert(fs::fat32::sync(&filesystem) == fs::fat32::Status::Ok);

    constexpr char new_profile[] =
        "USERNAME=tester\nPASSWORD_REQUIRED=1\nPASSWORD_HASH=0123456789ABCDEF\n"
        "HASH_SCHEME=FNV1A64-DEV\n";
    const install::reliable_file::Paths paths{
        "/etc/user.cfg",
        "/etc/user.new",
        "/etc/user.bak",
        "/etc/user.old",
    };
    assert(install::fat32_reliable_file::replace(
        &filesystem,
        paths,
        new_profile,
        sizeof(new_profile) - 1U) == install::reliable_file::Status::Ok);

    char readback[192]{};
    size_t bytes_read = 0U;
    assert(fs::fat32::read(
        &filesystem,
        "/etc/user.cfg",
        0U,
        readback,
        sizeof(readback) - 1U,
        &bytes_read) == fs::fat32::Status::Ok);
    assert(bytes_read == sizeof(new_profile) - 1U);
    assert(std::memcmp(readback, new_profile, bytes_read) == 0);

    fs::fat32::Stat info{};
    assert(fs::fat32::stat(&filesystem, "/etc/user.new", &info) ==
           fs::fat32::Status::NotFound);
    assert(fs::fat32::stat(&filesystem, "/etc/user.bak", &info) ==
           fs::fat32::Status::NotFound);
    assert(fs::fat32::stat(&filesystem, "/etc/user.old", &info) ==
           fs::fat32::Status::NotFound);
    assert(fs::fat32::sync(&filesystem) == fs::fat32::Status::Ok);
}
} // namespace

int main() {
    SparseDisk disk{};
    storage::block::Device device{
        &disk, 512U, install::disk_layout::MINIMUM_DISK_SECTORS,
        read_blocks, write_blocks, flush
    };
    install::disk_layout::Layout layout{};
    assert(install::disk_layout::prepare_empty_disk(&device, &layout) ==
           install::disk_layout::Status::Ok);
    assert(layout.esp_first_lba == 2048U);
    assert(layout.esp_sector_count == 131072U);
    assert(layout.root_first_lba == 133120U);
    assert(disk.sectors.at(0)[510] == 0x55U &&
           disk.sectors.at(0)[511] == 0xAAU);

    storage::gpt::Table table{};
    assert(storage::gpt::parse_primary(&device, &table).status ==
           storage::gpt::Status::Ok);
    assert(table.partition_count == 2U);

    storage::partition::Device esp{};
    storage::partition::Device root{};
    assert(storage::partition::initialize(
        &esp, &device, layout.esp_first_lba, layout.esp_sector_count) ==
        storage::block::Status::Ok);
    assert(storage::partition::initialize(
        &root, &device, layout.root_first_lba, layout.root_sector_count) ==
        storage::block::Status::Ok);
    const auto esp_format = fs::fat32::format(
        storage::partition::as_block_device(&esp), "KURO_ESP", 1U,
        static_cast<uint32_t>(layout.esp_first_lba));
    if (esp_format != fs::fat32::Status::Ok) {
        std::cerr << "ESP format: " << fs::fat32::status_message(esp_format) << '\n';
    }
    assert(esp_format == fs::fat32::Status::Ok);
    const auto root_format = fs::fat32::format(
        storage::partition::as_block_device(&root), "KURO_ROOT", 8U,
        static_cast<uint32_t>(layout.root_first_lba));
    if (root_format != fs::fat32::Status::Ok) {
        std::cerr << "root format: " << fs::fat32::status_message(root_format) << '\n';
    }
    assert(root_format == fs::fat32::Status::Ok);

    qualify_reliable_state_replace(&root);

    // The conservative API remains useful for tooling that requires a blank
    // target and must refuse an existing partition table.
    install::disk_layout::Layout second{};
    assert(install::disk_layout::prepare_empty_disk(&device, &second) ==
           install::disk_layout::Status::DiskNotEmpty);

    // The real installer reaches this API only after explicit erase
    // confirmation. It must therefore be able to recover from a previous,
    // partially completed KuroganeOS installation without asking the user to
    // recreate the VDI manually.
    install::disk_layout::Layout retry{};
    assert(install::disk_layout::prepare_install_target(&device, &retry) ==
           install::disk_layout::Status::Ok);
    assert(retry.esp_first_lba == layout.esp_first_lba);
    assert(retry.esp_sector_count == layout.esp_sector_count);
    assert(retry.root_first_lba == layout.root_first_lba);
    assert(retry.root_sector_count == layout.root_sector_count);
    table = {};
    assert(storage::gpt::parse_primary(&device, &table).status ==
           storage::gpt::Status::Ok);
    assert(table.partition_count == 2U);
    assert(disk.flushes >= 2U);

    std::cout << "installer disk-layout tests: PASS\n";
    return 0;
}
