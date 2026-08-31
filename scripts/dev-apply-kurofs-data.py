#!/usr/bin/env python3
"""Apply KuroFS durable inode/data ownership primitives for 4.0 Pre-Steel."""

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
        "    InvalidRootInode,\n    NoSpace,\n    BlockDeviceError,\n",
        "    InvalidRootInode,\n    InvalidExtent,\n    StaleInode,\n    NoSpace,\n    BlockDeviceError,\n",
    )
    replace_once(
        "kernel/fs/kurofs.hpp",
        "Status allocate_inode(\n"
        "    FileSystem* filesystem, InodeType type, uint64_t* out_inode_id);\n\n"
        "const char* status_message(Status status);\n",
        "Status allocate_inode(\n"
        "    FileSystem* filesystem, InodeType type, uint64_t* out_inode_id);\n\n"
        "// Persist an update to an already allocated inode. The caller supplies the\n"
        "// generation it read; success increments that generation, making stale\n"
        "// snapshots fail deterministically. Any published extent must already be\n"
        "// fully allocated in the persistent bitmap.\n"
        "Status update_inode(FileSystem* filesystem, Inode* inode);\n\n"
        "// Write bytes into an already allocated extent. This is deliberately lower\n"
        "// level than inode publication: callers can make data durable first and only\n"
        "// then publish size/ownership through update_inode().\n"
        "Status write_extent_data(\n"
        "    FileSystem* filesystem,\n"
        "    uint64_t first_block,\n"
        "    uint64_t block_count,\n"
        "    uint64_t offset,\n"
        "    const void* source,\n"
        "    size_t size);\n\n"
        "// Read at most the persisted inode size. EOF is reported as zero bytes. The\n"
        "// supplied inode snapshot must still match the on-disk generation.\n"
        "Status read_inode_data(\n"
        "    FileSystem* filesystem,\n"
        "    const Inode* inode,\n"
        "    uint64_t offset,\n"
        "    void* destination,\n"
        "    size_t capacity,\n"
        "    size_t* out_read);\n\n"
        "const char* status_message(Status status);\n",
    )

    helper_anchor = "} // namespace\n\nStatus format(const storage::block::Device* device, uint32_t inode_count) {\n"
    helpers = r'''Status locate_inode(
    const FileSystem* filesystem,
    uint64_t inode_id,
    uint64_t* out_block,
    size_t* out_offset) {
    if (filesystem == nullptr || out_block == nullptr || out_offset == nullptr ||
        inode_id == 0U || inode_id > static_cast<uint64_t>(filesystem->geometry.inode_count)) {
        return Status::InvalidArgument;
    }
    const uint64_t zero_based = inode_id - 1U;
    uint64_t byte_offset = 0U;
    if (!multiply_u64(zero_based, INODE_SIZE, &byte_offset)) {
        return Status::ArithmeticOverflow;
    }
    const uint64_t relative_block = byte_offset / kSectorSize;
    const size_t offset = static_cast<size_t>(byte_offset % kSectorSize);
    if (offset + INODE_SIZE > kSectorSize ||
        relative_block >= filesystem->geometry.inode_table_blocks) {
        return Status::InvalidGeometry;
    }
    uint64_t table_block = 0U;
    if (!add_u64(filesystem->geometry.inode_table_start, relative_block, &table_block) ||
        table_block >= filesystem->geometry.total_blocks) {
        return Status::InvalidGeometry;
    }
    *out_block = table_block;
    *out_offset = offset;
    return Status::Ok;
}

Status validate_allocated_extent(
    FileSystem* filesystem,
    uint64_t first_block,
    uint64_t block_count,
    uint64_t* out_capacity) {
    if (filesystem == nullptr || out_capacity == nullptr) return Status::InvalidArgument;
    if (block_count == 0U) {
        if (first_block != 0U) return Status::InvalidExtent;
        *out_capacity = 0U;
        return Status::Ok;
    }
    uint64_t end_block = 0U;
    uint64_t capacity = 0U;
    if (!add_u64(first_block, block_count, &end_block) ||
        !multiply_u64(block_count, kSectorSize, &capacity)) {
        return Status::ArithmeticOverflow;
    }
    if (first_block < filesystem->geometry.data_start ||
        end_block > filesystem->geometry.total_blocks) {
        return Status::InvalidExtent;
    }

    uint8_t bitmap[kSectorSize]{};
    uint64_t loaded_bitmap = UINT64_MAX;
    for (uint64_t block = first_block; block < end_block; ++block) {
        const uint64_t bitmap_index = block / kBitsPerBitmapBlock;
        if (bitmap_index >= filesystem->geometry.allocation_bitmap_blocks) {
            return Status::InvalidGeometry;
        }
        if (bitmap_index != loaded_bitmap) {
            uint64_t bitmap_block = 0U;
            if (!add_u64(filesystem->geometry.allocation_bitmap_start,
                         bitmap_index, &bitmap_block) ||
                bitmap_block >= filesystem->geometry.total_blocks) {
                return Status::InvalidGeometry;
            }
            const Status status = read_one(filesystem->device, bitmap_block, bitmap);
            if (status != Status::Ok) return status;
            loaded_bitmap = bitmap_index;
        }
        const uint64_t bit = block % kBitsPerBitmapBlock;
        const size_t byte_index = static_cast<size_t>(bit / 8U);
        const uint8_t mask = static_cast<uint8_t>(
            UINT8_C(1) << static_cast<uint8_t>(bit % 8U));
        if ((bitmap[byte_index] & mask) == 0U) return Status::InvalidExtent;
    }
    *out_capacity = capacity;
    return Status::Ok;
}

Status validate_inode_extent(FileSystem* filesystem, const Inode& inode) {
    if (inode.id == 0U ||
        inode.id > static_cast<uint64_t>(filesystem->geometry.inode_count) ||
        (inode.type != InodeType::Regular && inode.type != InodeType::Directory) ||
        inode.link_count == 0U || inode.generation == 0U) {
        return Status::InvalidArgument;
    }
    uint64_t capacity = 0U;
    const Status status = validate_allocated_extent(
        filesystem, inode.extent_start, inode.extent_blocks, &capacity);
    if (status != Status::Ok) return status;
    if (inode.size > capacity ||
        (inode.extent_blocks == 0U && inode.size != 0U)) {
        return Status::InvalidExtent;
    }
    return Status::Ok;
}

Status zero_extent(FileSystem* filesystem, uint64_t first_block, uint64_t block_count) {
    uint64_t capacity = 0U;
    const Status valid = validate_allocated_extent(
        filesystem, first_block, block_count, &capacity);
    if (valid != Status::Ok) return valid;
    static_cast<void>(capacity);
    uint8_t zero[kSectorSize]{};
    for (uint64_t index = 0U; index < block_count; ++index) {
        uint64_t block = 0U;
        if (!add_u64(first_block, index, &block)) return Status::ArithmeticOverflow;
        const Status status = write_one(filesystem->device, block, zero);
        if (status != Status::Ok) return status;
    }
    return block_status(storage::block::flush(filesystem->device));
}

void copy_bytes(uint8_t* destination, const uint8_t* source, size_t size) {
    for (size_t index = 0U; index < size; ++index) destination[index] = source[index];
}

} // namespace

Status format(const storage::block::Device* device, uint32_t inode_count) {
'''
    replace_once("kernel/fs/kurofs.cpp", helper_anchor, helpers)

    replace_once(
        "kernel/fs/kurofs.cpp",
        "    status = publish_extent_allocation(filesystem, first, block_count);\n"
        "    if (status != Status::Ok) return status;\n"
        "    *out_first_block = first;\n",
        "    status = publish_extent_allocation(filesystem, first, block_count);\n"
        "    if (status != Status::Ok) return status;\n"
        "    // Allocation becomes visible only after stale device contents have been\n"
        "    // durably cleared. A failure here can leak the reserved bitmap range,\n"
        "    // but the failed range is never returned to a caller or double-issued.\n"
        "    status = zero_extent(filesystem, first, block_count);\n"
        "    if (status != Status::Ok) return status;\n"
        "    *out_first_block = first;\n",
    )

    public_anchor = "const char* status_message(Status status) {\n"
    public_impl = r'''Status update_inode(FileSystem* filesystem, Inode* inode) {
    if (filesystem == nullptr || inode == nullptr) return Status::InvalidArgument;
    if (!is_mounted(filesystem)) return Status::NotMounted;

    Inode current{};
    Status status = read_inode(filesystem, inode->id, &current);
    if (status != Status::Ok) return status;
    if (current.generation != inode->generation || current.type != inode->type) {
        return Status::StaleInode;
    }
    if (current.generation == UINT32_MAX) return Status::ArithmeticOverflow;
    status = validate_inode_extent(filesystem, *inode);
    if (status != Status::Ok) return status;

    Inode candidate = *inode;
    candidate.generation = current.generation + 1U;
    uint64_t table_block = 0U;
    size_t offset = 0U;
    status = locate_inode(filesystem, candidate.id, &table_block, &offset);
    if (status != Status::Ok) return status;
    uint8_t sector[kSectorSize]{};
    status = read_one(filesystem->device, table_block, sector);
    if (status != Status::Ok) return status;
    encode_inode(candidate, sector + offset);
    status = write_one(filesystem->device, table_block, sector);
    if (status != Status::Ok) return status;
    status = block_status(storage::block::flush(filesystem->device));
    if (status != Status::Ok) return status;
    *inode = candidate;
    return Status::Ok;
}

Status write_extent_data(
    FileSystem* filesystem,
    uint64_t first_block,
    uint64_t block_count,
    uint64_t offset,
    const void* source,
    size_t size) {
    if (filesystem == nullptr || (source == nullptr && size != 0U)) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;
    uint64_t capacity = 0U;
    Status status = validate_allocated_extent(
        filesystem, first_block, block_count, &capacity);
    if (status != Status::Ok) return status;
    const uint64_t size64 = static_cast<uint64_t>(size);
    uint64_t end = 0U;
    if (!add_u64(offset, size64, &end)) return Status::ArithmeticOverflow;
    if (end > capacity) return Status::NoSpace;
    if (size == 0U) return Status::Ok;

    const auto* input = static_cast<const uint8_t*>(source);
    size_t done = 0U;
    while (done < size) {
        const uint64_t absolute = offset + static_cast<uint64_t>(done);
        const uint64_t relative_block = absolute / kSectorSize;
        const size_t in_sector = static_cast<size_t>(absolute % kSectorSize);
        uint64_t disk_block = 0U;
        if (!add_u64(first_block, relative_block, &disk_block)) {
            return Status::ArithmeticOverflow;
        }
        const size_t remaining = size - done;
        const size_t available = kSectorSize - in_sector;
        const size_t chunk = remaining < available ? remaining : available;
        uint8_t sector[kSectorSize]{};
        if (in_sector != 0U || chunk != kSectorSize) {
            status = read_one(filesystem->device, disk_block, sector);
            if (status != Status::Ok) return status;
        }
        copy_bytes(sector + in_sector, input + done, chunk);
        status = write_one(filesystem->device, disk_block, sector);
        if (status != Status::Ok) return status;
        done += chunk;
    }
    return block_status(storage::block::flush(filesystem->device));
}

Status read_inode_data(
    FileSystem* filesystem,
    const Inode* inode,
    uint64_t offset,
    void* destination,
    size_t capacity,
    size_t* out_read) {
    if (filesystem == nullptr || inode == nullptr || out_read == nullptr ||
        (destination == nullptr && capacity != 0U)) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;
    *out_read = 0U;

    Inode current{};
    Status status = read_inode(filesystem, inode->id, &current);
    if (status != Status::Ok) return status;
    if (current.generation != inode->generation || current.type != inode->type ||
        current.extent_start != inode->extent_start ||
        current.extent_blocks != inode->extent_blocks || current.size != inode->size) {
        return Status::StaleInode;
    }
    status = validate_inode_extent(filesystem, current);
    if (status != Status::Ok) return status;
    if (offset >= current.size || capacity == 0U) return Status::Ok;

    const uint64_t remaining64 = current.size - offset;
    size_t wanted = capacity;
    if (remaining64 < static_cast<uint64_t>(wanted)) {
        wanted = static_cast<size_t>(remaining64);
    }
    auto* output = static_cast<uint8_t*>(destination);
    size_t done = 0U;
    while (done < wanted) {
        const uint64_t absolute = offset + static_cast<uint64_t>(done);
        const uint64_t relative_block = absolute / kSectorSize;
        const size_t in_sector = static_cast<size_t>(absolute % kSectorSize);
        uint64_t disk_block = 0U;
        if (!add_u64(current.extent_start, relative_block, &disk_block)) {
            return Status::ArithmeticOverflow;
        }
        uint8_t sector[kSectorSize]{};
        status = read_one(filesystem->device, disk_block, sector);
        if (status != Status::Ok) return status;
        const size_t remaining = wanted - done;
        const size_t available = kSectorSize - in_sector;
        const size_t chunk = remaining < available ? remaining : available;
        copy_bytes(output + done, sector + in_sector, chunk);
        done += chunk;
    }
    *out_read = done;
    return Status::Ok;
}

const char* status_message(Status status) {
'''
    replace_once("kernel/fs/kurofs.cpp", public_anchor, public_impl)
    replace_once(
        "kernel/fs/kurofs.cpp",
        "        case Status::InvalidRootInode: return \"invalid root inode\";\n"
        "        case Status::NoSpace: return \"no free KuroFS space\";\n",
        "        case Status::InvalidRootInode: return \"invalid root inode\";\n"
        "        case Status::InvalidExtent: return \"invalid or unallocated KuroFS extent\";\n"
        "        case Status::StaleInode: return \"stale KuroFS inode generation\";\n"
        "        case Status::NoSpace: return \"no free KuroFS space\";\n",
    )

    test_path = ROOT / "tests/test_kurofs_data.cpp"
    if test_path.exists():
        raise SystemExit("tests/test_kurofs_data.cpp already exists")
    test_path.write_text(r'''#include "../kernel/fs/kurofs.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace system_metrics { void record_disk_blocks(uint64_t) {} }

namespace {
constexpr uint32_t SECTOR_SIZE = 512U;
constexpr uint64_t SECTOR_COUNT = 128U;
uint8_t storage_bytes[SECTOR_SIZE * SECTOR_COUNT]{};
struct MemoryDevice { uint64_t flushes; };

storage::block::Status read_cb(void*, uint64_t first, uint64_t count, void* destination) {
    if (destination == nullptr || first >= SECTOR_COUNT || count > SECTOR_COUNT - first)
        return storage::block::Status::OutOfRange;
    std::memcpy(destination, storage_bytes + first * SECTOR_SIZE,
                static_cast<size_t>(count) * SECTOR_SIZE);
    return storage::block::Status::Ok;
}
storage::block::Status write_cb(void*, uint64_t first, uint64_t count, const void* source) {
    if (source == nullptr || first >= SECTOR_COUNT || count > SECTOR_COUNT - first)
        return storage::block::Status::OutOfRange;
    std::memcpy(storage_bytes + first * SECTOR_SIZE, source,
                static_cast<size_t>(count) * SECTOR_SIZE);
    return storage::block::Status::Ok;
}
storage::block::Status flush_cb(void* context) {
    auto* memory = static_cast<MemoryDevice*>(context);
    ++memory->flushes;
    return storage::block::Status::Ok;
}

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}

int main() {
    using namespace fs::kurofs;
    std::memset(storage_bytes, 0xA5, sizeof(storage_bytes));
    MemoryDevice memory{};
    storage::block::Device device{&memory, SECTOR_SIZE, SECTOR_COUNT, read_cb, write_cb, flush_cb};
    if (!expect(format(&device, 16U) == Status::Ok, "format")) return 1;
    FileSystem fs{};
    if (!expect(mount(&fs, &device) == Status::Ok, "mount")) return 1;

    uint64_t extent = 0U;
    if (!expect(allocate_blocks(&fs, 2U, &extent) == Status::Ok, "allocate extent")) return 1;
    for (size_t index = 0U; index < 2U * SECTOR_SIZE; ++index) {
        if (!expect(storage_bytes[extent * SECTOR_SIZE + index] == 0U,
                    "allocated extent must be zeroed")) return 1;
    }

    uint64_t inode_id = 0U;
    if (!expect(allocate_inode(&fs, InodeType::Regular, &inode_id) == Status::Ok,
                "allocate inode")) return 1;
    Inode inode{};
    if (!expect(read_inode(&fs, inode_id, &inode) == Status::Ok, "read fresh inode")) return 1;
    const uint32_t original_generation = inode.generation;

    uint8_t payload[700]{};
    for (size_t index = 0U; index < sizeof(payload); ++index)
        payload[index] = static_cast<uint8_t>((index * 13U + 7U) & 0xffU);
    if (!expect(write_extent_data(&fs, extent, 2U, 0U, payload, sizeof(payload)) == Status::Ok,
                "write extent data")) return 1;
    inode.extent_start = extent;
    inode.extent_blocks = 2U;
    inode.size = sizeof(payload);
    if (!expect(update_inode(&fs, &inode) == Status::Ok, "publish inode ownership")) return 1;
    if (!expect(inode.generation == original_generation + 1U, "generation increment")) return 1;

    Inode stale = inode;
    stale.generation = original_generation;
    if (!expect(update_inode(&fs, &stale) == Status::StaleInode, "reject stale update")) return 1;

    Inode illegal = inode;
    illegal.extent_start = fs.geometry.data_start + 10U;
    illegal.extent_blocks = 1U;
    illegal.size = 1U;
    if (!expect(update_inode(&fs, &illegal) == Status::InvalidExtent,
                "reject unallocated extent ownership")) return 1;

    uint8_t patch[100]{};
    for (size_t index = 0U; index < sizeof(patch); ++index) patch[index] = 0xCCU;
    if (!expect(write_extent_data(&fs, extent, 2U, 500U, patch, sizeof(patch)) == Status::Ok,
                "cross-sector partial write")) return 1;
    for (size_t index = 0U; index < sizeof(patch); ++index) payload[500U + index] = 0xCCU;

    FileSystem remounted{};
    if (!expect(mount(&remounted, &device) == Status::Ok, "remount")) return 1;
    Inode persisted{};
    if (!expect(read_inode(&remounted, inode_id, &persisted) == Status::Ok,
                "read persisted inode")) return 1;
    if (!expect(persisted.extent_start == extent && persisted.extent_blocks == 2U &&
                persisted.size == sizeof(payload) && persisted.generation == inode.generation,
                "persisted inode fields")) return 1;

    uint8_t output[800]{};
    size_t read = 0U;
    if (!expect(read_inode_data(&remounted, &persisted, 0U, output, sizeof(output), &read) == Status::Ok,
                "read persisted data")) return 1;
    if (!expect(read == sizeof(payload), "EOF-bounded read")) return 1;
    if (!expect(std::memcmp(output, payload, sizeof(payload)) == 0, "payload persistence")) return 1;
    if (!expect(read_inode_data(&remounted, &persisted, persisted.size, output, sizeof(output), &read) == Status::Ok && read == 0U,
                "EOF zero read")) return 1;
    if (!expect(write_extent_data(&remounted, extent, 2U, 1000U, patch, sizeof(patch)) == Status::NoSpace,
                "reject extent overflow")) return 1;

    Inode stale_read = persisted;
    stale_read.generation -= 1U;
    if (!expect(read_inode_data(&remounted, &stale_read, 0U, output, sizeof(output), &read) == Status::StaleInode,
                "reject stale read snapshot")) return 1;

    std::puts("KuroFS inode/data persistence tests passed");
    return 0;
}
''', encoding="utf-8")

    replace_once(
        "scripts/run-host-tests.sh",
        '"$OUT_DIR/test_kurofs_allocator"\n\n',
        '"$OUT_DIR/test_kurofs_allocator"\n\n'
        '# Exercise durable KuroFS extent contents and generation-safe inode publication.\n'
        '"$HOST_CXX" \\\n'
        '  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \\\n'
        '  tests/test_kurofs_data.cpp \\\n'
        '  kernel/fs/kurofs.cpp \\\n'
        '  -o "$OUT_DIR/test_kurofs_data"\n\n'
        '"$OUT_DIR/test_kurofs_data"\n\n',
    )


if __name__ == "__main__":
    main()
