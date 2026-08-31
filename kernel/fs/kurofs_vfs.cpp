#include "kurofs_vfs.hpp"

namespace fs::kurofs_vfs {
namespace {

vfs::Status map_status(kurofs::Status status) {
    switch (status) {
        case kurofs::Status::Ok: return vfs::Status::Ok;
        case kurofs::Status::NotMounted: return vfs::Status::BackendFailure;
        case kurofs::Status::InvalidArgument: return vfs::Status::InvalidArgument;
        case kurofs::Status::UnsupportedSectorSize: return vfs::Status::Unsupported;
        case kurofs::Status::DeviceTooSmall: return vfs::Status::OutOfRange;
        case kurofs::Status::ArithmeticOverflow: return vfs::Status::ArithmeticOverflow;
        case kurofs::Status::InvalidSuperblock:
        case kurofs::Status::CorruptSuperblock:
        case kurofs::Status::InvalidGeometry:
        case kurofs::Status::InvalidRootInode:
        case kurofs::Status::InvalidExtent:
        case kurofs::Status::CorruptDirectory:
            return vfs::Status::CorruptFilesystem;
        case kurofs::Status::StaleInode: return vfs::Status::StaleHandle;
        case kurofs::Status::NotFound: return vfs::Status::NotFound;
        case kurofs::Status::AlreadyExists: return vfs::Status::AlreadyExists;
        case kurofs::Status::NotDirectory: return vfs::Status::NotDirectory;
        case kurofs::Status::NameTooLong: return vfs::Status::NameTooLong;
        case kurofs::Status::NoSpace: return vfs::Status::NoSpace;
        case kurofs::Status::BlockDeviceError: return vfs::Status::IoError;
    }
    return vfs::Status::BackendFailure;
}

vfs::FileStat make_stat(const kurofs::Inode& inode) {
    return {
        inode.type == kurofs::InodeType::Directory
            ? vfs::NodeType::Directory
            : vfs::NodeType::Regular,
        inode.type == kurofs::InodeType::Regular
            ? vfs::NodeFlags::Seekable
            : vfs::NodeFlags::None,
        inode.size,
    };
}

bool next_generation(uint32_t current, uint32_t* output) {
    if (output == nullptr || current == UINT32_MAX) return false;
    const uint32_t next = current + 1U;
    if (next == 0U) return false;
    *output = next;
    return true;
}

Adapter* require_adapter(void* context) {
    Adapter* const adapter = static_cast<Adapter*>(context);
    if (adapter == nullptr || !adapter->initialized ||
        adapter->filesystem == nullptr ||
        !kurofs::is_mounted(adapter->filesystem)) {
        return nullptr;
    }
    return adapter;
}

vfs::Status bounded_path_length(const char* path, size_t* output) {
    if (path == nullptr || output == nullptr) return vfs::Status::InvalidArgument;
    for (size_t index = 0U; index <= vfs::MAX_PATH_LENGTH; ++index) {
        if (path[index] == '\0') {
            *output = index;
            return vfs::Status::Ok;
        }
    }
    return vfs::Status::PathTooLong;
}

vfs::Status resolve_path(Adapter* adapter, const char* path, kurofs::Inode* output) {
    if (adapter == nullptr || path == nullptr || output == nullptr) {
        return vfs::Status::InvalidArgument;
    }
    size_t length = 0U;
    vfs::Status path_status = bounded_path_length(path, &length);
    if (path_status != vfs::Status::Ok) return path_status;
    if (length == 0U || path[0] != '/' ||
        (length > 1U && path[length - 1U] == '/')) {
        return vfs::Status::InvalidPath;
    }

    kurofs::Inode current{};
    kurofs::Status status = kurofs::read_inode(
        adapter->filesystem, kurofs::ROOT_INODE, &current);
    if (status != kurofs::Status::Ok) return map_status(status);
    if (length == 1U) {
        *output = current;
        return vfs::Status::Ok;
    }

    size_t position = 1U;
    while (position < length) {
        if (current.type != kurofs::InodeType::Directory) {
            return vfs::Status::NotDirectory;
        }
        const size_t begin = position;
        while (position < length && path[position] != '/') ++position;
        const size_t component_length = position - begin;
        if (component_length == 0U) return vfs::Status::InvalidPath;
        if (component_length > kurofs::MAX_DIRECTORY_NAME) {
            return vfs::Status::NameTooLong;
        }
        char component[kurofs::MAX_DIRECTORY_NAME + 1U]{};
        for (size_t index = 0U; index < component_length; ++index) {
            component[index] = path[begin + index];
        }
        component[component_length] = '\0';

        kurofs::DirectoryEntry entry{};
        status = kurofs::directory_lookup(
            adapter->filesystem, &current, component, &entry);
        if (status != kurofs::Status::Ok) return map_status(status);

        kurofs::Inode child{};
        status = kurofs::read_inode(adapter->filesystem, entry.inode_id, &child);
        if (status != kurofs::Status::Ok) return map_status(status);
        if (child.generation != entry.inode_generation || child.type != entry.type) {
            return vfs::Status::CorruptFilesystem;
        }
        current = child;
        if (position < length) {
            ++position;
            if (position == length) return vfs::Status::InvalidPath;
        }
    }
    *output = current;
    return vfs::Status::Ok;
}

OpenSlot* resolve_slot(Adapter* adapter, const vfs::BackendFile* file) {
    if (adapter == nullptr || file == nullptr || file->words[0] == 0U) return nullptr;
    const uintptr_t encoded = file->words[0] - 1U;
    if (encoded >= vfs::MAX_OPEN_FILES) return nullptr;
    OpenSlot& slot = adapter->open_slots[encoded];
    if (!slot.active || static_cast<uintptr_t>(slot.generation) != file->words[1]) {
        return nullptr;
    }
    return &slot;
}

vfs::Status refresh_slot(Adapter* adapter, OpenSlot* slot) {
    if (adapter == nullptr || slot == nullptr) return vfs::Status::InvalidHandle;
    kurofs::Inode current{};
    const kurofs::Status status = kurofs::read_inode(
        adapter->filesystem, slot->inode.id, &current);
    if (status != kurofs::Status::Ok) return map_status(status);
    if (current.generation != slot->inode.generation) return vfs::Status::StaleHandle;
    if (current.type != slot->inode.type) return vfs::Status::CorruptFilesystem;
    slot->inode = current;
    return vfs::Status::Ok;
}

vfs::Status stat_path(void* context, const char* path, vfs::FileStat* output) {
    Adapter* const adapter = require_adapter(context);
    if (adapter == nullptr || output == nullptr) return vfs::Status::InvalidArgument;
    kurofs::Inode inode{};
    const vfs::Status status = resolve_path(adapter, path, &inode);
    if (status == vfs::Status::Ok) *output = make_stat(inode);
    return status;
}

vfs::Status open_file(
    void* context,
    const char* path,
    vfs::OpenFlags,
    vfs::BackendFile* output) {
    Adapter* const adapter = require_adapter(context);
    if (adapter == nullptr || path == nullptr || output == nullptr) {
        return vfs::Status::InvalidArgument;
    }
    *output = {};
    kurofs::Inode inode{};
    vfs::Status status = resolve_path(adapter, path, &inode);
    if (status != vfs::Status::Ok) return status;

    for (size_t index = 0U; index < vfs::MAX_OPEN_FILES; ++index) {
        OpenSlot& slot = adapter->open_slots[index];
        if (slot.active) continue;
        uint32_t generation = 0U;
        if (!next_generation(slot.generation, &generation)) continue;
        slot.active = true;
        slot.generation = generation;
        slot.inode = inode;
        output->words[0] = static_cast<uintptr_t>(index + 1U);
        output->words[1] = static_cast<uintptr_t>(generation);
        return vfs::Status::Ok;
    }
    return vfs::Status::OpenFileTableFull;
}

void close_file(void* context, const vfs::BackendFile* file) {
    Adapter* const adapter = require_adapter(context);
    OpenSlot* const slot = resolve_slot(adapter, file);
    if (slot == nullptr) return;
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
    vfs::Status status = refresh_slot(adapter, slot);
    if (status != vfs::Status::Ok) return status;
    if (slot->inode.type == kurofs::InodeType::Directory) return vfs::Status::IsDirectory;
    return map_status(kurofs::read_inode_data(
        adapter->filesystem, &slot->inode, offset, buffer, size, bytes_read));
}

vfs::Status stat_open(
    void* context,
    const vfs::BackendFile* file,
    vfs::FileStat* output) {
    Adapter* const adapter = require_adapter(context);
    OpenSlot* const slot = resolve_slot(adapter, file);
    if (adapter == nullptr || slot == nullptr || output == nullptr) {
        return vfs::Status::InvalidHandle;
    }
    const vfs::Status status = refresh_slot(adapter, slot);
    if (status != vfs::Status::Ok) return status;
    *output = make_stat(slot->inode);
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
    if (adapter == nullptr || slot == nullptr || output == nullptr || next_cookie == nullptr) {
        return vfs::Status::InvalidHandle;
    }
    vfs::Status status = refresh_slot(adapter, slot);
    if (status != vfs::Status::Ok) return status;
    if (slot->inode.type != kurofs::InodeType::Directory) return vfs::Status::NotDirectory;

    kurofs::DirectoryEntry entry{};
    const kurofs::Status directory_status = kurofs::directory_entry_at(
        adapter->filesystem, &slot->inode, cookie, &entry);
    if (directory_status == kurofs::Status::NotFound) return vfs::Status::EndOfDirectory;
    if (directory_status != kurofs::Status::Ok) return map_status(directory_status);
    if (cookie == UINT64_MAX) return vfs::Status::ArithmeticOverflow;

    kurofs::Inode child{};
    const kurofs::Status child_status = kurofs::read_inode(
        adapter->filesystem, entry.inode_id, &child);
    if (child_status != kurofs::Status::Ok) return map_status(child_status);
    if (child.generation != entry.inode_generation || child.type != entry.type) {
        return vfs::Status::CorruptFilesystem;
    }

    vfs::DirectoryEntry candidate{};
    for (size_t index = 0U; index < entry.name_length; ++index) {
        candidate.name[index] = entry.name[index];
    }
    candidate.name[entry.name_length] = '\0';
    candidate.name_length = entry.name_length;
    candidate.info = make_stat(child);
    *output = candidate;
    *next_cookie = cookie + 1U;
    return vfs::Status::Ok;
}

vfs::Status sync_filesystem(void* context) {
    Adapter* const adapter = require_adapter(context);
    if (adapter == nullptr || adapter->filesystem->device == nullptr) {
        return vfs::Status::BackendFailure;
    }
    return storage::block::flush(adapter->filesystem->device) == storage::block::Status::Ok
        ? vfs::Status::Ok : vfs::Status::IoError;
}

} // namespace

vfs::Status initialize(
    Adapter* adapter,
    kurofs::FileSystem* filesystem,
    vfs::FileSystem* output_filesystem) {
    if (adapter == nullptr || filesystem == nullptr || output_filesystem == nullptr ||
        !kurofs::is_mounted(filesystem)) {
        return vfs::Status::InvalidArgument;
    }

    Adapter candidate{};
    candidate.filesystem = filesystem;
    candidate.initialized = true;

    vfs::Operations operations{};
    operations.stat_path = stat_path;
    operations.open = open_file;
    operations.close = close_file;
    operations.read = read_file;
    operations.stat_open = stat_open;
    operations.readdir = read_directory;
    operations.sync = sync_filesystem;

    *adapter = candidate;
    *output_filesystem = vfs::FileSystem{adapter, operations, true};
    return vfs::Status::Ok;
}

} // namespace fs::kurofs_vfs
