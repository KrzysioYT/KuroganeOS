#include "fat32_reliable_file.hpp"

namespace install::fat32_reliable_file {
namespace {

reliable_file::BackendStatus map_status(fs::fat32::Status status) {
    switch (status) {
        case fs::fat32::Status::Ok:
            return reliable_file::BackendStatus::Ok;
        case fs::fat32::Status::NotFound:
            return reliable_file::BackendStatus::NotFound;
        case fs::fat32::Status::AlreadyExists:
            return reliable_file::BackendStatus::AlreadyExists;
        default:
            return reliable_file::BackendStatus::Failure;
    }
}

reliable_file::BackendStatus stat_file(void* context, const char* path) {
    auto* filesystem = static_cast<fs::fat32::FileSystem*>(context);
    if (filesystem == nullptr || path == nullptr) {
        return reliable_file::BackendStatus::Failure;
    }
    fs::fat32::Stat info{};
    return map_status(fs::fat32::stat(filesystem, path, &info));
}

reliable_file::BackendStatus create_file(void* context, const char* path) {
    auto* filesystem = static_cast<fs::fat32::FileSystem*>(context);
    return filesystem == nullptr
        ? reliable_file::BackendStatus::Failure
        : map_status(fs::fat32::create(filesystem, path));
}

reliable_file::BackendStatus write_file(
    void* context,
    const char* path,
    uint64_t offset,
    const void* data,
    size_t size) {
    auto* filesystem = static_cast<fs::fat32::FileSystem*>(context);
    return filesystem == nullptr
        ? reliable_file::BackendStatus::Failure
        : map_status(fs::fat32::write(filesystem, path, offset, data, size));
}

reliable_file::BackendStatus unlink_file(void* context, const char* path) {
    auto* filesystem = static_cast<fs::fat32::FileSystem*>(context);
    return filesystem == nullptr
        ? reliable_file::BackendStatus::Failure
        : map_status(fs::fat32::unlink(filesystem, path));
}

reliable_file::BackendStatus rename_file(
    void* context,
    const char* source_path,
    const char* destination_path) {
    auto* filesystem = static_cast<fs::fat32::FileSystem*>(context);
    return filesystem == nullptr
        ? reliable_file::BackendStatus::Failure
        : map_status(fs::fat32::rename(
              filesystem, source_path, destination_path));
}

reliable_file::BackendStatus sync_filesystem(void* context) {
    auto* filesystem = static_cast<fs::fat32::FileSystem*>(context);
    return filesystem == nullptr
        ? reliable_file::BackendStatus::Failure
        : map_status(fs::fat32::sync(filesystem));
}

} // namespace

reliable_file::Status replace(
    fs::fat32::FileSystem* filesystem,
    const reliable_file::Paths& paths,
    const void* data,
    size_t size) {
    if (filesystem == nullptr) {
        return reliable_file::Status::InvalidArgument;
    }
    const reliable_file::Operations operations{
        filesystem,
        stat_file,
        create_file,
        write_file,
        unlink_file,
        rename_file,
        sync_filesystem,
    };
    return reliable_file::replace(operations, paths, data, size);
}

} // namespace install::fat32_reliable_file
