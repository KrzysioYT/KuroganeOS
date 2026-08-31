#!/usr/bin/env python3
"""Add the first KuroFS VFS adapter using the production VFS and block ABI."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded match, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def write_new(path: str, content: str) -> None:
    target = ROOT / path
    if target.exists():
        raise SystemExit(f"{path}: already exists")
    target.write_text(content, encoding="utf-8")


def main() -> None:
    replace_once(
        "kernel/fs/kurofs.hpp",
        "// Persist an update to an already allocated inode. The caller supplies the\n"
        "// generation it read; success increments that generation, making stale\n"
        "// snapshots fail deterministically. Any published extent must already be\n",
        "// Persist an update to an already allocated inode. The caller supplies the\n"
        "// incarnation generation and metadata revision it read; success preserves\n"
        "// generation and increments revision, making stale writes deterministic.\n"
        "// Any published extent must already be fully allocated in the bitmap.\n",
    )
    replace_once(
        "kernel/fs/kurofs.hpp",
        "// Read at most the persisted inode size. EOF is reported as zero bytes. The\n"
        "// supplied inode snapshot must still match the on-disk generation.\n",
        "// Read at most the persisted inode size. EOF is reported as zero bytes. The\n"
        "// supplied inode snapshot must still match on-disk generation and revision.\n",
    )

    write_new("kernel/fs/kurofs_vfs.hpp", r'''#pragma once

#include <stddef.h>
#include <stdint.h>

#include "kurofs.hpp"
#include "vfs.hpp"

namespace fs::kurofs_vfs {

struct OpenSlot {
    bool active;
    uint32_t generation;
    kurofs::Inode inode;
};

struct Adapter {
    kurofs::FileSystem* filesystem;
    bool initialized;
    OpenSlot open_slots[vfs::MAX_OPEN_FILES];
};

// Expose an already mounted KuroFS instance through the common VFS contract.
// This first adapter slice is intentionally read-only; mutating callbacks stay
// absent until create/write durability is qualified through VFS itself.
vfs::Status initialize(
    Adapter* adapter,
    kurofs::FileSystem* filesystem,
    vfs::FileSystem* output_filesystem);

} // namespace fs::kurofs_vfs
''')

    write_new("kernel/fs/kurofs_vfs.cpp", r'''#include "kurofs_vfs.hpp"

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
''')

    write_new("tests/test_kurofs_vfs.cpp", r'''#include "../kernel/fs/kurofs_vfs.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace system_metrics { void record_disk_blocks(uint64_t) {} }

namespace {
constexpr uint32_t S = 512U;
constexpr uint64_t N = 512U;
uint8_t disk[S * N]{};

storage::block::Status rd(void*, uint64_t first, uint64_t count, void* output) {
    if (output == nullptr || first >= N || count > N - first) return storage::block::Status::OutOfRange;
    std::memcpy(output, disk + first * S, static_cast<size_t>(count) * S);
    return storage::block::Status::Ok;
}
storage::block::Status wr(void*, uint64_t first, uint64_t count, const void* input) {
    if (input == nullptr || first >= N || count > N - first) return storage::block::Status::OutOfRange;
    std::memcpy(disk + first * S, input, static_cast<size_t>(count) * S);
    return storage::block::Status::Ok;
}
storage::block::Status fl(void*) { return storage::block::Status::Ok; }

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

fs::vfs::Status host_stat(void*, const char* path, fs::vfs::FileStat* output) {
    if (path == nullptr || output == nullptr) return fs::vfs::Status::InvalidArgument;
    if (std::strcmp(path, "/") != 0 && std::strcmp(path, "/kuro") != 0) {
        return fs::vfs::Status::NotFound;
    }
    *output = {fs::vfs::NodeType::Directory, fs::vfs::NodeFlags::None, 0U};
    return fs::vfs::Status::Ok;
}

bool create_regular(
    fs::kurofs::FileSystem* filesystem,
    fs::kurofs::Inode* parent,
    const char* name,
    const uint8_t* payload,
    size_t payload_size,
    uint64_t* out_inode) {
    using namespace fs::kurofs;
    uint64_t inode_id = 0U;
    if (allocate_inode(filesystem, InodeType::Regular, &inode_id) != Status::Ok) return false;
    Inode inode{};
    if (read_inode(filesystem, inode_id, &inode) != Status::Ok) return false;
    const uint64_t blocks = payload_size == 0U
        ? 0U
        : (static_cast<uint64_t>(payload_size) + S - 1U) / S;
    if (blocks != 0U) {
        uint64_t extent = 0U;
        if (allocate_blocks(filesystem, blocks, &extent) != Status::Ok) return false;
        if (write_extent_data(filesystem, extent, blocks, 0U, payload, payload_size) != Status::Ok) return false;
        inode.extent_start = extent;
        inode.extent_blocks = blocks;
        inode.size = payload_size;
        if (update_inode(filesystem, &inode) != Status::Ok) return false;
    }
    if (directory_append(filesystem, parent, name, inode_id) != Status::Ok) return false;
    if (out_inode != nullptr) *out_inode = inode_id;
    return true;
}
}

int main() {
    using namespace fs;
    std::memset(disk, 0xA7, sizeof(disk));
    storage::block::Device device{nullptr, S, N, rd, wr, fl};
    if (!expect(kurofs::format(&device, 64U) == kurofs::Status::Ok, "format")) return 1;
    kurofs::FileSystem kfs{};
    if (!expect(kurofs::mount(&kfs, &device) == kurofs::Status::Ok, "mount KuroFS")) return 1;

    kurofs::Inode root{};
    if (!expect(kurofs::read_inode(&kfs, kurofs::ROOT_INODE, &root) == kurofs::Status::Ok, "read root")) return 1;
    static const uint8_t hello[] = "hello from KuroFS";
    uint64_t hello_inode = 0U;
    if (!expect(create_regular(&kfs, &root, "hello", hello, sizeof(hello) - 1U, &hello_inode), "create hello fixture")) return 1;

    uint64_t sub_id = 0U;
    if (!expect(kurofs::allocate_inode(&kfs, kurofs::InodeType::Directory, &sub_id) == kurofs::Status::Ok, "allocate subdir")) return 1;
    kurofs::Inode sub{};
    if (!expect(kurofs::read_inode(&kfs, sub_id, &sub) == kurofs::Status::Ok, "read subdir")) return 1;
    if (!expect(kurofs::directory_append(&kfs, &root, "sub", sub_id) == kurofs::Status::Ok, "attach subdir")) return 1;
    static const uint8_t nested[] = "nested payload";
    if (!expect(create_regular(&kfs, &sub, "nested", nested, sizeof(nested) - 1U, nullptr), "create nested fixture")) return 1;

    kurofs_vfs::Adapter adapter{};
    vfs::FileSystem kfs_backend{};
    if (!expect(kurofs_vfs::initialize(&adapter, &kfs, &kfs_backend) == vfs::Status::Ok && kfs_backend.read_only,
                "initialize read-only KuroFS VFS adapter")) return 1;

    vfs::Operations host_ops{};
    host_ops.stat_path = host_stat;
    vfs::FileSystem host_backend{nullptr, host_ops, true};
    vfs::State state{};
    if (!expect(vfs::initialize(&state, &host_backend) == vfs::Status::Ok, "initialize VFS")) return 1;
    vfs::PathContext context{};
    if (!expect(vfs::initialize_path_context(&state, &context) == vfs::Status::Ok, "initialize path context")) return 1;
    vfs::MountHandle mount{};
    if (!expect(vfs::mount(&state, &context, "/kuro", &kfs_backend, &mount) == vfs::Status::Ok,
                "mount KuroFS through VFS")) return 1;

    vfs::FileStat info{};
    if (!expect(vfs::stat(&state, &context, "/kuro/hello", &info) == vfs::Status::Ok &&
                info.type == vfs::NodeType::Regular && info.size == sizeof(hello) - 1U,
                "stat KuroFS file through VFS")) return 1;
    vfs::OpenFileHandle file{};
    if (!expect(vfs::open(&state, &context, "/kuro/hello", vfs::OpenFlags::Read, &file) == vfs::Status::Ok,
                "open KuroFS file through VFS")) return 1;
    uint8_t buffer[64]{};
    size_t bytes_read = 0U;
    if (!expect(vfs::read(&state, file, buffer, sizeof(buffer), &bytes_read) == vfs::Status::Ok &&
                bytes_read == sizeof(hello) - 1U && std::memcmp(buffer, hello, bytes_read) == 0,
                "read KuroFS file through VFS")) return 1;
    vfs::OpenFileHandle write_attempt{};
    if (!expect(vfs::open(&state, &context, "/kuro/hello", vfs::OpenFlags::Write, &write_attempt) == vfs::Status::ReadOnly,
                "VFS enforces read-only KuroFS mount")) return 1;

    // Metadata revision can advance while an open handle keeps the same inode
    // incarnation. stat_open/seek must refresh the revision rather than fail.
    kurofs::Inode hello_current{};
    if (!expect(kurofs::read_inode(&kfs, hello_inode, &hello_current) == kurofs::Status::Ok, "read hello inode")) return 1;
    const uint32_t hello_generation = hello_current.generation;
    hello_current.size = 5U;
    if (!expect(kurofs::update_inode(&kfs, &hello_current) == kurofs::Status::Ok &&
                hello_current.generation == hello_generation, "advance file revision")) return 1;
    uint64_t end = 0U;
    if (!expect(vfs::seek(&state, file, 0, vfs::SeekOrigin::End, &end) == vfs::Status::Ok && end == 5U,
                "open VFS handle refreshes metadata revision")) return 1;
    if (!expect(vfs::close(&state, file) == vfs::Status::Ok, "close file")) return 1;
    if (!expect(vfs::read(&state, file, buffer, sizeof(buffer), &bytes_read) == vfs::Status::StaleHandle,
                "closed VFS handle is stale")) return 1;

    vfs::OpenFileHandle directory{};
    if (!expect(vfs::open(&state, &context, "/kuro", vfs::OpenFlags::Read | vfs::OpenFlags::Directory, &directory) == vfs::Status::Ok,
                "open KuroFS directory")) return 1;
    vfs::DirectoryEntry first{};
    vfs::DirectoryEntry second{};
    if (!expect(vfs::readdir(&state, directory, &first) == vfs::Status::Ok &&
                std::strcmp(first.name, "hello") == 0 && first.info.size == 5U,
                "readdir first entry with refreshed stat")) return 1;
    if (!expect(vfs::readdir(&state, directory, &second) == vfs::Status::Ok &&
                std::strcmp(second.name, "sub") == 0 && second.info.type == vfs::NodeType::Directory,
                "readdir second entry")) return 1;
    vfs::DirectoryEntry eof{};
    if (!expect(vfs::readdir(&state, directory, &eof) == vfs::Status::EndOfDirectory,
                "readdir end")) return 1;
    if (!expect(vfs::unmount(&state, mount) == vfs::Status::Busy,
                "open directory blocks unmount")) return 1;
    if (!expect(vfs::close(&state, directory) == vfs::Status::Ok, "close directory")) return 1;

    if (!expect(vfs::chdir(&state, &context, "/kuro/sub") == vfs::Status::Ok, "chdir into KuroFS")) return 1;
    vfs::OpenFileHandle nested_file{};
    if (!expect(vfs::open(&state, &context, "nested", vfs::OpenFlags::Read, &nested_file) == vfs::Status::Ok,
                "relative open inside KuroFS")) return 1;
    std::memset(buffer, 0, sizeof(buffer));
    bytes_read = 0U;
    if (!expect(vfs::read(&state, nested_file, buffer, sizeof(buffer), &bytes_read) == vfs::Status::Ok &&
                bytes_read == sizeof(nested) - 1U && std::memcmp(buffer, nested, bytes_read) == 0,
                "nested path read through VFS")) return 1;
    if (!expect(vfs::close(&state, nested_file) == vfs::Status::Ok, "close nested")) return 1;
    if (!expect(vfs::chdir(&state, &context, "/") == vfs::Status::Ok, "leave KuroFS cwd")) return 1;
    if (!expect(vfs::unmount(&state, mount) == vfs::Status::Ok, "unmount KuroFS")) return 1;
    if (!expect(vfs::stat(&state, &context, "/kuro/hello", &info) == vfs::Status::NotFound,
                "path no longer routed after unmount")) return 1;

    std::puts("KuroFS read-only VFS integration tests passed");
    return 0;
}
''')

    replace_once(
        "scripts/run-host-tests.sh",
        '"$OUT_DIR/test_kurofs_directory"\n\n"$HOST_CXX" \\\n',
        '"$OUT_DIR/test_kurofs_directory"\n\n'
        '# Exercise KuroFS through the production VFS routing/open/read/readdir API.\n'
        '"$HOST_CXX" \\\n'
        '  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \\\n'
        '  tests/test_kurofs_vfs.cpp \\\n'
        '  kernel/fs/kurofs_vfs.cpp \\\n'
        '  kernel/fs/kurofs.cpp \\\n'
        '  kernel/fs/vfs.cpp \\\n'
        '  -o "$OUT_DIR/test_kurofs_vfs"\n\n'
        '"$OUT_DIR/test_kurofs_vfs"\n\n'
        '"$HOST_CXX" \\\n',
    )


if __name__ == "__main__":
    main()
