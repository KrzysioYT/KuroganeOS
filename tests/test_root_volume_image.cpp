#include "../kernel/fs/root_volume.hpp"
#include "../kernel/fs/fat32.hpp"
#include "../kernel/storage/gpt.hpp"
#include "../kernel/storage/partition_device.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

constexpr const char* kRequiredRuntimePaths[] = {
    "/apps/hello",
    "/apps/bad",
    "/apps/spin",
    "/apps/probe",
    "/apps/shell",
    "/apps/files",
    "/apps/monitor",
    "/apps/about",
    "/apps/external",
    "/system/init",
    "/system/devprobe",
    "/system/stprobe",
    "/system/eventd",
    "/system/evprobe",
    "/system/setd",
    "/system/setprobe",
    "/system/notifd",
    "/system/ntfprobe",
    "/system/accountd",
    "/system/acctprb",
    "/system/sessiond",
    "/system/sesprobe",
    "/system/setchprb",
    "/system/clipd",
    "/system/netevtd",
    "/system/dnsd",
    "/system/appregd",
    "/system/audiod",
    "/system/clipprb",
    "/system/kfsprb",
    "/system/sockprb",
    "/system/sockwake",
    "/system/sockexit",
    "/system/tcpexit",
    "/gui/login",
    "/gui/launcher",
    "/gui/terminal",
    "/gui/files",
    "/gui/sysmon",
    "/gui/perf",
    "/gui/browser",
    "/gui/about",
    "/gui/settings",
    "/gui/notify",
};

struct FileDisk {
    std::FILE* file;
};

bool is_x86_64_elf(const char* path) {
    fs::vfs::FileStat info{};
    if (fs::root_volume::stat(path, &info) != fs::vfs::Status::Ok ||
        info.type != fs::vfs::NodeType::Regular || info.size < 64U) {
        return false;
    }

    fs::vfs::OpenFileHandle handle{};
    if (fs::root_volume::open(path, fs::vfs::OpenFlags::Read, &handle) !=
        fs::vfs::Status::Ok) {
        return false;
    }
    uint8_t header[64]{};
    size_t bytes_read = 0U;
    const fs::vfs::Status read_status =
        fs::root_volume::read(handle, header, sizeof(header), &bytes_read);
    const fs::vfs::Status close_status = fs::root_volume::close(handle);
    return read_status == fs::vfs::Status::Ok &&
        close_status == fs::vfs::Status::Ok &&
        bytes_read == sizeof(header) &&
        header[0] == 0x7FU && header[1] == 'E' &&
        header[2] == 'L' && header[3] == 'F' &&
        header[4] == 2U && header[5] == 1U &&
        header[16] == 2U && header[17] == 0U &&
        header[18] == 62U && header[19] == 0U;
}

storage::block::Status read_blocks(
    void* context,
    uint64_t first_block,
    uint64_t block_count,
    void* destination) {
    auto* disk = static_cast<FileDisk*>(context);
    if (disk == nullptr || disk->file == nullptr || destination == nullptr ||
        first_block > static_cast<uint64_t>(INT64_MAX) / 512U ||
        block_count > static_cast<uint64_t>(SIZE_MAX) / 512U) {
        return storage::block::Status::InvalidArgument;
    }
    const auto offset = static_cast<off_t>(first_block * 512U);
    if (fseeko(disk->file, offset, SEEK_SET) != 0) {
        return storage::block::Status::IoError;
    }
    const size_t bytes = static_cast<size_t>(block_count * 512U);
    return std::fread(destination, 1U, bytes, disk->file) == bytes
        ? storage::block::Status::Ok
        : storage::block::Status::IoError;
}

storage::block::Status reject_write(
    void*, uint64_t, uint64_t, const void*) {
    return storage::block::Status::ReadOnly;
}

