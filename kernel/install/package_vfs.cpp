#include "package_vfs.hpp"

namespace install::package_vfs {
namespace {

using fs::vfs::BackendFile;
using fs::vfs::FileStat;
using fs::vfs::NodeFlags;
using fs::vfs::NodeType;
using fs::vfs::OpenFlags;
using fs::vfs::Status;

constexpr uintptr_t kDirectoryBit =
    static_cast<uintptr_t>(1) << (sizeof(uintptr_t) * 8U - 1U);

bool path_equal(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    size_t index = 0U;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

size_t path_length(const char* path) {
    if (path == nullptr) return 0U;
    size_t length = 0U;
    while (path[length] != '\0') ++length;
    return length;
}

bool is_directory_path(const Adapter& adapter, const char* path) {
    if (path_equal(path, "/")) return true;
    const size_t length = path_length(path);
    if (length == 0U) return false;
    for (size_t index = 0U; index < adapter.package.file_count; ++index) {
        package::File file{};
        if (package::file_at(adapter.package, index, &file) !=
            package::Status::Ok ||
            file.destination != package::DESTINATION_ROOT) {
            continue;
        }
        size_t cursor = 0U;
        while (cursor < length && file.path[cursor] == path[cursor]) ++cursor;
        if (cursor == length && file.path[cursor] == '/') return true;
    }
    return false;
}

bool find_file(
    const Adapter& adapter,
    const char* path,
    size_t* out_index,
    package::File* out_file) {
    for (size_t index = 0U; index < adapter.package.file_count; ++index) {
        package::File file{};
        if (package::file_at(adapter.package, index, &file) !=
            package::Status::Ok ||
            file.destination != package::DESTINATION_ROOT ||
            !path_equal(file.path, path)) {
            continue;
        }
        if (out_index != nullptr) *out_index = index;
        if (out_file != nullptr) *out_file = file;
        return true;
    }
    return false;
}

Status stat_path(void* context, const char* path, FileStat* info) {
    if (context == nullptr || path == nullptr || info == nullptr) {
        return Status::InvalidArgument;
    }
    const auto& adapter = *static_cast<const Adapter*>(context);
    package::File file{};
    if (find_file(adapter, path, nullptr, &file)) {
        *info = {NodeType::Regular, NodeFlags::Seekable, file.size};
        return Status::Ok;
    }
    if (is_directory_path(adapter, path)) {
        *info = {NodeType::Directory, NodeFlags::Seekable, 0U};
        return Status::Ok;
    }
    return Status::NotFound;
}

Status open_file(
    void* context,
    const char* path,
    OpenFlags flags,
    BackendFile* backend) {
    if (context == nullptr || path == nullptr || backend == nullptr) {
        return Status::InvalidArgument;
    }
    if (fs::vfs::has_flag(flags, OpenFlags::Write) ||
        fs::vfs::has_flag(flags, OpenFlags::Append)) {
        return Status::ReadOnly;
    }
    const auto& adapter = *static_cast<const Adapter*>(context);
    size_t index = 0U;
    if (find_file(adapter, path, &index, nullptr)) {
        backend->words[0] = index + 1U;
        backend->words[1] = 0U;
        return Status::Ok;
    }
    if (is_directory_path(adapter, path)) {
        backend->words[0] = kDirectoryBit;
        backend->words[1] = 0U;
        return Status::Ok;
    }
    return Status::NotFound;
}

void close_file(void*, const BackendFile*) {}

Status decode_regular(
    const Adapter& adapter,
    const BackendFile* backend,
    package::File* file) {
    if (backend == nullptr || file == nullptr ||
        backend->words[0] == 0U ||
        (backend->words[0] & kDirectoryBit) != 0U) {
        return Status::InvalidHandle;
    }
    const size_t index = static_cast<size_t>(backend->words[0] - 1U);
    if (index >= adapter.package.file_count ||
        package::file_at(adapter.package, index, file) != package::Status::Ok ||
        file->destination != package::DESTINATION_ROOT) {
        return Status::StaleHandle;
    }
    return Status::Ok;
}

Status read_file(
    void* context,
    const BackendFile* backend,
    uint64_t offset,
    void* buffer,
    size_t size,
    size_t* bytes_read) {
    if (bytes_read != nullptr) *bytes_read = 0U;
    if (context == nullptr || backend == nullptr || bytes_read == nullptr ||
        (size != 0U && buffer == nullptr)) {
        return Status::InvalidArgument;
    }
    const auto& adapter = *static_cast<const Adapter*>(context);
    package::File file{};
    const Status decoded = decode_regular(adapter, backend, &file);
    if (decoded != Status::Ok) return decoded;
    if (offset >= file.size) return Status::Ok;
    const size_t available = file.size - static_cast<size_t>(offset);
    const size_t count = size < available ? size : available;
    auto* output = static_cast<uint8_t*>(buffer);
    const uint8_t* input = file.data + static_cast<size_t>(offset);
    for (size_t index = 0U; index < count; ++index) output[index] = input[index];
    *bytes_read = count;
    return Status::Ok;
}

Status write_file(
    void*, const BackendFile*, uint64_t,
    const void*, size_t, size_t* bytes_written) {
    if (bytes_written != nullptr) *bytes_written = 0U;
    return Status::ReadOnly;
}

Status stat_open(void* context, const BackendFile* backend, FileStat* info) {
    if (context == nullptr || backend == nullptr || info == nullptr) {
        return Status::InvalidArgument;
    }
    if ((backend->words[0] & kDirectoryBit) != 0U) {
        *info = {NodeType::Directory, NodeFlags::Seekable, 0U};
        return Status::Ok;
    }
    const auto& adapter = *static_cast<const Adapter*>(context);
    package::File file{};
    const Status decoded = decode_regular(adapter, backend, &file);
    if (decoded != Status::Ok) return decoded;
    *info = {NodeType::Regular, NodeFlags::Seekable, file.size};
    return Status::Ok;
}

Status unsupported_readdir(
    void*, const BackendFile*, uint64_t,
    fs::vfs::DirectoryEntry*, uint64_t*) {
    return Status::Unsupported;
}

Status read_only_mutation(void*, const char*) { return Status::ReadOnly; }
Status read_only_rename(void*, const char*, const char*) { return Status::ReadOnly; }
Status read_only_sync(void*) { return Status::Ok; }

} // namespace

fs::vfs::Status initialize(
    Adapter* adapter,
    const package::View& package,
    fs::vfs::FileSystem* filesystem) {
    if (adapter == nullptr || filesystem == nullptr || package.bytes == nullptr ||
        package.file_count == 0U) {
        return fs::vfs::Status::InvalidArgument;
    }
    adapter->package = package;
    *filesystem = {};
    filesystem->context = adapter;
    filesystem->read_only = true;
    filesystem->operations.stat_path = stat_path;
    filesystem->operations.open = open_file;
    filesystem->operations.close = close_file;
    filesystem->operations.read = read_file;
    filesystem->operations.write = write_file;
    filesystem->operations.stat_open = stat_open;
    filesystem->operations.readdir = unsupported_readdir;
    filesystem->operations.create = read_only_mutation;
    filesystem->operations.unlink = read_only_mutation;
    filesystem->operations.rename = read_only_rename;
    filesystem->operations.mkdir = read_only_mutation;
    filesystem->operations.rmdir = read_only_mutation;
    filesystem->operations.sync = read_only_sync;
    return fs::vfs::Status::Ok;
}

} // namespace install::package_vfs
