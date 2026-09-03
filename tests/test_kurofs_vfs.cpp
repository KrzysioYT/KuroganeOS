#include "../kernel/fs/kurofs_vfs.hpp"

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
    if (!expect(kurofs_vfs::initialize(&adapter, &kfs, &kfs_backend) == vfs::Status::Ok && !kfs_backend.read_only,
                "initialize writable KuroFS VFS adapter")) return 1;

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

    if (!expect(vfs::create(&state, &context, "/kuro/new") == vfs::Status::Ok,
                "create regular file through VFS")) return 1;
    if (!expect(vfs::create(&state, &context, "/kuro/new") == vfs::Status::AlreadyExists,
                "reject duplicate VFS create")) return 1;
    vfs::OpenFileHandle writable{};
    if (!expect(vfs::open(
                    &state, &context, "/kuro/new",
                    vfs::OpenFlags::Read | vfs::OpenFlags::Write,
                    &writable) == vfs::Status::Ok,
                "open created file for read and write")) return 1;
    uint64_t position = 0U;
    if (!expect(vfs::seek(
                    &state, writable, 4, vfs::SeekOrigin::Begin,
                    &position) == vfs::Status::Ok && position == 4U,
                "seek beyond empty file")) return 1;
    static const uint8_t steel[] = {'s', 't', 'e', 'e', 'l'};
    size_t bytes_written = 0U;
    if (!expect(vfs::write(
                    &state, writable, steel, sizeof(steel),
                    &bytes_written) == vfs::Status::Ok &&
                bytes_written == sizeof(steel),
                "sparse write through VFS")) return 1;
    if (!expect(vfs::seek(
                    &state, writable, 0, vfs::SeekOrigin::Begin,
                    &position) == vfs::Status::Ok && position == 0U,
                "rewind written file")) return 1;
    uint8_t sparse[10]{};
    bytes_read = 0U;
    if (!expect(vfs::read(
                    &state, writable, sparse, sizeof(sparse),
                    &bytes_read) == vfs::Status::Ok && bytes_read == 9U &&
                sparse[0] == 0U && sparse[1] == 0U &&
                sparse[2] == 0U && sparse[3] == 0U &&
                std::memcmp(sparse + 4U, steel, sizeof(steel)) == 0,
                "read zero-filled sparse write through VFS")) return 1;
    if (!expect(vfs::close(&state, writable) == vfs::Status::Ok,
                "close written file")) return 1;

    vfs::OpenFileHandle append{};
    if (!expect(vfs::open(
                    &state, &context, "/kuro/new",
                    vfs::OpenFlags::Write | vfs::OpenFlags::Append,
                    &append) == vfs::Status::Ok,
                "open created file for append")) return 1;
    static const uint8_t suffix = '!';
    bytes_written = 0U;
    if (!expect(vfs::write(
                    &state, append, &suffix, sizeof(suffix),
                    &bytes_written) == vfs::Status::Ok &&
                bytes_written == sizeof(suffix),
                "append through VFS")) return 1;
    if (!expect(vfs::close(&state, append) == vfs::Status::Ok,
                "close append handle")) return 1;

    if (!expect(vfs::mkdir(&state, &context, "/kuro/live") == vfs::Status::Ok,
                "create directory through VFS")) return 1;
    if (!expect(vfs::mkdir(&state, &context, "/kuro/live") == vfs::Status::AlreadyExists,
                "reject duplicate VFS directory create")) return 1;
    if (!expect(vfs::create(&state, &context, "/kuro/live/item") == vfs::Status::Ok,
                "create nested file through VFS")) return 1;
    if (!expect(vfs::rename(
                    &state, &context, "/kuro/new", "/kuro/live/new") ==
                    vfs::Status::Unsupported,
                "cross-directory KuroFS rename remains explicit")) return 1;
    if (!expect(vfs::rename(
                    &state, &context, "/kuro/new", "/kuro/forged") ==
                    vfs::Status::Ok,
                "same-directory rename through VFS")) return 1;
    if (!expect(vfs::stat(&state, &context, "/kuro/new", &info) ==
                    vfs::Status::NotFound &&
                vfs::stat(&state, &context, "/kuro/forged", &info) ==
                    vfs::Status::Ok && info.size == sizeof(sparse),
                "renamed file has durable size")) return 1;
    if (!expect(vfs::unlink(&state, &context, "/kuro/live") ==
                    vfs::Status::IsDirectory,
                "unlink rejects a directory")) return 1;
    if (!expect(vfs::rmdir(&state, &context, "/kuro/live/item") ==
                    vfs::Status::NotDirectory,
                "rmdir rejects a regular file")) return 1;
    if (!expect(vfs::rmdir(&state, &context, "/kuro/live") ==
                    vfs::Status::DirectoryNotEmpty,
                "rmdir rejects a non-empty directory")) return 1;
    if (!expect(vfs::stat(&state, &context, "/kuro/live", &info) ==
                    vfs::Status::Ok && info.type == vfs::NodeType::Directory,
                "failed rmdir preserves non-empty directory")) return 1;
    if (!expect(vfs::unlink(&state, &context, "/kuro/live/item") ==
                    vfs::Status::Ok &&
                vfs::rmdir(&state, &context, "/kuro/live") == vfs::Status::Ok,
                "remove nested file and empty directory")) return 1;

    if (!expect(vfs::create(&state, &context, "/kuro/victim") == vfs::Status::Ok,
                "create stale-handle victim")) return 1;
    vfs::OpenFileHandle victim{};
    if (!expect(vfs::open(
                    &state, &context, "/kuro/victim",
                    vfs::OpenFlags::Read, &victim) == vfs::Status::Ok,
                "open stale-handle victim")) return 1;
    if (!expect(vfs::unlink(&state, &context, "/kuro/victim") == vfs::Status::Ok,
                "unlink open file through VFS")) return 1;
    bytes_read = 0U;
    if (!expect(vfs::read(
                    &state, victim, buffer, sizeof(buffer), &bytes_read) ==
                    vfs::Status::StaleHandle,
                "unlinked inode invalidates open backend handle")) return 1;
    if (!expect(vfs::close(&state, victim) == vfs::Status::Ok,
                "close invalidated backend handle")) return 1;

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

    kurofs::FileSystem durable_kfs{};
    if (!expect(kurofs::mount(&durable_kfs, &device) == kurofs::Status::Ok,
                "remount mutated KuroFS")) return 1;
    kurofs_vfs::Adapter durable_adapter{};
    vfs::FileSystem durable_backend{};
    if (!expect(kurofs_vfs::initialize(
                    &durable_adapter, &durable_kfs, &durable_backend) ==
                    vfs::Status::Ok &&
                vfs::mount(
                    &state, &context, "/kuro", &durable_backend, &mount) ==
                    vfs::Status::Ok,
                "remount writable adapter through VFS")) return 1;
    vfs::OpenFileHandle durable{};
    if (!expect(vfs::open(
                    &state, &context, "/kuro/forged",
                    vfs::OpenFlags::Read, &durable) == vfs::Status::Ok,
                "open renamed file after remount")) return 1;
    std::memset(sparse, 0xFF, sizeof(sparse));
    bytes_read = 0U;
    if (!expect(vfs::read(
                    &state, durable, sparse, sizeof(sparse), &bytes_read) ==
                    vfs::Status::Ok && bytes_read == sizeof(sparse) &&
                sparse[0] == 0U && sparse[3] == 0U &&
                std::memcmp(sparse + 4U, steel, sizeof(steel)) == 0 &&
                sparse[9] == suffix,
                "VFS mutation payload survives remount")) return 1;
    if (!expect(vfs::close(&state, durable) == vfs::Status::Ok,
                "close remounted file")) return 1;
    if (!expect(vfs::stat(&state, &context, "/kuro/live", &info) ==
                    vfs::Status::NotFound &&
                vfs::stat(&state, &context, "/kuro/victim", &info) ==
                    vfs::Status::NotFound,
                "removed nodes stay absent after remount")) return 1;
    if (!expect(vfs::unmount(&state, mount) == vfs::Status::Ok,
                "unmount remounted KuroFS")) return 1;

    std::puts("KuroFS writable VFS integration tests passed");
    return 0;
}
