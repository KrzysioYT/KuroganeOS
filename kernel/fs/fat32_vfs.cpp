#include "fat32_vfs.hpp"

namespace fs::fat32_vfs {

namespace {

vfs::Status map_status(fat32::Status status) {
    switch (status) {
        case fat32::Status::Ok:
            return vfs::Status::Ok;
        case fat32::Status::InvalidArgument:
            return vfs::Status::InvalidArgument;
        case fat32::Status::InvalidPath:
        case fat32::Status::PathEscapesRoot:
            return vfs::Status::InvalidPath;
        case fat32::Status::PathTooLong:
            return vfs::Status::PathTooLong;
        case fat32::Status::NameTooLong:
            return vfs::Status::NameTooLong;
        case fat32::Status::PathTooDeep:
            return vfs::Status::PathTooDeep;
        case fat32::Status::NotFound:
            return vfs::Status::NotFound;
        case fat32::Status::NotDirectory:
            return vfs::Status::NotDirectory;
        case fat32::Status::IsDirectory:
            return vfs::Status::IsDirectory;
        case fat32::Status::EndOfDirectory:
            return vfs::Status::EndOfDirectory;
        case fat32::Status::UnsupportedNameEncoding:
        case fat32::Status::UnsupportedSectorSize:
        case fat32::Status::UnsupportedGeometry:
        case fat32::Status::Unsupported:
            return vfs::Status::Unsupported;
        case fat32::Status::ArithmeticOverflow:
            return vfs::Status::ArithmeticOverflow;
        case fat32::Status::BlockDeviceError:
            return vfs::Status::IoError;
        case fat32::Status::ReadOnly:
            return vfs::Status::ReadOnly;
        case fat32::Status::AlreadyExists:
            return vfs::Status::AlreadyExists;
        case fat32::Status::DirectoryNotEmpty:
            return vfs::Status::DirectoryNotEmpty;
        case fat32::Status::NoSpace:
            return vfs::Status::NoSpace;
        case fat32::Status::NotMounted:
            return vfs::Status::BackendFailure;
        case fat32::Status::InvalidBootSector:
        case fat32::Status::CorruptFsInfo:
        case fat32::Status::CorruptBackupBoot:
        case fat32::Status::FatMirrorMismatch:
        case fat32::Status::CorruptFat:
        case fat32::Status::CorruptDirectory:
        case fat32::Status::CorruptChain:
        case fat32::Status::ChainCycle:
        case fat32::Status::TruncatedChain:
            return vfs::Status::CorruptFilesystem;
    }
    return vfs::Status::BackendFailure;
}

vfs::FileStat make_stat(const fat32::Node& node) {
    return {
        node.type == fat32::EntryType::Directory
            ? vfs::NodeType::Directory
            : vfs::NodeType::Regular,
        node.type == fat32::EntryType::File
            ? vfs::NodeFlags::Seekable
            : vfs::NodeFlags::None,
        node.size,
    };
}

vfs::FileStat make_stat(const fat32::Stat& info) {
    return {
        info.type == fat32::EntryType::Directory
            ? vfs::NodeType::Directory
            : vfs::NodeType::Regular,
        info.type == fat32::EntryType::File
            ? vfs::NodeFlags::Seekable
            : vfs::NodeFlags::None,
        info.size,
    };
}

uint32_t next_generation(uint32_t current) {
    ++current;
    return current == 0U ? 1U : current;
}

bool copy_path(char* destination, const char* source) {
    if (destination == nullptr || source == nullptr) {
        return false;
    }
    for (size_t index = 0U; index <= vfs::MAX_PATH_LENGTH; ++index) {
        destination[index] = source[index];
        if (source[index] == '\0') {
            return true;
        }
    }
    destination[0] = '\0';
    return false;
}

Adapter* require_adapter(void* context) {
    Adapter* const adapter = static_cast<Adapter*>(context);
    if (adapter == nullptr || !adapter->initialized ||
        adapter->filesystem == nullptr ||
        !fat32::mounted(adapter->filesystem)) {
        return nullptr;
    }
    return adapter;
}

OpenSlot* resolve_slot(Adapter* adapter, const vfs::BackendFile* file) {
    if (adapter == nullptr || file == nullptr || file->words[0] == 0U) {
        return nullptr;
    }
    const uintptr_t encoded_index = file->words[0] - 1U;
    if (encoded_index >= vfs::MAX_OPEN_FILES) {
        return nullptr;
    }
    OpenSlot& slot = adapter->open_slots[encoded_index];
    if (!slot.active ||
        static_cast<uintptr_t>(slot.generation) != file->words[1]) {
        return nullptr;
    }
    return &slot;
}

vfs::Status stat_path(
    void* context,
    const char* path,
    vfs::FileStat* output) {
    Adapter* const adapter = require_adapter(context);
    if (adapter == nullptr || output == nullptr) {
        return vfs::Status::InvalidArgument;
    }
    fat32::Stat info{};
    const fat32::Status status = fat32::stat(
        adapter->filesystem, path, &info);
    if (status == fat32::Status::Ok) {
        *output = make_stat(info);
    }
    return map_status(status);
}

vfs::Status open_file(
    void* context,
    const char* path,
    vfs::OpenFlags flags,
    vfs::BackendFile* output) {
    Adapter* const adapter = require_adapter(context);
    if (adapter == nullptr || path == nullptr || output == nullptr) {
        return vfs::Status::InvalidArgument;
    }
    (void)flags;
    *output = {};
    fat32::Node node{};
    const fat32::Status lookup_status = fat32::lookup(
        adapter->filesystem, path, &node);
    if (lookup_status != fat32::Status::Ok) {
        return map_status(lookup_status);
    }

    for (size_t index = 0U; index < vfs::MAX_OPEN_FILES; ++index) {
        OpenSlot& slot = adapter->open_slots[index];
        if (slot.active) {
            continue;
        }
        char copied_path[vfs::MAX_PATH_LENGTH + 1U]{};
        if (!copy_path(copied_path, path)) {
            return vfs::Status::PathTooLong;
        }
        slot.generation = next_generation(slot.generation);
        slot.node = node;
        for (size_t character = 0U;
             character <= vfs::MAX_PATH_LENGTH;
             ++character) {
            slot.path[character] = copied_path[character];
            if (copied_path[character] == '\0') {
                break;
            }
        }
        slot.active = true;
        output->words[0] = static_cast<uintptr_t>(index + 1U);
        output->words[1] = static_cast<uintptr_t>(slot.generation);
        return vfs::Status::Ok;
    }
    return vfs::Status::OpenFileTableFull;
}

void close_file(void* context, const vfs::BackendFile* file) {
    Adapter* const adapter = require_adapter(context);
    OpenSlot* const slot = resolve_slot(adapter, file);
    if (slot == nullptr) {
        return;
    }
    const uint32_t generation = slot->generation;
    *slot = {};
    slot->generation = generation;
}

vfs::Status read_file(
    void* context,
    const vfs::BackendFile* file,
    uint64_t offset,
    void* buffer,
    size_t size,
    size_t* bytes_read) {
    Adapter* const adapter = require_adapter(context);
    OpenSlot* const slot = resolve_slot(adapter, file);
    if (adapter == nullptr || slot == nullptr || bytes_read == nullptr) {
        return vfs::Status::InvalidHandle;
    }
    return map_status(fat32::read_node(
        adapter->filesystem,
        &slot->node,
        offset,
        buffer,
        size,
        bytes_read));
}

vfs::Status write_file(
    void* context,
    const vfs::BackendFile* file,
    uint64_t offset,
    const void* buffer,
    size_t size,
    size_t* bytes_written) {
    Adapter* const adapter = require_adapter(context);
    OpenSlot* const slot = resolve_slot(adapter, file);
    if (adapter == nullptr || slot == nullptr || bytes_written == nullptr ||
        (buffer == nullptr && size != 0U)) {
        return vfs::Status::InvalidHandle;
    }
    *bytes_written = 0U;
    const fat32::Status status = fat32::write(
        adapter->filesystem, slot->path, offset, buffer, size);
    if (status != fat32::Status::Ok) {
        return map_status(status);
    }
    fat32::Node refreshed{};
    const fat32::Status lookup_status = fat32::lookup(
        adapter->filesystem, slot->path, &refreshed);
    if (lookup_status != fat32::Status::Ok) {
        return map_status(lookup_status);
    }
    slot->node = refreshed;
    *bytes_written = size;
    return vfs::Status::Ok;
}

vfs::Status stat_open(
    void* context,
    const vfs::BackendFile* file,
    vfs::FileStat* output) {
    Adapter* const adapter = require_adapter(context);
    OpenSlot* const slot = resolve_slot(adapter, file);
    if (slot == nullptr || output == nullptr) {
        return vfs::Status::InvalidHandle;
    }
    *output = make_stat(slot->node);
    return vfs::Status::Ok;
}

vfs::Status read_directory(
    void* context,
    const vfs::BackendFile* file,
    uint64_t cookie,
    vfs::DirectoryEntry* output,
    uint64_t* next_cookie) {
    Adapter* const adapter = require_adapter(context);
    OpenSlot* const slot = resolve_slot(adapter, file);
    if (adapter == nullptr || slot == nullptr || output == nullptr ||
        next_cookie == nullptr) {
        return vfs::Status::InvalidHandle;
    }
    fat32::DirectoryEntry entry{};
    uint64_t candidate_cookie = cookie;
    const fat32::Status status = fat32::readdir(
        adapter->filesystem,
        slot->path,
        &candidate_cookie,
        &entry);
    if (status != fat32::Status::Ok) {
        return map_status(status);
    }
    if (entry.name_length > vfs::MAX_NAME_LENGTH) {
        return vfs::Status::NameTooLong;
    }

    vfs::DirectoryEntry candidate{};
    for (size_t index = 0U; index <= entry.name_length; ++index) {
        candidate.name[index] = entry.name[index];
    }
    candidate.name_length = entry.name_length;
    candidate.info = make_stat(fat32::Node{
        entry.info.type,
        entry.info.attributes,
        entry.info.first_cluster,
        entry.info.size,
    });
    *output = candidate;
    *next_cookie = candidate_cookie;
    return vfs::Status::Ok;
}

vfs::Status sync_filesystem(void* context) {
    Adapter* const adapter = require_adapter(context);
    return adapter == nullptr
        ? vfs::Status::BackendFailure
        : map_status(fat32::sync(adapter->filesystem));
}

vfs::Status create_file(void* context, const char* path) {
    Adapter* const adapter = require_adapter(context);
    return adapter == nullptr
        ? vfs::Status::BackendFailure
        : map_status(fat32::create(adapter->filesystem, path));
}

vfs::Status unlink_file(void* context, const char* path) {
    Adapter* const adapter = require_adapter(context);
    return adapter == nullptr
        ? vfs::Status::BackendFailure
        : map_status(fat32::unlink(adapter->filesystem, path));
}

vfs::Status rename_file(
    void* context,
    const char* source_path,
    const char* destination_path) {
    Adapter* const adapter = require_adapter(context);
    return adapter == nullptr
        ? vfs::Status::BackendFailure
        : map_status(fat32::rename(
              adapter->filesystem, source_path, destination_path));
}

vfs::Status make_directory(void* context, const char* path) {
    Adapter* const adapter = require_adapter(context);
    return adapter == nullptr
        ? vfs::Status::BackendFailure
        : map_status(fat32::mkdir(adapter->filesystem, path));
}

vfs::Status remove_directory(void* context, const char* path) {
    Adapter* const adapter = require_adapter(context);
    return adapter == nullptr
        ? vfs::Status::BackendFailure
        : map_status(fat32::rmdir(adapter->filesystem, path));
}

} // namespace

vfs::Status initialize(
    Adapter* adapter,
    fat32::FileSystem* filesystem,
    vfs::FileSystem* output_filesystem) {
    if (adapter == nullptr || filesystem == nullptr ||
        output_filesystem == nullptr || !fat32::mounted(filesystem)) {
        return vfs::Status::InvalidArgument;
    }

    Adapter candidate{};
    candidate.filesystem = filesystem;
    candidate.initialized = true;
    const vfs::FileSystem backend{
        adapter,
        {
            stat_path,
            open_file,
            close_file,
            read_file,
            write_file,
            stat_open,
            read_directory,
            create_file,
            unlink_file,
            rename_file,
            make_directory,
            remove_directory,
            sync_filesystem,
        },
        false,
    };

    *adapter = candidate;
    *output_filesystem = backend;
    return vfs::Status::Ok;
}

} // namespace fs::fat32_vfs
