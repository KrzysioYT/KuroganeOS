#include "package_vfs.hpp"

namespace install::package_vfs {
namespace {

using fs::vfs::BackendFile;
using fs::vfs::DirectoryEntry;
using fs::vfs::FileStat;
using fs::vfs::NodeFlags;
using fs::vfs::NodeType;
using fs::vfs::OpenFlags;
using fs::vfs::Status;

constexpr uintptr_t kDirectoryBit =
    static_cast<uintptr_t>(1) << (sizeof(uintptr_t) * 8U - 1U);
constexpr uintptr_t kDirectoryIndexMask = ~kDirectoryBit;

// Package manifests contain bounded 7-bit FAT 8.3 paths. Treat path
// components with the same case-folding rule as the installed FAT root,
// so live Try mode and installed mode do not resolve files differently.
constexpr char fold_ascii(char c) {
    return c >= 'a' && c <= 'z'
        ? static_cast<char>(c - ('a' - 'A')) : c;
}

bool path_equal(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    size_t index = 0U;
    while (left[index] != '\0' &&
           fold_ascii(left[index]) == fold_ascii(right[index]))
        ++index;
    return fold_ascii(left[index]) == fold_ascii(right[index]);
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
        const package::File& file = adapter.files[index];
        if (file.destination != package::DESTINATION_ROOT) {
            continue;
        }
        size_t cursor = 0U;
        while (cursor < length &&
               fold_ascii(file.path[cursor]) == fold_ascii(path[cursor]))
            ++cursor;
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
        const package::File& file = adapter.files[index];
        if (file.destination != package::DESTINATION_ROOT ||
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
        const size_t length = path_length(path);
        uintptr_t directory_index = 0U;
        if (!path_equal(path, "/")) {
            for (size_t index = 0U;
                 index < adapter.package.file_count;
                 ++index) {
                const package::File& file = adapter.files[index];
                if (file.destination != package::DESTINATION_ROOT) continue;
                size_t cursor = 0U;
                while (cursor < length &&
                       fold_ascii(file.path[cursor]) ==
                           fold_ascii(path[cursor])) {
                    ++cursor;
                }
                if (cursor == length && file.path[cursor] == '/') {
                    directory_index = static_cast<uintptr_t>(index + 1U);
                    break;
                }
            }
            if (directory_index == 0U) return Status::BackendFailure;
        }
        backend->words[0] = kDirectoryBit | directory_index;
        backend->words[1] = static_cast<uintptr_t>(length);
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
        adapter.files[index].destination != package::DESTINATION_ROOT) {
        return Status::StaleHandle;
    }
    *file = adapter.files[index];
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

bool immediate_child(
    const package::File& file,
    const char* directory_path,
    size_t directory_length,
    bool root_directory,
    const char** name,
    size_t* name_length,
    bool* is_directory) {
    if (file.destination != package::DESTINATION_ROOT ||
        directory_path == nullptr || name == nullptr ||
        name_length == nullptr) {
        return false;
    }

    const char* suffix = nullptr;
    if (root_directory) {
        if (file.path[0] != '/' || file.path[1] == '\0') return false;
        suffix = file.path + 1U;
    } else {
        for (size_t index = 0U; index < directory_length; ++index) {
            if (fold_ascii(file.path[index]) !=
                fold_ascii(directory_path[index])) return false;
        }
        if (file.path[directory_length] != '/' ||
            file.path[directory_length + 1U] == '\0') {
            return false;
        }
        suffix = file.path + directory_length + 1U;
    }

    size_t length = 0U;
    while (suffix[length] != '\0' && suffix[length] != '/') ++length;
    if (length == 0U || length > fs::vfs::MAX_NAME_LENGTH) return false;
    *name = suffix;
    *name_length = length;
    if (is_directory != nullptr) *is_directory = suffix[length] == '/';
    return true;
}

Status read_directory(
    void* context,
    const BackendFile* backend,
    uint64_t cookie,
    DirectoryEntry* entry,
    uint64_t* next_cookie) {
    if (context == nullptr || backend == nullptr || entry == nullptr ||
        next_cookie == nullptr) {
        return Status::InvalidArgument;
    }
    const auto& adapter = *static_cast<const Adapter*>(context);
    if ((backend->words[0] & kDirectoryBit) == 0U) {
        return Status::InvalidHandle;
    }

    const uintptr_t encoded_index =
        backend->words[0] & kDirectoryIndexMask;
    const uintptr_t encoded_length = backend->words[1];
    if (encoded_index > adapter.package.file_count ||
        encoded_length == 0U ||
        encoded_length > package::MAXIMUM_PATH) {
        return Status::InvalidHandle;
    }

    const bool root_directory = encoded_index == 0U;
    const char* directory_path = "/";
    const size_t directory_length = static_cast<size_t>(encoded_length);
    if (!root_directory) {
        const package::File& representative =
            adapter.files[static_cast<size_t>(encoded_index - 1U)];
        const size_t representative_length = path_length(representative.path);
        if (representative.destination != package::DESTINATION_ROOT ||
            representative_length <= directory_length ||
            representative.path[directory_length] != '/') {
            return Status::InvalidHandle;
        }
        directory_path = representative.path;
    } else if (directory_length != 1U) {
        return Status::InvalidHandle;
    }

    uint64_t visible_index = 0U;
    for (size_t index = 0U; index < adapter.package.file_count; ++index) {
        const package::File& candidate = adapter.files[index];
        const char* candidate_name = nullptr;
        size_t candidate_length = 0U;
        bool candidate_is_directory = false;
        if (!immediate_child(
                candidate,
                directory_path,
                directory_length,
                root_directory,
                &candidate_name,
                &candidate_length,
                &candidate_is_directory)) {
            continue;
        }

        bool duplicate = false;
        for (size_t earlier = 0U; earlier < index; ++earlier) {
            const char* earlier_name = nullptr;
            size_t earlier_length = 0U;
            if (immediate_child(
                    adapter.files[earlier],
                    directory_path,
                    directory_length,
                    root_directory,
                    &earlier_name,
                    &earlier_length,
                    nullptr) &&
                candidate_length == earlier_length) {
                size_t character = 0U;
                while (character < candidate_length &&
                       fold_ascii(candidate_name[character]) ==
                           fold_ascii(earlier_name[character])) {
                    ++character;
                }
                duplicate = character == candidate_length;
                if (duplicate) break;
            }
        }
        if (duplicate) continue;
        if (visible_index == cookie) {
            *entry = {};
            for (size_t character = 0U;
                 character < candidate_length;
                 ++character) {
                entry->name[character] = candidate_name[character];
            }
            entry->name[candidate_length] = '\0';
            entry->name_length = candidate_length;
            entry->info = {
                candidate_is_directory ? NodeType::Directory : NodeType::Regular,
                NodeFlags::Seekable,
                candidate_is_directory ? 0U : candidate.size,
            };
            *next_cookie = cookie + 1U;
            return Status::Ok;
        }
        ++visible_index;
    }
    return Status::EndOfDirectory;
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
        package.file_count == 0U ||
        package.file_count > package::MAXIMUM_FILES) {
        return fs::vfs::Status::InvalidArgument;
    }
    adapter->package = package;
    for (size_t index = 0U; index < package.file_count; ++index) {
        if (package::file_at(package, index, &adapter->files[index]) !=
            package::Status::Ok) {
            return fs::vfs::Status::BackendFailure;
        }
    }
    *filesystem = {};
    filesystem->context = adapter;
    filesystem->read_only = true;
    filesystem->operations.stat_path = stat_path;
    filesystem->operations.open = open_file;
    filesystem->operations.close = close_file;
    filesystem->operations.read = read_file;
    filesystem->operations.write = write_file;
    filesystem->operations.stat_open = stat_open;
    filesystem->operations.readdir = read_directory;
    filesystem->operations.create = read_only_mutation;
    filesystem->operations.unlink = read_only_mutation;
    filesystem->operations.rename = read_only_rename;
    filesystem->operations.mkdir = read_only_mutation;
    filesystem->operations.rmdir = read_only_mutation;
    filesystem->operations.sync = read_only_sync;
    return fs::vfs::Status::Ok;
}

} // namespace install::package_vfs
