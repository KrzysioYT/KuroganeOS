#include "../kernel/fs/kurofs.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace system_metrics { void record_disk_blocks(uint64_t) {} }

namespace {

struct FileDevice {
    std::FILE* file;
    uint64_t sector_count;
};

bool seek_sector(FileDevice* device, uint64_t sector) {
    if (device == nullptr || device->file == nullptr ||
        sector > static_cast<uint64_t>(LONG_MAX) / 512U) {
        return false;
    }
    return std::fseek(
        device->file, static_cast<long>(sector * 512U), SEEK_SET) == 0;
}

storage::block::Status read_blocks(
    void* context,
    uint64_t first,
    uint64_t count,
    void* output) {
    auto* device = static_cast<FileDevice*>(context);
    if (device == nullptr || output == nullptr || first >= device->sector_count ||
        count > device->sector_count - first || !seek_sector(device, first)) {
        return storage::block::Status::OutOfRange;
    }
    const size_t bytes = static_cast<size_t>(count * 512U);
    return std::fread(output, 1U, bytes, device->file) == bytes
        ? storage::block::Status::Ok
        : storage::block::Status::IoError;
}

storage::block::Status write_blocks(
    void* context,
    uint64_t first,
    uint64_t count,
    const void* source) {
    auto* device = static_cast<FileDevice*>(context);
    if (device == nullptr || source == nullptr || first >= device->sector_count ||
        count > device->sector_count - first || !seek_sector(device, first)) {
        return storage::block::Status::OutOfRange;
    }
    const size_t bytes = static_cast<size_t>(count * 512U);
    return std::fwrite(source, 1U, bytes, device->file) == bytes
        ? storage::block::Status::Ok
        : storage::block::Status::IoError;
}

storage::block::Status flush_device(void* context) {
    auto* device = static_cast<FileDevice*>(context);
    return device != nullptr && device->file != nullptr &&
        std::fflush(device->file) == 0
        ? storage::block::Status::Ok
        : storage::block::Status::IoError;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: kurofs-image IMAGE\n");
        return 2;
    }
    std::FILE* file = std::fopen(argv[1], "r+b");
    if (file == nullptr || std::fseek(file, 0L, SEEK_END) != 0) {
        std::fprintf(stderr, "cannot open KuroFS image: %s\n", argv[1]);
        if (file != nullptr) std::fclose(file);
        return 1;
    }
    const long bytes = std::ftell(file);
    if (bytes < 512L * 32L || (bytes % 512L) != 0) {
        std::fprintf(stderr, "KuroFS image must contain at least 32 sectors\n");
        std::fclose(file);
        return 1;
    }
    FileDevice context{file, static_cast<uint64_t>(bytes / 512L)};
    storage::block::Device device{
        &context,
        512U,
        context.sector_count,
        read_blocks,
        write_blocks,
        flush_device,
    };
    fs::kurofs::Status status = fs::kurofs::format(
        &device, fs::kurofs::DEFAULT_INODE_COUNT);
    if (status == fs::kurofs::Status::Ok) {
        fs::kurofs::FileSystem filesystem{};
        status = fs::kurofs::mount(&filesystem, &device);
    }
    if (std::fclose(file) != 0 && status == fs::kurofs::Status::Ok) {
        std::fprintf(stderr, "failed to close KuroFS image\n");
        return 1;
    }
    if (status != fs::kurofs::Status::Ok) {
        std::fprintf(
            stderr, "KuroFS image format failed: %s\n",
            fs::kurofs::status_message(status));
        return 1;
    }
    std::printf(
        "Formatted raw KuroFS v%u image: %s (%ld bytes, %u inodes)\n",
        fs::kurofs::FORMAT_VERSION,
        argv[1],
        bytes,
        fs::kurofs::DEFAULT_INODE_COUNT);
    return 0;
}
