#include "kurofs.hpp"

namespace fs::kurofs {
namespace {

constexpr size_t kSectorSize = SUPPORTED_SECTOR_SIZE;
constexpr uint64_t kPrimarySuperblock = 0U;
constexpr uint64_t kSecondarySuperblock = 1U;
constexpr uint64_t kInodeTableStart = 2U;
constexpr size_t kSuperblockChecksumOffset = kSectorSize - sizeof(uint32_t);
constexpr size_t kInodeChecksumOffset = INODE_SIZE - sizeof(uint32_t);
constexpr uint8_t kMagic[8] = {'K', 'U', 'R', 'O', 'F', 'S', '1', 0U};
constexpr uint64_t kMinimumDataBlocks = 16U;
constexpr uint64_t kBitsPerBitmapBlock = static_cast<uint64_t>(kSectorSize) * 8U;
constexpr uint32_t kFreeInodeType = 0U;
constexpr uint32_t kInodeTombstoneFlag = UINT32_C(1) << 31U;

uint32_t load_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8U) |
        (static_cast<uint32_t>(bytes[2]) << 16U) |
        (static_cast<uint32_t>(bytes[3]) << 24U);
}

uint64_t load_u64(const uint8_t* bytes) {
    uint64_t value = 0U;
    for (size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

void store_u32(uint8_t* bytes, uint32_t value) {
    for (size_t index = 0U; index < 4U; ++index) {
        bytes[index] = static_cast<uint8_t>((value >> (index * 8U)) & UINT32_C(0xff));
    }
}

void store_u64(uint8_t* bytes, uint64_t value) {
    for (size_t index = 0U; index < 8U; ++index) {
        bytes[index] = static_cast<uint8_t>((value >> (index * 8U)) & UINT64_C(0xff));
    }
}

void clear_bytes(uint8_t* bytes, size_t size) {
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

bool all_zero(const uint8_t* bytes, size_t size) {
    if (bytes == nullptr) return false;
    for (size_t index = 0U; index < size; ++index) {
        if (bytes[index] != 0U) return false;
    }
    return true;
}

uint32_t crc32(const uint8_t* bytes, size_t size) {
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0U; index < size; ++index) {
        crc ^= static_cast<uint32_t>(bytes[index]);
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(
                -static_cast<int32_t>(crc & UINT32_C(1)));
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

bool add_u64(uint64_t left, uint64_t right, uint64_t* output) {
    if (output == nullptr || right > UINT64_MAX - left) return false;
    *output = left + right;
    return true;
}

bool multiply_u64(uint64_t left, uint64_t right, uint64_t* output) {
    if (output == nullptr) return false;
    if (left != 0U && right > UINT64_MAX / left) return false;
    *output = left * right;
    return true;
}

uint64_t divide_round_up(uint64_t value, uint64_t divisor) {
    return value / divisor + ((value % divisor) == 0U ? 0U : 1U);
}

Status block_status(storage::block::Status status) {
    return status == storage::block::Status::Ok ? Status::Ok : Status::BlockDeviceError;
}

bool geometry_equal(const Geometry& left, const Geometry& right) {
    return left.sector_size == right.sector_size &&
        left.total_blocks == right.total_blocks &&
        left.generation == right.generation &&
        left.inode_count == right.inode_count &&
        left.inode_size == right.inode_size &&
        left.inode_table_start == right.inode_table_start &&
        left.inode_table_blocks == right.inode_table_blocks &&
        left.allocation_bitmap_start == right.allocation_bitmap_start &&
        left.allocation_bitmap_blocks == right.allocation_bitmap_blocks &&
        left.data_start == right.data_start &&
        left.root_inode == right.root_inode &&
        left.feature_flags == right.feature_flags;
}

Status calculate_geometry(
    const storage::block::Device* device,
    uint32_t inode_count,
    Geometry* output) {
    if (device == nullptr || output == nullptr || inode_count == 0U) {
        return Status::InvalidArgument;
    }
    if (device->sector_size != SUPPORTED_SECTOR_SIZE) {
        return Status::UnsupportedSectorSize;
    }

    uint64_t inode_bytes = 0U;
    if (!multiply_u64(static_cast<uint64_t>(inode_count), INODE_SIZE, &inode_bytes)) {
        return Status::ArithmeticOverflow;
    }
    const uint64_t inode_blocks = divide_round_up(inode_bytes, kSectorSize);
    const uint64_t bitmap_blocks = divide_round_up(device->sector_count, kBitsPerBitmapBlock);
    uint64_t bitmap_start = 0U;
    uint64_t data_start = 0U;
    if (!add_u64(kInodeTableStart, inode_blocks, &bitmap_start) ||
        !add_u64(bitmap_start, bitmap_blocks, &data_start)) {
        return Status::ArithmeticOverflow;
    }
    if (data_start >= device->sector_count ||
        device->sector_count - data_start < kMinimumDataBlocks) {
        return Status::DeviceTooSmall;
    }

    output->sector_size = SUPPORTED_SECTOR_SIZE;
    output->total_blocks = device->sector_count;
    output->generation = 1U;
    output->inode_count = inode_count;
    output->inode_size = INODE_SIZE;
    output->inode_table_start = kInodeTableStart;
    output->inode_table_blocks = inode_blocks;
    output->allocation_bitmap_start = bitmap_start;
    output->allocation_bitmap_blocks = bitmap_blocks;
    output->data_start = data_start;
    output->root_inode = ROOT_INODE;
    output->feature_flags = FEATURE_NONE;
    return Status::Ok;
}

void encode_superblock(const Geometry& geometry, uint8_t* sector) {
    clear_bytes(sector, kSectorSize);
    for (size_t index = 0U; index < sizeof(kMagic); ++index) sector[index] = kMagic[index];
    store_u32(sector + 8U, FORMAT_VERSION);
    store_u32(sector + 12U, geometry.sector_size);
    store_u64(sector + 16U, geometry.total_blocks);
    store_u64(sector + 24U, geometry.generation);
    store_u32(sector + 32U, geometry.inode_count);
    store_u32(sector + 36U, geometry.inode_size);
    store_u64(sector + 40U, geometry.inode_table_start);
    store_u64(sector + 48U, geometry.inode_table_blocks);
    store_u64(sector + 56U, geometry.allocation_bitmap_start);
    store_u64(sector + 64U, geometry.allocation_bitmap_blocks);
    store_u64(sector + 72U, geometry.data_start);
    store_u64(sector + 80U, geometry.root_inode);
    store_u64(sector + 88U, geometry.feature_flags);
    store_u32(sector + kSuperblockChecksumOffset, crc32(sector, kSuperblockChecksumOffset));
}

Status validate_geometry(const Geometry& geometry, const storage::block::Device* device) {
    if (device == nullptr || geometry.sector_size != SUPPORTED_SECTOR_SIZE ||
        geometry.sector_size != device->sector_size ||
        geometry.total_blocks != device->sector_count ||
        geometry.inode_count == 0U || geometry.inode_size != INODE_SIZE ||
        geometry.inode_table_start != kInodeTableStart ||
        geometry.root_inode != ROOT_INODE || geometry.generation == 0U ||
        geometry.feature_flags != FEATURE_NONE) {
        return Status::InvalidGeometry;
    }
    uint64_t inode_bytes = 0U;
    if (!multiply_u64(static_cast<uint64_t>(geometry.inode_count), INODE_SIZE, &inode_bytes)) {
        return Status::ArithmeticOverflow;
    }
    const uint64_t expected_inode_blocks = divide_round_up(inode_bytes, kSectorSize);
    const uint64_t expected_bitmap_blocks = divide_round_up(geometry.total_blocks, kBitsPerBitmapBlock);
    uint64_t expected_bitmap_start = 0U;
    uint64_t expected_data_start = 0U;
    if (!add_u64(kInodeTableStart, expected_inode_blocks, &expected_bitmap_start) ||
        !add_u64(expected_bitmap_start, expected_bitmap_blocks, &expected_data_start)) {
        return Status::ArithmeticOverflow;
    }
    if (geometry.inode_table_blocks != expected_inode_blocks ||
        geometry.allocation_bitmap_start != expected_bitmap_start ||
        geometry.allocation_bitmap_blocks != expected_bitmap_blocks ||
        geometry.data_start != expected_data_start ||
        geometry.data_start >= geometry.total_blocks) {
        return Status::InvalidGeometry;
    }
    return Status::Ok;
}

Status decode_superblock(
    const uint8_t* sector,
    const storage::block::Device* device,
    Geometry* output) {
    if (sector == nullptr || device == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    for (size_t index = 0U; index < sizeof(kMagic); ++index) {
        if (sector[index] != kMagic[index]) return Status::InvalidSuperblock;
    }
    if (load_u32(sector + 8U) != FORMAT_VERSION) return Status::InvalidSuperblock;
    const uint32_t stored_crc = load_u32(sector + kSuperblockChecksumOffset);
    if (stored_crc != crc32(sector, kSuperblockChecksumOffset)) {
        return Status::CorruptSuperblock;
    }

    Geometry geometry{};
    geometry.sector_size = load_u32(sector + 12U);
    geometry.total_blocks = load_u64(sector + 16U);
    geometry.generation = load_u64(sector + 24U);
    geometry.inode_count = load_u32(sector + 32U);
    geometry.inode_size = load_u32(sector + 36U);
    geometry.inode_table_start = load_u64(sector + 40U);
    geometry.inode_table_blocks = load_u64(sector + 48U);
    geometry.allocation_bitmap_start = load_u64(sector + 56U);
    geometry.allocation_bitmap_blocks = load_u64(sector + 64U);
    geometry.data_start = load_u64(sector + 72U);
    geometry.root_inode = load_u64(sector + 80U);
    geometry.feature_flags = load_u64(sector + 88U);
    const Status valid = validate_geometry(geometry, device);
    if (valid != Status::Ok) return valid;
    *output = geometry;
    return Status::Ok;
}

void encode_inode(const Inode& inode, uint8_t* bytes) {
    clear_bytes(bytes, INODE_SIZE);
    store_u64(bytes + 0U, inode.id);
    store_u32(bytes + 8U, static_cast<uint32_t>(inode.type));
    store_u32(bytes + 12U, inode.flags);
    store_u64(bytes + 16U, inode.size);
    store_u64(bytes + 24U, inode.extent_start);
    store_u64(bytes + 32U, inode.extent_blocks);
    store_u32(bytes + 40U, inode.link_count);
    store_u32(bytes + 44U, inode.generation);
    store_u32(bytes + 48U, inode.revision);
    store_u32(bytes + kInodeChecksumOffset, crc32(bytes, kInodeChecksumOffset));
}

void encode_free_inode(uint64_t inode_id, uint32_t generation, uint8_t* bytes) {
    clear_bytes(bytes, INODE_SIZE);
    store_u64(bytes + 0U, inode_id);
    store_u32(bytes + 8U, kFreeInodeType);
    store_u32(bytes + 12U, kInodeTombstoneFlag);
    store_u32(bytes + 44U, generation);
    store_u32(bytes + kInodeChecksumOffset, crc32(bytes, kInodeChecksumOffset));
}

bool decode_free_inode(
    const uint8_t* bytes, uint64_t expected_id, uint32_t* out_generation) {
    if (bytes == nullptr || out_generation == nullptr) return false;
    if (all_zero(bytes, INODE_SIZE)) {
        *out_generation = 1U;
        return true;
    }
    if (load_u32(bytes + kInodeChecksumOffset) !=
        crc32(bytes, kInodeChecksumOffset)) return false;
    const uint32_t generation = load_u32(bytes + 44U);
    if (load_u64(bytes + 0U) != expected_id ||
        load_u32(bytes + 8U) != kFreeInodeType ||
        load_u32(bytes + 12U) != kInodeTombstoneFlag ||
        load_u64(bytes + 16U) != 0U || load_u64(bytes + 24U) != 0U ||
        load_u64(bytes + 32U) != 0U || load_u32(bytes + 40U) != 0U ||
        generation == 0U || load_u32(bytes + 48U) != 0U) return false;
    *out_generation = generation;
    return true;
}

Status decode_inode(const uint8_t* bytes, uint64_t expected_id, Inode* output) {
    if (bytes == nullptr || output == nullptr) return Status::InvalidArgument;
    uint32_t free_generation = 0U;
    if (decode_free_inode(bytes, expected_id, &free_generation)) {
        return Status::NotFound;
    }
    if (load_u32(bytes + kInodeChecksumOffset) != crc32(bytes, kInodeChecksumOffset)) {
        return Status::InvalidRootInode;
    }
    Inode inode{};
    inode.id = load_u64(bytes + 0U);
    inode.type = static_cast<InodeType>(load_u32(bytes + 8U));
    inode.flags = load_u32(bytes + 12U);
    inode.size = load_u64(bytes + 16U);
    inode.extent_start = load_u64(bytes + 24U);
    inode.extent_blocks = load_u64(bytes + 32U);
    inode.link_count = load_u32(bytes + 40U);
    inode.generation = load_u32(bytes + 44U);
    inode.revision = load_u32(bytes + 48U);
    // Pre-revision KuroFS v1 images left this reserved field zero. Treat
    // that encoding as the initial metadata revision so existing v1
    // volumes remain mountable and upgrade naturally on the next write.
    if (inode.revision == 0U) inode.revision = 1U;
    if (inode.id != expected_id ||
        (inode.type != InodeType::Regular && inode.type != InodeType::Directory) ||
        inode.link_count == 0U || inode.generation == 0U || inode.revision == 0U) {
        return Status::InvalidRootInode;
    }
    *output = inode;
    return Status::Ok;
}

Status write_one(
    const storage::block::Device* device,
    uint64_t block,
    const uint8_t* sector) {
    return block_status(storage::block::write_blocks(device, block, 1U, sector, kSectorSize));
}

Status read_one(
    const storage::block::Device* device,
    uint64_t block,
    uint8_t* sector) {
    return block_status(storage::block::read_blocks(device, block, 1U, sector, kSectorSize));
}

Status initialize_inode_table(
    const storage::block::Device* device,
    const Geometry& geometry) {
    uint8_t sector[kSectorSize]{};
    for (uint64_t block = 0U; block < geometry.inode_table_blocks; ++block) {
        const Status status = write_one(device, geometry.inode_table_start + block, sector);
        if (status != Status::Ok) return status;
    }

    Inode root{};
    root.id = ROOT_INODE;
    root.type = InodeType::Directory;
    root.flags = 0U;
    root.size = 0U;
    root.extent_start = 0U;
    root.extent_blocks = 0U;
    root.link_count = 1U;
    root.generation = 1U;
    root.revision = 1U;
    encode_inode(root, sector);
    return write_one(device, geometry.inode_table_start, sector);
}

Status initialize_bitmap(
    const storage::block::Device* device,
    const Geometry& geometry) {
    for (uint64_t bitmap_index = 0U;
         bitmap_index < geometry.allocation_bitmap_blocks;
         ++bitmap_index) {
        uint8_t sector[kSectorSize]{};
        const uint64_t first_represented = bitmap_index * kBitsPerBitmapBlock;
        for (uint64_t bit = 0U; bit < kBitsPerBitmapBlock; ++bit) {
            const uint64_t block = first_represented + bit;
            if (block >= geometry.total_blocks) break;
            if (block < geometry.data_start) {
                const size_t byte_index = static_cast<size_t>(bit / 8U);
                const uint8_t bit_mask = static_cast<uint8_t>(UINT8_C(1) << static_cast<uint8_t>(bit % 8U));
                sector[byte_index] = static_cast<uint8_t>(sector[byte_index] | bit_mask);
            }
        }
        const Status status = write_one(
            device, geometry.allocation_bitmap_start + bitmap_index, sector);
        if (status != Status::Ok) return status;
    }
    return Status::Ok;
}

Status read_superblock_copy(
    const storage::block::Device* device,
    uint64_t block,
    Geometry* output) {
    uint8_t sector[kSectorSize]{};
    const Status read_status = read_one(device, block, sector);
    if (read_status != Status::Ok) return read_status;
    return decode_superblock(sector, device, output);
}

Status find_free_extent(
    FileSystem* filesystem, uint64_t block_count, uint64_t* out_start) {
    uint8_t bitmap[kSectorSize]{};
    uint64_t loaded_bitmap = UINT64_MAX;
    uint64_t run_start = 0U;
    uint64_t run_length = 0U;
    for (uint64_t block = filesystem->geometry.data_start;
         block < filesystem->geometry.total_blocks; ++block) {
        const uint64_t bitmap_index = block / kBitsPerBitmapBlock;
        if (bitmap_index >= filesystem->geometry.allocation_bitmap_blocks) {
            return Status::InvalidGeometry;
        }
        if (bitmap_index != loaded_bitmap) {
            const Status status = read_one(
                filesystem->device,
                filesystem->geometry.allocation_bitmap_start + bitmap_index,
                bitmap);
            if (status != Status::Ok) return status;
            loaded_bitmap = bitmap_index;
        }
        const uint64_t bit = block % kBitsPerBitmapBlock;
        const size_t byte_index = static_cast<size_t>(bit / 8U);
        const uint8_t mask = static_cast<uint8_t>(
            UINT8_C(1) << static_cast<uint8_t>(bit % 8U));
        if ((bitmap[byte_index] & mask) == 0U) {
            if (run_length == 0U) run_start = block;
            ++run_length;
            if (run_length == block_count) {
                *out_start = run_start;
                return Status::Ok;
            }
        } else {
            run_length = 0U;
        }
    }
    return Status::NoSpace;
}

Status publish_extent_allocation(
    FileSystem* filesystem, uint64_t first_block, uint64_t block_count) {
    uint64_t end_block = 0U;
    if (!add_u64(first_block, block_count, &end_block) ||
        first_block < filesystem->geometry.data_start ||
        end_block > filesystem->geometry.total_blocks) {
        return Status::InvalidGeometry;
    }
    uint64_t current = first_block;
    while (current < end_block) {
        const uint64_t bitmap_index = current / kBitsPerBitmapBlock;
        if (bitmap_index >= filesystem->geometry.allocation_bitmap_blocks) {
            return Status::InvalidGeometry;
        }
        uint8_t bitmap[kSectorSize]{};
        const uint64_t bitmap_block =
            filesystem->geometry.allocation_bitmap_start + bitmap_index;
        Status status = read_one(filesystem->device, bitmap_block, bitmap);
        if (status != Status::Ok) return status;
        const uint64_t represented_end =
            (bitmap_index + 1U) * kBitsPerBitmapBlock;
        const uint64_t segment_end =
            end_block < represented_end ? end_block : represented_end;
        for (uint64_t block = current; block < segment_end; ++block) {
            const uint64_t bit = block % kBitsPerBitmapBlock;
            const size_t byte_index = static_cast<size_t>(bit / 8U);
            const uint8_t mask = static_cast<uint8_t>(
                UINT8_C(1) << static_cast<uint8_t>(bit % 8U));
            // Re-check before publication. KuroFS currently serializes
            // metadata callers; this additionally refuses inconsistent
            // on-disk state rather than silently double-allocating.
            if ((bitmap[byte_index] & mask) != 0U) return Status::NoSpace;
            bitmap[byte_index] = static_cast<uint8_t>(bitmap[byte_index] | mask);
        }
        status = write_one(filesystem->device, bitmap_block, bitmap);
        if (status != Status::Ok) return status;
        current = segment_end;
    }
    return block_status(storage::block::flush(filesystem->device));
}

Status extent_is_free(
    FileSystem* filesystem, uint64_t first_block, uint64_t block_count) {
    if (filesystem == nullptr || block_count == 0U) return Status::InvalidArgument;
    uint64_t end_block = 0U;
    if (!add_u64(first_block, block_count, &end_block) ||
        first_block < filesystem->geometry.data_start ||
        end_block > filesystem->geometry.total_blocks) {
        return Status::NoSpace;
    }

    uint8_t bitmap[kSectorSize]{};
    uint64_t loaded_bitmap = UINT64_MAX;
    for (uint64_t block = first_block; block < end_block; ++block) {
        const uint64_t bitmap_index = block / kBitsPerBitmapBlock;
        if (bitmap_index >= filesystem->geometry.allocation_bitmap_blocks) {
            return Status::InvalidGeometry;
        }
        if (bitmap_index != loaded_bitmap) {
            const Status status = read_one(
                filesystem->device,
                filesystem->geometry.allocation_bitmap_start + bitmap_index,
                bitmap);
            if (status != Status::Ok) return status;
            loaded_bitmap = bitmap_index;
        }
        const uint64_t bit = block % kBitsPerBitmapBlock;
        const size_t byte_index = static_cast<size_t>(bit / 8U);
        const uint8_t mask = static_cast<uint8_t>(
            UINT8_C(1) << static_cast<uint8_t>(bit % 8U));
        if ((bitmap[byte_index] & mask) != 0U) return Status::NoSpace;
    }
    return Status::Ok;
}

Status locate_inode(
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

Status release_extent(
    FileSystem* filesystem, uint64_t first_block, uint64_t block_count) {
    if (filesystem == nullptr || block_count == 0U) return Status::InvalidArgument;
    uint64_t capacity = 0U;
    Status status = validate_allocated_extent(
        filesystem, first_block, block_count, &capacity);
    if (status != Status::Ok) return status;
    static_cast<void>(capacity);

    uint64_t end_block = 0U;
    if (!add_u64(first_block, block_count, &end_block)) {
        return Status::ArithmeticOverflow;
    }
    uint64_t current = first_block;
    while (current < end_block) {
        const uint64_t bitmap_index = current / kBitsPerBitmapBlock;
        uint8_t bitmap[kSectorSize]{};
        const uint64_t bitmap_block =
            filesystem->geometry.allocation_bitmap_start + bitmap_index;
        status = read_one(filesystem->device, bitmap_block, bitmap);
        if (status != Status::Ok) return status;
        const uint64_t represented_end =
            (bitmap_index + 1U) * kBitsPerBitmapBlock;
        const uint64_t segment_end =
            end_block < represented_end ? end_block : represented_end;
        for (uint64_t block = current; block < segment_end; ++block) {
            const uint64_t bit = block % kBitsPerBitmapBlock;
            const size_t byte_index = static_cast<size_t>(bit / 8U);
            const uint8_t mask = static_cast<uint8_t>(
                UINT8_C(1) << static_cast<uint8_t>(bit % 8U));
            bitmap[byte_index] = static_cast<uint8_t>(bitmap[byte_index] & ~mask);
        }
        status = write_one(filesystem->device, bitmap_block, bitmap);
        if (status != Status::Ok) return status;
        current = segment_end;
    }
    return block_status(storage::block::flush(filesystem->device));
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

Status zero_extent_range(
    FileSystem* filesystem,
    uint64_t first_block,
    uint64_t block_count,
    uint64_t offset,
    uint64_t length) {
    uint64_t capacity = 0U;
    Status status = validate_allocated_extent(
        filesystem, first_block, block_count, &capacity);
    if (status != Status::Ok) return status;
    uint64_t end = 0U;
    if (!add_u64(offset, length, &end)) return Status::ArithmeticOverflow;
    if (end > capacity) return Status::InvalidExtent;
    if (length == 0U) return Status::Ok;

    uint64_t done = 0U;
    while (done < length) {
        const uint64_t absolute = offset + done;
        const uint64_t relative_block = absolute / kSectorSize;
        const size_t in_sector = static_cast<size_t>(absolute % kSectorSize);
        uint64_t disk_block = 0U;
        if (!add_u64(first_block, relative_block, &disk_block)) {
            return Status::ArithmeticOverflow;
        }
        const uint64_t remaining = length - done;
        const size_t available = kSectorSize - in_sector;
        const size_t chunk = remaining < static_cast<uint64_t>(available)
            ? static_cast<size_t>(remaining) : available;
        uint8_t sector[kSectorSize]{};
        if (in_sector != 0U || chunk != kSectorSize) {
            status = read_one(filesystem->device, disk_block, sector);
            if (status != Status::Ok) return status;
        }
        clear_bytes(sector + in_sector, chunk);
        status = write_one(filesystem->device, disk_block, sector);
        if (status != Status::Ok) return status;
        done += static_cast<uint64_t>(chunk);
    }
    return block_status(storage::block::flush(filesystem->device));
}

void copy_bytes(uint8_t* destination, const uint8_t* source, size_t size) {
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
        left.generation == right.generation && left.revision == right.revision;
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

Status retire_inode(FileSystem* filesystem, const Inode& snapshot) {
    Inode current{};
    Status status = require_current_snapshot(filesystem, &snapshot, &current);
    if (status != Status::Ok) return status;
    if (current.id == ROOT_INODE || current.link_count != 1U) {
        return Status::InvalidArgument;
    }
    if (current.type == InodeType::Directory && current.size != 0U) {
        return Status::DirectoryNotEmpty;
    }
    if (current.generation == UINT32_MAX) return Status::ArithmeticOverflow;

    uint64_t table_block = 0U;
    size_t offset = 0U;
    status = locate_inode(filesystem, current.id, &table_block, &offset);
    if (status != Status::Ok) return status;
    uint8_t sector[kSectorSize]{};
    status = read_one(filesystem->device, table_block, sector);
    if (status != Status::Ok) return status;
    encode_free_inode(current.id, current.generation + 1U, sector + offset);
    status = write_one(filesystem->device, table_block, sector);
    if (status != Status::Ok) return status;
    status = block_status(storage::block::flush(filesystem->device));
    if (status != Status::Ok || current.extent_blocks == 0U) return status;
    return release_extent(
        filesystem, current.extent_start, current.extent_blocks);
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

Status format(const storage::block::Device* device, uint32_t inode_count) {
    if (device == nullptr || inode_count == 0U) return Status::InvalidArgument;
    const storage::block::Status valid_device = storage::block::validate(device);
    if (valid_device != storage::block::Status::Ok) return Status::BlockDeviceError;

    Geometry geometry{};
    Status status = calculate_geometry(device, inode_count, &geometry);
    if (status != Status::Ok) return status;

    status = initialize_inode_table(device, geometry);
    if (status != Status::Ok) return status;
    status = initialize_bitmap(device, geometry);
    if (status != Status::Ok) return status;

    uint8_t superblock[kSectorSize]{};
    encode_superblock(geometry, superblock);
    status = write_one(device, kSecondarySuperblock, superblock);
    if (status != Status::Ok) return status;
    status = block_status(storage::block::flush(device));
    if (status != Status::Ok) return status;
    status = write_one(device, kPrimarySuperblock, superblock);
    if (status != Status::Ok) return status;
    return block_status(storage::block::flush(device));
}

Status mount(FileSystem* output, const storage::block::Device* device) {
    if (output == nullptr || device == nullptr) return Status::InvalidArgument;
    if (device->sector_size != SUPPORTED_SECTOR_SIZE) return Status::UnsupportedSectorSize;
    if (storage::block::validate(device) != storage::block::Status::Ok) {
        return Status::BlockDeviceError;
    }

    Geometry primary{};
    Geometry secondary{};
    const Status primary_status = read_superblock_copy(device, kPrimarySuperblock, &primary);
    const Status secondary_status = read_superblock_copy(device, kSecondarySuperblock, &secondary);
    const bool primary_valid = primary_status == Status::Ok;
    const bool secondary_valid = secondary_status == Status::Ok;
    if (!primary_valid && !secondary_valid) {
        if (primary_status == Status::BlockDeviceError || secondary_status == Status::BlockDeviceError) {
            return Status::BlockDeviceError;
        }
        return Status::InvalidSuperblock;
    }

    Geometry selected{};
    if (primary_valid && secondary_valid) {
        if (primary.generation == secondary.generation && !geometry_equal(primary, secondary)) {
            return Status::CorruptSuperblock;
        }
        selected = primary.generation >= secondary.generation ? primary : secondary;
    } else {
        selected = primary_valid ? primary : secondary;
    }

    FileSystem candidate{};
    candidate.device = device;
    candidate.geometry = selected;
    candidate.mounted = true;
    Inode root{};
    const Status root_status = read_inode(&candidate, selected.root_inode, &root);
    if (root_status != Status::Ok || root.type != InodeType::Directory) {
        return Status::InvalidRootInode;
    }
    *output = candidate;
    return Status::Ok;
}

bool is_mounted(const FileSystem* filesystem) {
    return filesystem != nullptr && filesystem->mounted && filesystem->device != nullptr;
}

Status get_geometry(const FileSystem* filesystem, Geometry* output) {
    if (filesystem == nullptr || output == nullptr) return Status::InvalidArgument;
    if (!is_mounted(filesystem)) return Status::NotMounted;
    *output = filesystem->geometry;
    return Status::Ok;
}

Status read_inode(FileSystem* filesystem, uint64_t inode_id, Inode* output) {
    if (filesystem == nullptr || output == nullptr || inode_id == 0U) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;
    if (inode_id > static_cast<uint64_t>(filesystem->geometry.inode_count)) {
        return Status::InvalidArgument;
    }

    const uint64_t zero_based = inode_id - 1U;
    uint64_t byte_offset = 0U;
    if (!multiply_u64(zero_based, INODE_SIZE, &byte_offset)) {
        return Status::ArithmeticOverflow;
    }
    const uint64_t relative_block = byte_offset / kSectorSize;
    const size_t offset_in_block = static_cast<size_t>(byte_offset % kSectorSize);
    if (offset_in_block + INODE_SIZE > kSectorSize ||
        relative_block >= filesystem->geometry.inode_table_blocks) {
        return Status::InvalidGeometry;
    }

    uint8_t sector[kSectorSize]{};
    const Status status = read_one(
        filesystem->device,
        filesystem->geometry.inode_table_start + relative_block,
        sector);
    if (status != Status::Ok) return status;
    return decode_inode(sector + offset_in_block, inode_id, output);
}

Status allocate_blocks(
    FileSystem* filesystem, uint64_t block_count, uint64_t* out_first_block) {
    if (filesystem == nullptr || out_first_block == nullptr || block_count == 0U) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;
    const uint64_t available = filesystem->geometry.total_blocks -
        filesystem->geometry.data_start;
    if (block_count > available) return Status::NoSpace;

    uint64_t first = 0U;
    Status status = find_free_extent(filesystem, block_count, &first);
    if (status != Status::Ok) return status;
    status = publish_extent_allocation(filesystem, first, block_count);
    if (status != Status::Ok) return status;
    // Allocation becomes visible only after stale device contents have been
    // durably cleared. A failure here can leak the reserved bitmap range,
    // but the failed range is never returned to a caller or double-issued.
    status = zero_extent(filesystem, first, block_count);
    if (status != Status::Ok) return status;
    *out_first_block = first;
    return Status::Ok;
}

Status allocate_inode(
    FileSystem* filesystem, InodeType type, uint64_t* out_inode_id) {
    if (filesystem == nullptr || out_inode_id == nullptr ||
        (type != InodeType::Regular && type != InodeType::Directory)) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;
    for (uint64_t relative_block = 0U;
         relative_block < filesystem->geometry.inode_table_blocks;
         ++relative_block) {
        uint8_t sector[kSectorSize]{};
        const uint64_t table_block =
            filesystem->geometry.inode_table_start + relative_block;
        Status status = read_one(filesystem->device, table_block, sector);
        if (status != Status::Ok) return status;
        for (size_t offset = 0U; offset + INODE_SIZE <= kSectorSize;
             offset += INODE_SIZE) {
            const uint64_t inode_id =
                (relative_block * kSectorSize + offset) / INODE_SIZE + 1U;
            if (inode_id == ROOT_INODE) continue;
            if (inode_id > filesystem->geometry.inode_count) return Status::NoSpace;
            uint32_t generation = 0U;
            if (!decode_free_inode(
                    sector + offset, inode_id, &generation)) continue;
            Inode inode{};
            inode.id = inode_id;
            inode.type = type;
            inode.flags = 0U;
            inode.size = 0U;
            inode.extent_start = 0U;
            inode.extent_blocks = 0U;
            inode.link_count = 1U;
            inode.generation = generation;
            inode.revision = 1U;
            encode_inode(inode, sector + offset);
            status = write_one(filesystem->device, table_block, sector);
            if (status != Status::Ok) return status;
            status = block_status(storage::block::flush(filesystem->device));
            if (status != Status::Ok) return status;
            *out_inode_id = inode_id;
            return Status::Ok;
        }
    }
    return Status::NoSpace;
}

Status update_inode(FileSystem* filesystem, Inode* inode) {
    if (filesystem == nullptr || inode == nullptr) return Status::InvalidArgument;
    if (!is_mounted(filesystem)) return Status::NotMounted;

    Inode current{};
    Status status = read_inode(filesystem, inode->id, &current);
    if (status != Status::Ok) return status;
    if (current.generation != inode->generation ||
        current.revision != inode->revision || current.type != inode->type) {
        return Status::StaleInode;
    }
    if (current.revision == UINT32_MAX) return Status::ArithmeticOverflow;
    status = validate_inode_extent(filesystem, *inode);
    if (status != Status::Ok) return status;

    Inode candidate = *inode;
    candidate.revision = current.revision + 1U;
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
    if (current.generation != inode->generation || current.revision != inode->revision ||
        current.type != inode->type || current.extent_start != inode->extent_start ||
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

Status resize_inode(
    FileSystem* filesystem, Inode* inode, uint64_t new_size) {
    if (filesystem == nullptr || inode == nullptr) return Status::InvalidArgument;
    if (!is_mounted(filesystem)) return Status::NotMounted;

    Inode current{};
    Status status = require_current_snapshot(filesystem, inode, &current);
    if (status != Status::Ok) return status;
    if (current.type != InodeType::Regular) return Status::InvalidArgument;
    status = validate_inode_extent(filesystem, current);
    if (status != Status::Ok) return status;
    if (new_size == current.size) return Status::Ok;

    const uint64_t required_blocks = divide_round_up(new_size, kSectorSize);
    const uint64_t available_blocks = filesystem->geometry.total_blocks -
        filesystem->geometry.data_start;
    if (required_blocks > available_blocks) return Status::NoSpace;

    if (required_blocks == current.extent_blocks) {
        if (new_size > current.size) {
            status = zero_extent_range(
                filesystem, current.extent_start, current.extent_blocks,
                current.size, new_size - current.size);
        }
        if (status != Status::Ok) return status;
        Inode candidate = current;
        candidate.size = new_size;
        status = update_inode(filesystem, &candidate);
        if (status == Status::Ok) *inode = candidate;
        return status;
    }

    if (required_blocks < current.extent_blocks) {
        Inode candidate = current;
        candidate.size = new_size;
        candidate.extent_blocks = required_blocks;
        if (required_blocks == 0U) candidate.extent_start = 0U;
        status = update_inode(filesystem, &candidate);
        if (status != Status::Ok) return status;
        *inode = candidate;

        uint64_t released_start = 0U;
        if (!add_u64(current.extent_start, required_blocks, &released_start)) {
            return Status::ArithmeticOverflow;
        }
        return release_extent(
            filesystem, released_start,
            current.extent_blocks - required_blocks);
    }

    if (current.extent_blocks != 0U) {
        uint64_t suffix_start = 0U;
        if (!add_u64(
                current.extent_start, current.extent_blocks, &suffix_start)) {
            return Status::ArithmeticOverflow;
        }
        const uint64_t suffix_blocks = required_blocks - current.extent_blocks;
        status = extent_is_free(filesystem, suffix_start, suffix_blocks);
        if (status == Status::Ok) {
            status = publish_extent_allocation(
                filesystem, suffix_start, suffix_blocks);
            if (status != Status::Ok) return status;
            status = zero_extent(filesystem, suffix_start, suffix_blocks);
            if (status != Status::Ok) return status;
            status = zero_extent_range(
                filesystem, current.extent_start, required_blocks,
                current.size, new_size - current.size);
            if (status != Status::Ok) return status;

            Inode candidate = current;
            candidate.size = new_size;
            candidate.extent_blocks = required_blocks;
            status = update_inode(filesystem, &candidate);
            if (status == Status::Ok) *inode = candidate;
            return status;
        }
        if (status != Status::NoSpace) return status;
    }

    uint64_t new_extent = 0U;
    status = allocate_blocks(filesystem, required_blocks, &new_extent);
    if (status != Status::Ok) return status;

    uint64_t copied = 0U;
    while (copied < current.size) {
        uint8_t chunk[kSectorSize]{};
        const uint64_t remaining = current.size - copied;
        const size_t wanted = remaining < kSectorSize
            ? static_cast<size_t>(remaining) : kSectorSize;
        size_t read = 0U;
        status = read_inode_data(
            filesystem, &current, copied, chunk, wanted, &read);
        if (status != Status::Ok) return status;
        if (read != wanted) return Status::InvalidExtent;
        status = write_extent_data(
            filesystem, new_extent, required_blocks, copied, chunk, read);
        if (status != Status::Ok) return status;
        copied += static_cast<uint64_t>(read);
    }

    Inode candidate = current;
    candidate.size = new_size;
    candidate.extent_start = new_extent;
    candidate.extent_blocks = required_blocks;
    status = update_inode(filesystem, &candidate);
    if (status != Status::Ok) return status;
    *inode = candidate;
    if (current.extent_blocks == 0U) return Status::Ok;
    return release_extent(
        filesystem, current.extent_start, current.extent_blocks);
}

Status directory_entry_at(
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
    if (candidate.extent_start != current.extent_start &&
        current.extent_blocks != 0U) {
        return release_extent(
            filesystem, current.extent_start, current.extent_blocks);
    }
    return Status::Ok;
}

Status directory_remove(
    FileSystem* filesystem, Inode* directory, const char* name) {
    if (filesystem == nullptr || directory == nullptr) {
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
    if ((current.size % DIRECTORY_ENTRY_SIZE) != 0U) {
        return Status::CorruptDirectory;
    }

    const uint64_t entry_count = current.size / DIRECTORY_ENTRY_SIZE;
    uint64_t removed_index = UINT64_MAX;
    DirectoryEntry removed_entry{};
    for (uint64_t index = 0U; index < entry_count; ++index) {
        DirectoryEntry entry{};
        status = directory_entry_at(filesystem, &current, index, &entry);
        if (status != Status::Ok) return status;
        if (same_name(entry, name, name_length)) {
            removed_index = index;
            removed_entry = entry;
            break;
        }
    }
    if (removed_index == UINT64_MAX) return Status::NotFound;

    Inode child{};
    status = read_inode(filesystem, removed_entry.inode_id, &child);
    if (status != Status::Ok || child.generation != removed_entry.inode_generation ||
        child.type != removed_entry.type) return Status::CorruptDirectory;
    if (child.type == InodeType::Directory && child.size != 0U) {
        return Status::DirectoryNotEmpty;
    }

    const uint64_t new_size = current.size - DIRECTORY_ENTRY_SIZE;
    const uint64_t new_blocks = divide_round_up(new_size, kSectorSize);
    uint64_t new_extent = 0U;
    if (new_blocks != 0U) {
        status = allocate_blocks(filesystem, new_blocks, &new_extent);
        if (status != Status::Ok) return status;
        uint64_t output_index = 0U;
        for (uint64_t index = 0U; index < entry_count; ++index) {
            if (index == removed_index) continue;
            DirectoryEntry verified{};
            status = directory_entry_at(filesystem, &current, index, &verified);
            if (status != Status::Ok) return status;
            static_cast<void>(verified);

            uint64_t source_offset = 0U;
            uint64_t destination_offset = 0U;
            if (!multiply_u64(index, DIRECTORY_ENTRY_SIZE, &source_offset) ||
                !multiply_u64(
                    output_index, DIRECTORY_ENTRY_SIZE, &destination_offset)) {
                return Status::ArithmeticOverflow;
            }
            uint8_t record[DIRECTORY_ENTRY_SIZE]{};
            size_t read = 0U;
            status = read_inode_data(
                filesystem, &current, source_offset,
                record, sizeof(record), &read);
            if (status != Status::Ok) return status;
            if (read != sizeof(record)) return Status::CorruptDirectory;
            status = write_extent_data(
                filesystem, new_extent, new_blocks, destination_offset,
                record, sizeof(record));
            if (status != Status::Ok) return status;
            ++output_index;
        }
    }

    Inode candidate = current;
    candidate.size = new_size;
    candidate.extent_start = new_extent;
    candidate.extent_blocks = new_blocks;
    status = update_inode(filesystem, &candidate);
    if (status != Status::Ok) return status;
    *directory = candidate;

    status = retire_inode(filesystem, child);
    if (status != Status::Ok) return status;
    if (current.extent_blocks == 0U) return Status::Ok;
    return release_extent(
        filesystem, current.extent_start, current.extent_blocks);
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotMounted: return "not mounted";
        case Status::InvalidArgument: return "invalid argument";
        case Status::UnsupportedSectorSize: return "unsupported sector size";
        case Status::DeviceTooSmall: return "device too small";
        case Status::ArithmeticOverflow: return "arithmetic overflow";
        case Status::InvalidSuperblock: return "invalid superblock";
        case Status::CorruptSuperblock: return "corrupt superblock";
        case Status::InvalidGeometry: return "invalid filesystem geometry";
        case Status::InvalidRootInode: return "invalid root inode";
        case Status::InvalidExtent: return "invalid or unallocated KuroFS extent";
        case Status::StaleInode: return "stale KuroFS inode generation";
        case Status::NotFound: return "KuroFS entry not found";
        case Status::AlreadyExists: return "KuroFS entry already exists";
        case Status::NotDirectory: return "KuroFS inode is not a directory";
        case Status::DirectoryNotEmpty: return "KuroFS directory is not empty";
        case Status::NameTooLong: return "KuroFS name too long";
        case Status::CorruptDirectory: return "corrupt KuroFS directory";
        case Status::NoSpace: return "no free KuroFS space";
        case Status::BlockDeviceError: return "block device error";
    }
    return "unknown KuroFS status";
}

} // namespace fs::kurofs
