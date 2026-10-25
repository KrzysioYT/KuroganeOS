#!/usr/bin/env python3
"""Apply CRC-protected append-only KuroFS directory records for 4.0."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded match, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> None:
    replace_once(
        "kernel/fs/kurofs.hpp",
        "constexpr uint64_t FEATURE_NONE = 0U;\n",
        "constexpr uint64_t FEATURE_NONE = 0U;\n"
        "constexpr uint32_t DIRECTORY_ENTRY_SIZE = 128U;\n"
        "constexpr size_t MAX_DIRECTORY_NAME = 63U;\n",
    )
    replace_once(
        "kernel/fs/kurofs.hpp",
        "    InvalidExtent,\n    StaleInode,\n    NoSpace,\n",
        "    InvalidExtent,\n    StaleInode,\n    NotFound,\n    AlreadyExists,\n"
        "    NotDirectory,\n    NameTooLong,\n    CorruptDirectory,\n    NoSpace,\n",
    )
    replace_once(
        "kernel/fs/kurofs.hpp",
        "struct FileSystem {\n"
        "    const storage::block::Device* device;\n"
        "    Geometry geometry;\n"
        "    bool mounted;\n"
        "};\n",
        "struct FileSystem {\n"
        "    const storage::block::Device* device;\n"
        "    Geometry geometry;\n"
        "    bool mounted;\n"
        "};\n\n"
        "struct DirectoryEntry {\n"
        "    char name[MAX_DIRECTORY_NAME + 1U];\n"
        "    size_t name_length;\n"
        "    uint64_t inode_id;\n"
        "    uint32_t inode_generation;\n"
        "    InodeType type;\n"
        "};\n",
    )
    replace_once(
        "kernel/fs/kurofs.hpp",
        "Status read_inode_data(\n"
        "    FileSystem* filesystem,\n"
        "    const Inode* inode,\n"
        "    uint64_t offset,\n"
        "    void* destination,\n"
        "    size_t capacity,\n"
        "    size_t* out_read);\n\n"
        "const char* status_message(Status status);\n",
        "Status read_inode_data(\n"
        "    FileSystem* filesystem,\n"
        "    const Inode* inode,\n"
        "    uint64_t offset,\n"
        "    void* destination,\n"
        "    size_t capacity,\n"
        "    size_t* out_read);\n\n"
        "// Directory records are fixed-size, CRC-protected and append-only until\n"
        "// transactional unlink/reclamation is introduced. Child inode generation\n"
        "// is bound into each record so stale aliases become corruption, not access.\n"
        "Status directory_entry_at(\n"
        "    FileSystem* filesystem, const Inode* directory,\n"
        "    uint64_t index, DirectoryEntry* output);\n"
        "Status directory_lookup(\n"
        "    FileSystem* filesystem, const Inode* directory,\n"
        "    const char* name, DirectoryEntry* output);\n"
        "Status directory_append(\n"
        "    FileSystem* filesystem, Inode* directory,\n"
        "    const char* name, uint64_t child_inode_id);\n\n"
        "const char* status_message(Status status);\n",
    )

    anchor = "void copy_bytes(uint8_t* destination, const uint8_t* source, size_t size) {\n"
    anchor += "    for (size_t index = 0U; index < size; ++index) destination[index] = source[index];\n"
    anchor += "}\n\n} // namespace\n"
    helpers = r'''void copy_bytes(uint8_t* destination, const uint8_t* source, size_t size) {
    for (size_t index = 0U; index < size; ++index) destination[index] = source[index];
}

Status parse_directory_name(const char* name, size_t* out_length) {
    if (name == nullptr || out_length == nullptr || name[0] == '\0') {
        return Status::InvalidArgument;
    }
    size_t length = 0U;
    for (; length <= MAX_DIRECTORY_NAME; ++length) {
        const char character = name[length];
        if (character == '\0') break;
        if (character == '/') return Status::InvalidArgument;
    }
    if (length > MAX_DIRECTORY_NAME) return Status::NameTooLong;
    if ((length == 1U && name[0] == '.') ||
        (length == 2U && name[0] == '.' && name[1] == '.')) {
        return Status::InvalidArgument;
    }
    *out_length = length;
    return Status::Ok;
}

bool same_name(const DirectoryEntry& entry, const char* name, size_t length) {
    if (entry.name_length != length) return false;
    for (size_t index = 0U; index < length; ++index) {
        if (entry.name[index] != name[index]) return false;
    }
    return true;
}

bool same_inode_snapshot(const Inode& left, const Inode& right) {
    return left.id == right.id && left.type == right.type && left.flags == right.flags &&
        left.size == right.size && left.extent_start == right.extent_start &&
        left.extent_blocks == right.extent_blocks && left.link_count == right.link_count &&
        left.generation == right.generation;
}

Status require_current_snapshot(
    FileSystem* filesystem, const Inode* snapshot, Inode* current) {
    if (filesystem == nullptr || snapshot == nullptr || current == nullptr) {
        return Status::InvalidArgument;
    }
    const Status status = read_inode(filesystem, snapshot->id, current);
    if (status != Status::Ok) return status;
    if (!same_inode_snapshot(*snapshot, *current)) return Status::StaleInode;
    return Status::Ok;
}

void encode_directory_entry(
    const char* name, size_t name_length, const Inode& child, uint8_t* record) {
    clear_bytes(record, DIRECTORY_ENTRY_SIZE);
    store_u64(record + 0U, child.id);
    store_u32(record + 8U, child.generation);
    store_u32(record + 12U, static_cast<uint32_t>(child.type));
    store_u32(record + 16U, static_cast<uint32_t>(name_length));
    for (size_t index = 0U; index < name_length; ++index) record[20U + index] = static_cast<uint8_t>(name[index]);
    store_u32(record + DIRECTORY_ENTRY_SIZE - sizeof(uint32_t),
              crc32(record, DIRECTORY_ENTRY_SIZE - sizeof(uint32_t)));
}

Status decode_directory_entry(const uint8_t* record, DirectoryEntry* output) {
    if (record == nullptr || output == nullptr) return Status::InvalidArgument;
    const uint32_t checksum = load_u32(record + DIRECTORY_ENTRY_SIZE - sizeof(uint32_t));
    if (checksum != crc32(record, DIRECTORY_ENTRY_SIZE - sizeof(uint32_t))) {
        return Status::CorruptDirectory;
    }
    DirectoryEntry entry{};
    entry.inode_id = load_u64(record + 0U);
    entry.inode_generation = load_u32(record + 8U);
    entry.type = static_cast<InodeType>(load_u32(record + 12U));
    const uint32_t encoded_length = load_u32(record + 16U);
    if (entry.inode_id == 0U || entry.inode_generation == 0U ||
        (entry.type != InodeType::Regular && entry.type != InodeType::Directory) ||
        encoded_length == 0U || encoded_length > MAX_DIRECTORY_NAME) {
        return Status::CorruptDirectory;
    }
    entry.name_length = encoded_length;
    for (size_t index = 0U; index < entry.name_length; ++index) {
        const char character = static_cast<char>(record[20U + index]);
        if (character == '\0' || character == '/') return Status::CorruptDirectory;
        entry.name[index] = character;
    }
    entry.name[entry.name_length] = '\0';
    *output = entry;
    return Status::Ok;
}

Status validate_directory_child(
    FileSystem* filesystem, const DirectoryEntry& entry) {
    Inode child{};
    const Status status = read_inode(filesystem, entry.inode_id, &child);
    if (status != Status::Ok || child.generation != entry.inode_generation ||
        child.type != entry.type) {
        return Status::CorruptDirectory;
    }
    return Status::Ok;
}

} // namespace
'''
    replace_once("kernel/fs/kurofs.cpp", anchor, helpers)

    public_anchor = "const char* status_message(Status status) {\n"
    public_impl = r'''Status directory_entry_at(
    FileSystem* filesystem,
    const Inode* directory,
    uint64_t index,
    DirectoryEntry* output) {
    if (filesystem == nullptr || directory == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;
    Inode current{};
    Status status = require_current_snapshot(filesystem, directory, &current);
    if (status != Status::Ok) return status;
    if (current.type != InodeType::Directory) return Status::NotDirectory;
    if ((current.size % DIRECTORY_ENTRY_SIZE) != 0U) return Status::CorruptDirectory;
    const uint64_t entry_count = current.size / DIRECTORY_ENTRY_SIZE;
    if (index >= entry_count) return Status::NotFound;
    uint64_t offset = 0U;
    if (!multiply_u64(index, DIRECTORY_ENTRY_SIZE, &offset)) {
        return Status::ArithmeticOverflow;
    }
    uint8_t record[DIRECTORY_ENTRY_SIZE]{};
    size_t read = 0U;
    status = read_inode_data(
        filesystem, &current, offset, record, sizeof(record), &read);
    if (status != Status::Ok) return status;
    if (read != sizeof(record)) return Status::CorruptDirectory;
    status = decode_directory_entry(record, output);
    if (status != Status::Ok) return status;
    return validate_directory_child(filesystem, *output);
}

Status directory_lookup(
    FileSystem* filesystem,
    const Inode* directory,
    const char* name,
    DirectoryEntry* output) {
    if (filesystem == nullptr || directory == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    size_t name_length = 0U;
    Status status = parse_directory_name(name, &name_length);
    if (status != Status::Ok) return status;
    Inode current{};
    status = require_current_snapshot(filesystem, directory, &current);
    if (status != Status::Ok) return status;
    if (current.type != InodeType::Directory) return Status::NotDirectory;
    if ((current.size % DIRECTORY_ENTRY_SIZE) != 0U) return Status::CorruptDirectory;
    const uint64_t entry_count = current.size / DIRECTORY_ENTRY_SIZE;
    for (uint64_t index = 0U; index < entry_count; ++index) {
        DirectoryEntry entry{};
        status = directory_entry_at(filesystem, &current, index, &entry);
        if (status != Status::Ok) return status;
        if (same_name(entry, name, name_length)) {
            *output = entry;
            return Status::Ok;
        }
    }
    return Status::NotFound;
}

Status directory_append(
    FileSystem* filesystem,
    Inode* directory,
    const char* name,
    uint64_t child_inode_id) {
    if (filesystem == nullptr || directory == nullptr || child_inode_id == 0U) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;
    size_t name_length = 0U;
    Status status = parse_directory_name(name, &name_length);
    if (status != Status::Ok) return status;
    Inode current{};
    status = require_current_snapshot(filesystem, directory, &current);
    if (status != Status::Ok) return status;
    if (current.type != InodeType::Directory) return Status::NotDirectory;
    if ((current.size % DIRECTORY_ENTRY_SIZE) != 0U) return Status::CorruptDirectory;
    if (child_inode_id == current.id) return Status::InvalidArgument;

    const uint64_t entry_count = current.size / DIRECTORY_ENTRY_SIZE;
    for (uint64_t index = 0U; index < entry_count; ++index) {
        DirectoryEntry existing{};
        status = directory_entry_at(filesystem, &current, index, &existing);
        if (status != Status::Ok) return status;
        if (same_name(existing, name, name_length) || existing.inode_id == child_inode_id) {
            return Status::AlreadyExists;
        }
    }

    Inode child{};
    status = read_inode(filesystem, child_inode_id, &child);
    if (status != Status::Ok) return status;
    uint8_t record[DIRECTORY_ENTRY_SIZE]{};
    encode_directory_entry(name, name_length, child, record);

    uint64_t required_size = 0U;
    if (!add_u64(current.size, DIRECTORY_ENTRY_SIZE, &required_size)) {
        return Status::ArithmeticOverflow;
    }
    uint64_t old_capacity = 0U;
    if (!multiply_u64(current.extent_blocks, kSectorSize, &old_capacity)) {
        return Status::ArithmeticOverflow;
    }

    Inode candidate = current;
    if (required_size > old_capacity) {
        uint64_t needed_blocks = divide_round_up(required_size, kSectorSize);
        uint64_t target_blocks = current.extent_blocks == 0U ? 1U : current.extent_blocks;
        if (target_blocks < needed_blocks) {
            if (target_blocks <= UINT64_MAX / 2U) target_blocks *= 2U;
            if (target_blocks < needed_blocks) target_blocks = needed_blocks;
        }
        uint64_t new_extent = 0U;
        status = allocate_blocks(filesystem, target_blocks, &new_extent);
        if (status != Status::Ok) return status;

        uint64_t copied = 0U;
        while (copied < current.size) {
            uint8_t chunk[DIRECTORY_ENTRY_SIZE]{};
            size_t read = 0U;
            status = read_inode_data(
                filesystem, &current, copied, chunk, sizeof(chunk), &read);
            if (status != Status::Ok) return status;
            if (read == 0U) return Status::CorruptDirectory;
            status = write_extent_data(
                filesystem, new_extent, target_blocks, copied, chunk, read);
            if (status != Status::Ok) return status;
            copied += static_cast<uint64_t>(read);
        }
        status = write_extent_data(
            filesystem, new_extent, target_blocks, current.size, record, sizeof(record));
        if (status != Status::Ok) return status;
        candidate.extent_start = new_extent;
        candidate.extent_blocks = target_blocks;
    } else {
        status = write_extent_data(
            filesystem, current.extent_start, current.extent_blocks,
            current.size, record, sizeof(record));
        if (status != Status::Ok) return status;
    }
    candidate.size = required_size;
    status = update_inode(filesystem, &candidate);
    if (status != Status::Ok) return status;
    *directory = candidate;
    return Status::Ok;
}

const char* status_message(Status status) {
'''
    replace_once("kernel/fs/kurofs.cpp", public_anchor, public_impl)
    replace_once(
        "kernel/fs/kurofs.cpp",
        "        case Status::StaleInode: return \"stale KuroFS inode generation\";\n"
        "        case Status::NoSpace: return \"no free KuroFS space\";\n",
        "        case Status::StaleInode: return \"stale KuroFS inode generation\";\n"
        "        case Status::NotFound: return \"KuroFS entry not found\";\n"
        "        case Status::AlreadyExists: return \"KuroFS entry already exists\";\n"
        "        case Status::NotDirectory: return \"KuroFS inode is not a directory\";\n"
        "        case Status::NameTooLong: return \"KuroFS name too long\";\n"
        "        case Status::CorruptDirectory: return \"corrupt KuroFS directory\";\n"
        "        case Status::NoSpace: return \"no free KuroFS space\";\n",
    )

    test = ROOT / "tests/test_kurofs_directory.cpp"
    if test.exists():
        raise SystemExit("tests/test_kurofs_directory.cpp already exists")
    test.write_text(r'''#include "../kernel/fs/kurofs.hpp"

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

    const uint64_t corrupt_offset = root2.extent_start * S + 20U;
    bytes[corrupt_offset] ^= 0x01U;
    DirectoryEntry corrupt{};
    if (!ok(directory_entry_at(&remounted, &root2, 0U, &corrupt) == Status::CorruptDirectory,
            "CRC detects directory corruption")) return 1;

    std::puts("KuroFS directory persistence tests passed");
    return 0;
}
''', encoding="utf-8")

    replace_once(
        "scripts/run-host-tests.sh",
        '"$OUT_DIR/test_kurofs_data"\n\n"$HOST_CXX" \\\n',
        '"$OUT_DIR/test_kurofs_data"\n\n'
        '# Exercise CRC-protected KuroFS directory persistence and copy-on-grow.\n'
        '"$HOST_CXX" \\\n'
        '  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \\\n'
        '  tests/test_kurofs_directory.cpp \\\n'
        '  kernel/fs/kurofs.cpp \\\n'
        '  -o "$OUT_DIR/test_kurofs_directory"\n\n'
        '"$OUT_DIR/test_kurofs_directory"\n\n'
        '"$HOST_CXX" \\\n',
    )


if __name__ == "__main__":
    main()
