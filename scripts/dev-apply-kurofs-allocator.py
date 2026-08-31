#!/usr/bin/env python3
"""Apply the first KuroFS 1.0 persistent allocator slice for 4.0 Pre-Steel."""

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
        "    InvalidRootInode,\n    BlockDeviceError,\n",
        "    InvalidRootInode,\n    NoSpace,\n    BlockDeviceError,\n",
    )
    replace_once(
        "kernel/fs/kurofs.hpp",
        "Status read_inode(FileSystem* filesystem, uint64_t inode_id, Inode* output);\n"
        "const char* status_message(Status status);\n",
        "Status read_inode(FileSystem* filesystem, uint64_t inode_id, Inode* output);\n\n"
        "// Reserve one persistent contiguous data extent. Metadata blocks are never\n"
        "// considered candidates. Successful bitmap publication is flushed before\n"
        "// returning; an I/O failure may conservatively leak reserved blocks but can\n"
        "// never hand the same block to two successful callers.\n"
        "Status allocate_blocks(\n"
        "    FileSystem* filesystem, uint64_t block_count, uint64_t* out_first_block);\n\n"
        "// Allocate and persist an empty non-root inode. This intentionally does not\n"
        "// attach data blocks yet: later file creation can reserve+flush its extent\n"
        "// first and publish the inode only after data ownership is durable.\n"
        "Status allocate_inode(\n"
        "    FileSystem* filesystem, InodeType type, uint64_t* out_inode_id);\n\n"
        "const char* status_message(Status status);\n",
    )

    replace_once(
        "kernel/fs/kurofs.cpp",
        "Status read_superblock_copy(\n"
        "    const storage::block::Device* device,\n"
        "    uint64_t block,\n"
        "    Geometry* output) {\n"
        "    uint8_t sector[kSectorSize]{};\n"
        "    const Status read_status = read_one(device, block, sector);\n"
        "    if (read_status != Status::Ok) return read_status;\n"
        "    return decode_superblock(sector, device, output);\n"
        "}\n\n"
        "} // namespace\n",
        "Status read_superblock_copy(\n"
        "    const storage::block::Device* device,\n"
        "    uint64_t block,\n"
        "    Geometry* output) {\n"
        "    uint8_t sector[kSectorSize]{};\n"
        "    const Status read_status = read_one(device, block, sector);\n"
        "    if (read_status != Status::Ok) return read_status;\n"
        "    return decode_superblock(sector, device, output);\n"
        "}\n\n"
        "bool all_zero(const uint8_t* bytes, size_t size) {\n"
        "    if (bytes == nullptr) return false;\n"
        "    for (size_t index = 0U; index < size; ++index) {\n"
        "        if (bytes[index] != 0U) return false;\n"
        "    }\n"
        "    return true;\n"
        "}\n\n"
        "Status find_free_extent(\n"
        "    FileSystem* filesystem, uint64_t block_count, uint64_t* out_start) {\n"
        "    uint8_t bitmap[kSectorSize]{};\n"
        "    uint64_t loaded_bitmap = UINT64_MAX;\n"
        "    uint64_t run_start = 0U;\n"
        "    uint64_t run_length = 0U;\n"
        "    for (uint64_t block = filesystem->geometry.data_start;\n"
        "         block < filesystem->geometry.total_blocks; ++block) {\n"
        "        const uint64_t bitmap_index = block / kBitsPerBitmapBlock;\n"
        "        if (bitmap_index >= filesystem->geometry.allocation_bitmap_blocks) {\n"
        "            return Status::InvalidGeometry;\n"
        "        }\n"
        "        if (bitmap_index != loaded_bitmap) {\n"
        "            const Status status = read_one(\n"
        "                filesystem->device,\n"
        "                filesystem->geometry.allocation_bitmap_start + bitmap_index,\n"
        "                bitmap);\n"
        "            if (status != Status::Ok) return status;\n"
        "            loaded_bitmap = bitmap_index;\n"
        "        }\n"
        "        const uint64_t bit = block % kBitsPerBitmapBlock;\n"
        "        const size_t byte_index = static_cast<size_t>(bit / 8U);\n"
        "        const uint8_t mask = static_cast<uint8_t>(\n"
        "            UINT8_C(1) << static_cast<uint8_t>(bit % 8U));\n"
        "        if ((bitmap[byte_index] & mask) == 0U) {\n"
        "            if (run_length == 0U) run_start = block;\n"
        "            ++run_length;\n"
        "            if (run_length == block_count) {\n"
        "                *out_start = run_start;\n"
        "                return Status::Ok;\n"
        "            }\n"
        "        } else {\n"
        "            run_length = 0U;\n"
        "        }\n"
        "    }\n"
        "    return Status::NoSpace;\n"
        "}\n\n"
        "Status publish_extent_allocation(\n"
        "    FileSystem* filesystem, uint64_t first_block, uint64_t block_count) {\n"
        "    uint64_t end_block = 0U;\n"
        "    if (!add_u64(first_block, block_count, &end_block) ||\n"
        "        first_block < filesystem->geometry.data_start ||\n"
        "        end_block > filesystem->geometry.total_blocks) {\n"
        "        return Status::InvalidGeometry;\n"
        "    }\n"
        "    uint64_t current = first_block;\n"
        "    while (current < end_block) {\n"
        "        const uint64_t bitmap_index = current / kBitsPerBitmapBlock;\n"
        "        if (bitmap_index >= filesystem->geometry.allocation_bitmap_blocks) {\n"
        "            return Status::InvalidGeometry;\n"
        "        }\n"
        "        uint8_t bitmap[kSectorSize]{};\n"
        "        const uint64_t bitmap_block =\n"
        "            filesystem->geometry.allocation_bitmap_start + bitmap_index;\n"
        "        Status status = read_one(filesystem->device, bitmap_block, bitmap);\n"
        "        if (status != Status::Ok) return status;\n"
        "        const uint64_t represented_end =\n"
        "            (bitmap_index + 1U) * kBitsPerBitmapBlock;\n"
        "        const uint64_t segment_end =\n"
        "            end_block < represented_end ? end_block : represented_end;\n"
        "        for (uint64_t block = current; block < segment_end; ++block) {\n"
        "            const uint64_t bit = block % kBitsPerBitmapBlock;\n"
        "            const size_t byte_index = static_cast<size_t>(bit / 8U);\n"
        "            const uint8_t mask = static_cast<uint8_t>(\n"
        "                UINT8_C(1) << static_cast<uint8_t>(bit % 8U));\n"
        "            // Re-check before publication. KuroFS currently serializes\n"
        "            // metadata callers; this additionally refuses inconsistent\n"
        "            // on-disk state rather than silently double-allocating.\n"
        "            if ((bitmap[byte_index] & mask) != 0U) return Status::NoSpace;\n"
        "            bitmap[byte_index] = static_cast<uint8_t>(bitmap[byte_index] | mask);\n"
        "        }\n"
        "        status = write_one(filesystem->device, bitmap_block, bitmap);\n"
        "        if (status != Status::Ok) return status;\n"
        "        current = segment_end;\n"
        "    }\n"
        "    return block_status(storage::block::flush(filesystem->device));\n"
        "}\n\n"
        "} // namespace\n",
    )

    replace_once(
        "kernel/fs/kurofs.cpp",
        "const char* status_message(Status status) {\n",
        "Status allocate_blocks(\n"
        "    FileSystem* filesystem, uint64_t block_count, uint64_t* out_first_block) {\n"
        "    if (filesystem == nullptr || out_first_block == nullptr || block_count == 0U) {\n"
        "        return Status::InvalidArgument;\n"
        "    }\n"
        "    if (!is_mounted(filesystem)) return Status::NotMounted;\n"
        "    const uint64_t available = filesystem->geometry.total_blocks -\n"
        "        filesystem->geometry.data_start;\n"
        "    if (block_count > available) return Status::NoSpace;\n\n"
        "    uint64_t first = 0U;\n"
        "    Status status = find_free_extent(filesystem, block_count, &first);\n"
        "    if (status != Status::Ok) return status;\n"
        "    status = publish_extent_allocation(filesystem, first, block_count);\n"
        "    if (status != Status::Ok) return status;\n"
        "    *out_first_block = first;\n"
        "    return Status::Ok;\n"
        "}\n\n"
        "Status allocate_inode(\n"
        "    FileSystem* filesystem, InodeType type, uint64_t* out_inode_id) {\n"
        "    if (filesystem == nullptr || out_inode_id == nullptr ||\n"
        "        (type != InodeType::Regular && type != InodeType::Directory)) {\n"
        "        return Status::InvalidArgument;\n"
        "    }\n"
        "    if (!is_mounted(filesystem)) return Status::NotMounted;\n"
        "    for (uint64_t relative_block = 0U;\n"
        "         relative_block < filesystem->geometry.inode_table_blocks;\n"
        "         ++relative_block) {\n"
        "        uint8_t sector[kSectorSize]{};\n"
        "        const uint64_t table_block =\n"
        "            filesystem->geometry.inode_table_start + relative_block;\n"
        "        Status status = read_one(filesystem->device, table_block, sector);\n"
        "        if (status != Status::Ok) return status;\n"
        "        for (size_t offset = 0U; offset + INODE_SIZE <= kSectorSize;\n"
        "             offset += INODE_SIZE) {\n"
        "            const uint64_t inode_id =\n"
        "                (relative_block * kSectorSize + offset) / INODE_SIZE + 1U;\n"
        "            if (inode_id == ROOT_INODE) continue;\n"
        "            if (inode_id > filesystem->geometry.inode_count) return Status::NoSpace;\n"
        "            if (!all_zero(sector + offset, INODE_SIZE)) continue;\n"
        "            Inode inode{};\n"
        "            inode.id = inode_id;\n"
        "            inode.type = type;\n"
        "            inode.flags = 0U;\n"
        "            inode.size = 0U;\n"
        "            inode.extent_start = 0U;\n"
        "            inode.extent_blocks = 0U;\n"
        "            inode.link_count = 1U;\n"
        "            inode.generation = 1U;\n"
        "            encode_inode(inode, sector + offset);\n"
        "            status = write_one(filesystem->device, table_block, sector);\n"
        "            if (status != Status::Ok) return status;\n"
        "            status = block_status(storage::block::flush(filesystem->device));\n"
        "            if (status != Status::Ok) return status;\n"
        "            *out_inode_id = inode_id;\n"
        "            return Status::Ok;\n"
        "        }\n"
        "    }\n"
        "    return Status::NoSpace;\n"
        "}\n\n"
        "const char* status_message(Status status) {\n",
    )
    replace_once(
        "kernel/fs/kurofs.cpp",
        "        case Status::InvalidRootInode: return \"invalid root inode\";\n"
        "        case Status::BlockDeviceError: return \"block device error\";\n",
        "        case Status::InvalidRootInode: return \"invalid root inode\";\n"
        "        case Status::NoSpace: return \"no free KuroFS space\";\n"
        "        case Status::BlockDeviceError: return \"block device error\";\n",
    )

    test_path = ROOT / "tests/test_kurofs_allocator.cpp"
    if test_path.exists():
        raise SystemExit("tests/test_kurofs_allocator.cpp already exists")
    test_path.write_text(r'''#include "../kernel/fs/kurofs.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace system_metrics {
void record_disk_blocks(uint64_t) {}
}

namespace {
constexpr uint32_t SECTOR_SIZE = 512U;
constexpr uint64_t SECTOR_COUNT = 128U;
uint8_t g_storage[SECTOR_SIZE * SECTOR_COUNT]{};

struct MemoryDevice {
    bool fail_reads;
    bool fail_writes;
    bool fail_flush;
    uint64_t flushes;
};

storage::block::Status read_blocks(
    void* context, uint64_t first, uint64_t count, void* destination) {
    auto* memory = static_cast<MemoryDevice*>(context);
    if (memory == nullptr || destination == nullptr) return storage::block::Status::InvalidArgument;
    if (memory->fail_reads) return storage::block::Status::IoError;
    if (first >= SECTOR_COUNT || count > SECTOR_COUNT - first) return storage::block::Status::OutOfRange;
    std::memcpy(destination, g_storage + first * SECTOR_SIZE,
                static_cast<size_t>(count) * SECTOR_SIZE);
    return storage::block::Status::Ok;
}

storage::block::Status write_blocks(
    void* context, uint64_t first, uint64_t count, const void* source) {
    auto* memory = static_cast<MemoryDevice*>(context);
    if (memory == nullptr || source == nullptr) return storage::block::Status::InvalidArgument;
    if (memory->fail_writes) return storage::block::Status::IoError;
    if (first >= SECTOR_COUNT || count > SECTOR_COUNT - first) return storage::block::Status::OutOfRange;
    std::memcpy(g_storage + first * SECTOR_SIZE, source,
                static_cast<size_t>(count) * SECTOR_SIZE);
    return storage::block::Status::Ok;
}

storage::block::Status flush(void* context) {
    auto* memory = static_cast<MemoryDevice*>(context);
    if (memory == nullptr) return storage::block::Status::InvalidArgument;
    ++memory->flushes;
    return memory->fail_flush ? storage::block::Status::IoError : storage::block::Status::Ok;
}
}

int main() {
    using namespace fs::kurofs;
    std::memset(g_storage, 0, sizeof(g_storage));
    MemoryDevice memory{};
    storage::block::Device device{
        &memory, SECTOR_SIZE, SECTOR_COUNT, read_blocks, write_blocks, flush
    };

    if (format(&device, 16U) != Status::Ok || memory.flushes < 2U) return 1;
    FileSystem filesystem{};
    if (mount(&filesystem, &device) != Status::Ok) return 2;
    Geometry geometry{};
    if (get_geometry(&filesystem, &geometry) != Status::Ok ||
        geometry.data_start >= geometry.total_blocks) return 3;

    uint64_t first = UINT64_MAX;
    if (allocate_blocks(&filesystem, 0U, &first) != Status::InvalidArgument ||
        allocate_blocks(&filesystem, SECTOR_COUNT, &first) != Status::NoSpace) return 4;
    if (allocate_blocks(&filesystem, 3U, &first) != Status::Ok ||
        first != geometry.data_start) return 5;
    uint64_t second = UINT64_MAX;
    if (allocate_blocks(&filesystem, 5U, &second) != Status::Ok || second != first + 3U) return 6;

    FileSystem remounted{};
    if (mount(&remounted, &device) != Status::Ok) return 7;
    uint64_t third = UINT64_MAX;
    if (allocate_blocks(&remounted, 1U, &third) != Status::Ok || third != second + 5U) return 8;

    uint64_t regular_id = 0U;
    uint64_t directory_id = 0U;
    if (allocate_inode(&remounted, InodeType::Regular, &regular_id) != Status::Ok ||
        regular_id != 2U ||
        allocate_inode(&remounted, InodeType::Directory, &directory_id) != Status::Ok ||
        directory_id != 3U) return 9;
    Inode inode{};
    if (read_inode(&remounted, regular_id, &inode) != Status::Ok ||
        inode.type != InodeType::Regular || inode.size != 0U ||
        inode.extent_blocks != 0U || inode.link_count != 1U) return 10;

    FileSystem second_mount{};
    if (mount(&second_mount, &device) != Status::Ok) return 11;
    uint64_t next_inode = 0U;
    if (allocate_inode(&second_mount, InodeType::Regular, &next_inode) != Status::Ok ||
        next_inode != 4U) return 12;
    for (uint64_t expected = 5U; expected <= 16U; ++expected) {
        uint64_t allocated = 0U;
        if (allocate_inode(&second_mount, InodeType::Regular, &allocated) != Status::Ok ||
            allocated != expected) return 13;
    }
    uint64_t exhausted = 0U;
    if (allocate_inode(&second_mount, InodeType::Regular, &exhausted) != Status::NoSpace) return 14;
    if (allocate_inode(&second_mount, static_cast<InodeType>(99U), &exhausted) !=
        Status::InvalidArgument) return 15;

    // I/O failures never publish a successful allocation to the caller.
    memory.fail_writes = true;
    uint64_t failed = UINT64_MAX;
    if (allocate_blocks(&second_mount, 1U, &failed) != Status::BlockDeviceError ||
        failed != UINT64_MAX) return 16;
    memory.fail_writes = false;
    memory.fail_flush = true;
    uint64_t uncertain = UINT64_MAX;
    if (allocate_blocks(&second_mount, 1U, &uncertain) != Status::BlockDeviceError ||
        uncertain != UINT64_MAX) return 17;
    memory.fail_flush = false;

    std::puts("KuroFS persistent allocator tests passed");
    return 0;
}
''', encoding="utf-8")

    replace_once(
        "scripts/run-host-tests.sh",
        '"$OUT_DIR/test_vfs_process_paths"\n\n"$HOST_CXX" \\\n',
        '"$OUT_DIR/test_vfs_process_paths"\n\n'
        '# Exercise KuroFS v1 metadata persistence through the production block-device ABI.\n'
        '"$HOST_CXX" \\\n'
        '  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \\\n'
        '  tests/test_kurofs_allocator.cpp \\\n'
        '  kernel/fs/kurofs.cpp \\\n'
        '  -o "$OUT_DIR/test_kurofs_allocator"\n\n'
        '"$OUT_DIR/test_kurofs_allocator"\n\n'
        '"$HOST_CXX" \\\n',
    )

    print("[dev-apply-kurofs-allocator] applied KuroFS persistent extent/inode allocator")


if __name__ == "__main__":
    main()