storage::block::Status flush(void*) {
    return storage::block::Status::Ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: test_root_volume_image <raw-image>\n";
        return 2;
    }
    std::FILE* file = std::fopen(argv[1], "rb");
    if (file == nullptr || fseeko(file, 0, SEEK_END) != 0) {
        std::cerr << "cannot open image\n";
        return 2;
    }
    const off_t byte_size = ftello(file);
    if (byte_size <= 0 || (byte_size % 512) != 0) {
        std::cerr << "invalid image size\n";
        std::fclose(file);
        return 2;
    }
    FileDisk backend{file};
    storage::block::Device disk{
        &backend,
        512U,
        static_cast<uint64_t>(byte_size / 512),
        read_blocks,
        reject_write,
        flush,
    };

    storage::gpt::Table table{};
    const storage::gpt::ParseResult parse =
        storage::gpt::parse_primary(&disk, &table);
    if (parse.status != storage::gpt::Status::Ok) {
        std::cerr << "GPT: " << storage::gpt::status_message(parse.status)
                  << '\n';
        std::fclose(file);
        return 1;
    }

    const storage::gpt::Partition& root_partition = table.partitions[1];
    storage::partition::Device partition{};
    if (storage::partition::initialize(
            &partition,
            &disk,
            root_partition.first_lba,
            root_partition.last_lba - root_partition.first_lba + 1U) !=
        storage::block::Status::Ok) {
        std::cerr << "diagnostic partition initialization failed\n";
        std::fclose(file);
        return 1;
    }
    fs::fat32::FileSystem direct_fat{};
    const fs::fat32::Status direct_mount = fs::fat32::mount(
        &direct_fat, storage::partition::as_block_device(&partition));
    if (direct_mount != fs::fat32::Status::Ok) {
        std::cerr << "direct FAT mount: "
                  << fs::fat32::status_message(direct_mount) << '\n';
        std::fclose(file);
        return 1;
    }
    char direct_configuration[512]{};
    size_t direct_bytes = 0U;
    const fs::fat32::Status direct_read = fs::fat32::read(
        &direct_fat,
        "/etc/system.cfg",
        0U,
        direct_configuration,
        sizeof(direct_configuration) - 1U,
        &direct_bytes);
    if (direct_read != fs::fat32::Status::Ok) {
        std::cerr << "direct FAT read: "
                  << fs::fat32::status_message(direct_read) << '\n';
        std::fclose(file);
        return 1;
    }
    const fs::root_volume::Status status =
        fs::root_volume::initialize(&disk, &table);
    if (status != fs::root_volume::Status::Ok) {
        std::cerr << "root: " << fs::root_volume::status_message(status)
                  << " (" << fs::root_volume::detail_message() << ")\n";
        std::fclose(file);
        return 1;
    }
    if (!fs::root_volume::mounted() || fs::root_volume::read_only() ||
        std::strcmp(fs::root_volume::volume_label(), "KURO_ROOT") != 0 ||
        std::strstr(fs::root_volume::configuration(), "HOSTNAME=kurogane") ==
            nullptr) {
        std::cerr << "root metadata/configuration mismatch\n";
        std::fclose(file);
        return 1;
    }
    fs::vfs::FileStat hello_info{};
    if (fs::root_volume::stat("/apps/hello", &hello_info) !=
            fs::vfs::Status::Ok ||
        hello_info.type != fs::vfs::NodeType::Regular ||
        hello_info.size < 64U || hello_info.size > 16384U) {
        std::cerr << "generated /apps/hello is missing or invalid\n";
        std::fclose(file);
        return 1;
    }
    char hello[16384]{};
    size_t hello_bytes = 0U;
    uint64_t hello_size = 0U;
    if (fs::root_volume::read_file(
            "/apps/hello",
            hello,
            sizeof(hello),
            &hello_bytes,
            &hello_size) != fs::vfs::Status::Ok ||
        hello_bytes != hello_info.size || hello_size != hello_info.size ||
        static_cast<unsigned char>(hello[0]) != 0x7FU ||
        hello[1] != 'E' || hello[2] != 'L' || hello[3] != 'F') {
        std::cerr << "cannot read generated /apps/hello ELF through VFS\n";
        std::fclose(file);
        return 1;
    }
    for (const char* path : kRequiredRuntimePaths) {
        if (!is_x86_64_elf(path)) {
            std::cerr << "required rootfs ELF is missing or invalid: "
                      << path << '\n';
            std::fclose(file);
            return 1;
        }
    }
    std::cout << "Foundation root FAT32/VFS runtime payload ("
              << (sizeof(kRequiredRuntimePaths) /
                  sizeof(kRequiredRuntimePaths[0]))
              << " ELF64 programs): PASS\n";
    std::fclose(file);
    return 0;
}
