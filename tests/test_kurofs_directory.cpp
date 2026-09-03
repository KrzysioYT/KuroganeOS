#include "../kernel/fs/kurofs.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace system_metrics { void record_disk_blocks(uint64_t) {} }
namespace {
constexpr uint32_t S = 512U;
constexpr uint64_t N = 256U;
uint8_t bytes[S * N]{};
struct Memory {};
storage::block::Status rd(void*, uint64_t first, uint64_t count, void* out) {
    if (out == nullptr || first >= N || count > N - first) return storage::block::Status::OutOfRange;
    std::memcpy(out, bytes + first * S, static_cast<size_t>(count) * S);
    return storage::block::Status::Ok;
}
storage::block::Status wr(void*, uint64_t first, uint64_t count, const void* in) {
    if (in == nullptr || first >= N || count > N - first) return storage::block::Status::OutOfRange;
    std::memcpy(bytes + first * S, in, static_cast<size_t>(count) * S);
    return storage::block::Status::Ok;
}
storage::block::Status fl(void*) { return storage::block::Status::Ok; }
bool ok(bool value, const char* message) {
    if (!value) std::fprintf(stderr, "FAIL: %s\n", message);
    return value;
}
}

int main() {
    using namespace fs::kurofs;
    std::memset(bytes, 0x5A, sizeof(bytes));
    Memory memory{};
    storage::block::Device device{&memory, S, N, rd, wr, fl};
    if (!ok(format(&device, 32U) == Status::Ok, "format")) return 1;
    FileSystem fs{};
    if (!ok(mount(&fs, &device) == Status::Ok, "mount")) return 1;
    Inode root{};
    if (!ok(read_inode(&fs, ROOT_INODE, &root) == Status::Ok, "read root")) return 1;

    uint64_t children[7]{};
    for (size_t index = 0U; index < 6U; ++index) {
        const InodeType type = (index % 2U) == 0U ? InodeType::Regular : InodeType::Directory;
        if (!ok(allocate_inode(&fs, type, &children[index]) == Status::Ok, "allocate child")) return 1;
        char name[16]{};
        std::snprintf(name, sizeof(name), "entry%zu", index);
        if (!ok(directory_append(&fs, &root, name, children[index]) == Status::Ok, "append child")) return 1;
    }
    if (!ok(root.size == 6U * DIRECTORY_ENTRY_SIZE, "directory size")) return 1;
    if (!ok(root.extent_blocks >= 2U, "copy-on-grow expanded directory")) return 1;

    DirectoryEntry found{};
    if (!ok(directory_lookup(&fs, &root, "entry4", &found) == Status::Ok &&
            found.inode_id == children[4], "lookup child")) return 1;

    // Directory identity is bound to stable inode generation, not mutable
    // metadata revision. Updating the child must not invalidate its name.
    Inode changed_child{};
    if (!ok(read_inode(&fs, children[4], &changed_child) == Status::Ok,
            "read child before metadata update")) return 1;
    const uint32_t child_generation = changed_child.generation;
    const uint32_t child_revision = changed_child.revision;
    uint64_t child_extent = 0U;
    if (!ok(allocate_blocks(&fs, 1U, &child_extent) == Status::Ok,
            "allocate child extent")) return 1;
    if (!ok(child_extent == fs.geometry.data_start,
            "directory relocation reclaims its original extent")) return 1;
    changed_child.extent_start = child_extent;
    changed_child.extent_blocks = 1U;
    changed_child.size = 0U;
    if (!ok(update_inode(&fs, &changed_child) == Status::Ok &&
            changed_child.generation == child_generation &&
            changed_child.revision == child_revision + 1U,
            "child revision update preserves incarnation")) return 1;
    DirectoryEntry after_child_update{};
    if (!ok(directory_lookup(&fs, &root, "entry4", &after_child_update) == Status::Ok &&
            after_child_update.inode_id == children[4] &&
            after_child_update.inode_generation == child_generation,
            "child metadata update preserves directory identity")) return 1;
    if (!ok(directory_append(&fs, &root, "entry4", children[5]) == Status::AlreadyExists,
            "reject duplicate name")) return 1;
    if (!ok(directory_append(&fs, &root, "alias", children[4]) == Status::AlreadyExists,
            "reject child alias without link accounting")) return 1;
    char too_long[MAX_DIRECTORY_NAME + 2U]{};
    for (size_t i = 0U; i < MAX_DIRECTORY_NAME + 1U; ++i) too_long[i] = 'x';
    if (!ok(directory_lookup(&fs, &root, too_long, &found) == Status::NameTooLong,
            "reject long name")) return 1;
    if (!ok(directory_lookup(&fs, &root, "bad/name", &found) == Status::InvalidArgument,
            "reject slash in component")) return 1;

    Inode stale_root = root;
    if (!ok(allocate_inode(&fs, InodeType::Regular, &children[6]) == Status::Ok, "allocate seventh child")) return 1;
    if (!ok(directory_append(&fs, &root, "entry6", children[6]) == Status::Ok, "append seventh")) return 1;
    uint64_t unused = 0U;
    if (!ok(allocate_inode(&fs, InodeType::Regular, &unused) == Status::Ok, "allocate stale test child")) return 1;
    if (!ok(directory_append(&fs, &stale_root, "stale", unused) == Status::StaleInode,
            "reject stale directory writer")) return 1;

    FileSystem remounted{};
    if (!ok(mount(&remounted, &device) == Status::Ok, "remount")) return 1;
    Inode root2{};
    if (!ok(read_inode(&remounted, ROOT_INODE, &root2) == Status::Ok, "read remounted root")) return 1;
    for (size_t index = 0U; index < 7U; ++index) {
        char name[16]{};
        std::snprintf(name, sizeof(name), "entry%zu", index);
        DirectoryEntry entry{};
        if (!ok(directory_lookup(&remounted, &root2, name, &entry) == Status::Ok &&
                entry.inode_id == children[index], "remount lookup")) return 1;
        DirectoryEntry ordinal{};
        if (!ok(directory_entry_at(&remounted, &root2, index, &ordinal) == Status::Ok &&
                ordinal.inode_id == children[index], "ordinal readdir record")) return 1;
    }

    Inode stale_remove_root = root2;
    Inode removed_child{};
    if (!ok(read_inode(&remounted, children[4], &removed_child) == Status::Ok,
            "read child before unlink")) return 1;
    const uint32_t removed_generation = removed_child.generation;
    if (!ok(directory_remove(&remounted, &root2, "entry4") == Status::Ok &&
            root2.size == 6U * DIRECTORY_ENTRY_SIZE,
            "copy-on-write unlink")) return 1;
    if (!ok(directory_lookup(&remounted, &root2, "entry4", &found) == Status::NotFound,
            "removed name no longer resolves")) return 1;
    DirectoryEntry shifted{};
    if (!ok(directory_entry_at(&remounted, &root2, 4U, &shifted) == Status::Ok &&
            std::strcmp(shifted.name, "entry5") == 0,
            "unlink compacts following records")) return 1;
    Inode retired{};
    if (!ok(read_inode(&remounted, children[4], &retired) == Status::NotFound,
            "unlinked inode is retired")) return 1;
    if (!ok(directory_remove(&remounted, &stale_remove_root, "entry0") == Status::StaleInode,
            "stale directory snapshot cannot unlink")) return 1;
    if (!ok(directory_remove(&remounted, &root2, "missing") == Status::NotFound,
            "missing unlink is non-mutating")) return 1;

    uint64_t reused_inode_id = 0U;
    if (!ok(allocate_inode(&remounted, InodeType::Regular, &reused_inode_id) == Status::Ok &&
            reused_inode_id == children[4],
            "allocator reuses retired inode slot")) return 1;
    Inode reused_inode{};
    if (!ok(read_inode(&remounted, reused_inode_id, &reused_inode) == Status::Ok &&
            reused_inode.generation == removed_generation + 1U,
            "reused inode advances incarnation generation")) return 1;
    uint64_t reclaimed_unlink_extents = 0U;
    if (!ok(allocate_blocks(&remounted, 3U, &reclaimed_unlink_extents) == Status::Ok &&
            reclaimed_unlink_extents == remounted.geometry.data_start,
            "unlink reclaims child and superseded directory extents")) return 1;

    Inode nonempty{};
    if (!ok(read_inode(&remounted, children[1], &nonempty) == Status::Ok,
            "read directory child")) return 1;
    uint64_t grandchild = 0U;
    if (!ok(allocate_inode(&remounted, InodeType::Regular, &grandchild) == Status::Ok &&
            directory_append(&remounted, &nonempty, "nested", grandchild) == Status::Ok,
            "create non-empty directory fixture")) return 1;
    const uint32_t root_revision_before_refusal = root2.revision;
    if (!ok(directory_remove(&remounted, &root2, "entry1") ==
                Status::DirectoryNotEmpty &&
            root2.revision == root_revision_before_refusal,
            "refuse unlink of non-empty directory")) return 1;

    DirectoryEntry before_rename{};
    if (!ok(directory_lookup(&remounted, &root2, "entry5", &before_rename) == Status::Ok,
            "lookup rename source")) return 1;
    Inode stale_rename_root = root2;
    const uint32_t before_rename_revision = root2.revision;
    if (!ok(directory_rename(&remounted, &root2, "entry5", "renamed") == Status::Ok &&
            root2.revision == before_rename_revision + 1U,
            "copy-on-write same-directory rename")) return 1;
    DirectoryEntry renamed{};
    if (!ok(directory_lookup(&remounted, &root2, "entry5", &found) == Status::NotFound &&
            directory_lookup(&remounted, &root2, "renamed", &renamed) == Status::Ok &&
            renamed.inode_id == before_rename.inode_id &&
            renamed.inode_generation == before_rename.inode_generation &&
            renamed.type == before_rename.type,
            "rename preserves child identity")) return 1;
    DirectoryEntry renamed_ordinal{};
    if (!ok(directory_entry_at(&remounted, &root2, 4U, &renamed_ordinal) == Status::Ok &&
            std::strcmp(renamed_ordinal.name, "renamed") == 0,
            "rename preserves directory order")) return 1;
    const uint32_t before_refused_rename = root2.revision;
    if (!ok(directory_rename(&remounted, &root2, "renamed", "entry6") ==
                Status::AlreadyExists &&
            root2.revision == before_refused_rename,
            "rename refuses destination collision")) return 1;
    if (!ok(directory_rename(&remounted, &root2, "renamed", "renamed") == Status::Ok &&
            root2.revision == before_refused_rename,
            "same-name rename is a non-mutating success")) return 1;
    if (!ok(directory_rename(&remounted, &stale_rename_root, "entry5", "stale") ==
                Status::StaleInode,
            "stale directory snapshot cannot rename")) return 1;

    FileSystem removed_mount{};
    Inode removed_root{};
    if (!ok(mount(&removed_mount, &device) == Status::Ok &&
            read_inode(&removed_mount, ROOT_INODE, &removed_root) == Status::Ok &&
            directory_lookup(&removed_mount, &removed_root, "entry4", &found) ==
                Status::NotFound &&
            directory_lookup(&removed_mount, &removed_root, "entry5", &found) ==
                Status::NotFound &&
            directory_lookup(&removed_mount, &removed_root, "renamed", &renamed) ==
                Status::Ok,
            "unlink and rename survive remount")) return 1;

    const uint64_t corrupt_offset = root2.extent_start * S + 20U;
    bytes[corrupt_offset] ^= 0x01U;
    DirectoryEntry corrupt{};
    if (!ok(directory_entry_at(&remounted, &root2, 0U, &corrupt) == Status::CorruptDirectory,
            "CRC detects directory corruption")) return 1;

    std::puts("KuroFS directory persistence tests passed");
    return 0;
}
