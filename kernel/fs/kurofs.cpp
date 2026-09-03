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
constexpr uint64_t kSupportedFeatures =
    FEATURE_MOVE_INTENT | FEATURE_INODE_OWNERSHIP;
constexpr size_t kMoveIntentOffset = 96U;
constexpr uint32_t kMoveIntentVersion = 1U;
constexpr uint32_t kMoveIntentSize = 160U;
constexpr uint8_t kMoveIntentMagic[8] = {
    'K', 'U', 'M', 'O', 'V', 'E', '1', 0U};

struct MoveParent {
    uint64_t id;
    uint32_t generation;
    uint32_t revision_before;
    uint64_t size_before;
    uint64_t extent_start_before;
    uint64_t extent_blocks_before;
    uint64_t size_after;
    uint64_t extent_start_after;
    uint64_t extent_blocks_after;
};

struct MoveIntent {
    bool active;
    uint64_t child_id;
    uint32_t child_generation;
    InodeType child_type;
    MoveParent source;
    MoveParent destination;
};

struct SuperblockState {
    Geometry geometry;
    MoveIntent move;
};

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

bool valid_inode_ownership_flags(uint32_t flags) {
    return flags == 0U || flags == INODE_FLAG_PENDING ||
        flags == INODE_FLAG_ORPHAN;
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

bool move_parent_equal(const MoveParent& left, const MoveParent& right) {
    return left.id == right.id && left.generation == right.generation &&
        left.revision_before == right.revision_before &&
        left.size_before == right.size_before &&
        left.extent_start_before == right.extent_start_before &&
        left.extent_blocks_before == right.extent_blocks_before &&
        left.size_after == right.size_after &&
        left.extent_start_after == right.extent_start_after &&
        left.extent_blocks_after == right.extent_blocks_after;
}

bool move_intent_equal(const MoveIntent& left, const MoveIntent& right) {
    return left.active == right.active &&
        (!left.active ||
         (left.child_id == right.child_id &&
          left.child_generation == right.child_generation &&
          left.child_type == right.child_type &&
          move_parent_equal(left.source, right.source) &&
          move_parent_equal(left.destination, right.destination)));
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
    output->feature_flags = kSupportedFeatures;
    return Status::Ok;
}

void encode_move_parent(const MoveParent& parent, uint8_t* bytes) {
    store_u64(bytes + 0U, parent.id);
    store_u32(bytes + 8U, parent.generation);
    store_u32(bytes + 12U, parent.revision_before);
    store_u64(bytes + 16U, parent.size_before);
    store_u64(bytes + 24U, parent.extent_start_before);
    store_u64(bytes + 32U, parent.extent_blocks_before);
    store_u64(bytes + 40U, parent.size_after);
    store_u64(bytes + 48U, parent.extent_start_after);
    store_u64(bytes + 56U, parent.extent_blocks_after);
}

MoveParent decode_move_parent(const uint8_t* bytes) {
    MoveParent parent{};
    parent.id = load_u64(bytes + 0U);
    parent.generation = load_u32(bytes + 8U);
    parent.revision_before = load_u32(bytes + 12U);
    parent.size_before = load_u64(bytes + 16U);
    parent.extent_start_before = load_u64(bytes + 24U);
    parent.extent_blocks_before = load_u64(bytes + 32U);
    parent.size_after = load_u64(bytes + 40U);
    parent.extent_start_after = load_u64(bytes + 48U);
    parent.extent_blocks_after = load_u64(bytes + 56U);
    return parent;
}

void encode_superblock(
    const Geometry& geometry,
    const MoveIntent* move,
    uint8_t* sector) {
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
    if (move != nullptr && move->active) {
        uint8_t* const record = sector + kMoveIntentOffset;
        for (size_t index = 0U; index < sizeof(kMoveIntentMagic); ++index) {
            record[index] = kMoveIntentMagic[index];
        }
        store_u32(record + 8U, kMoveIntentVersion);
        store_u32(record + 12U, kMoveIntentSize);
        store_u64(record + 16U, move->child_id);
        store_u32(record + 24U, move->child_generation);
        store_u32(record + 28U, static_cast<uint32_t>(move->child_type));
        encode_move_parent(move->source, record + 32U);
        encode_move_parent(move->destination, record + 96U);
    }
    store_u32(sector + kSuperblockChecksumOffset, crc32(sector, kSuperblockChecksumOffset));
}

Status validate_geometry(const Geometry& geometry, const storage::block::Device* device) {
    if (device == nullptr || geometry.sector_size != SUPPORTED_SECTOR_SIZE ||
        geometry.sector_size != device->sector_size ||
        geometry.total_blocks != device->sector_count ||
        geometry.inode_count == 0U || geometry.inode_size != INODE_SIZE ||
        geometry.inode_table_start != kInodeTableStart ||
        geometry.root_inode != ROOT_INODE || geometry.generation == 0U ||
        (geometry.feature_flags & ~kSupportedFeatures) != 0U) {
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

bool geometry_extent_valid(
    const Geometry& geometry,
    uint64_t size,
    uint64_t first_block,
    uint64_t block_count) {
    if (block_count == 0U) return size == 0U && first_block == 0U;
    uint64_t end_block = 0U;
    uint64_t capacity = 0U;
    return add_u64(first_block, block_count, &end_block) &&
        multiply_u64(block_count, kSectorSize, &capacity) &&
        first_block >= geometry.data_start &&
        end_block <= geometry.total_blocks && size <= capacity;
}

bool move_parent_valid(
    const Geometry& geometry,
    const MoveParent& parent) {
    return parent.id != 0U &&
        parent.id <= static_cast<uint64_t>(geometry.inode_count) &&
        parent.generation != 0U && parent.revision_before != 0U &&
        parent.revision_before != UINT32_MAX &&
        (parent.size_before % DIRECTORY_ENTRY_SIZE) == 0U &&
        (parent.size_after % DIRECTORY_ENTRY_SIZE) == 0U &&
        geometry_extent_valid(
            geometry, parent.size_before,
            parent.extent_start_before, parent.extent_blocks_before) &&
        geometry_extent_valid(
            geometry, parent.size_after,
            parent.extent_start_after, parent.extent_blocks_after);
}

Status decode_move_intent(
    const uint8_t* sector,
    const Geometry& geometry,
    MoveIntent* output) {
    if (sector == nullptr || output == nullptr) return Status::InvalidArgument;
    const uint8_t* const record = sector + kMoveIntentOffset;
    if (all_zero(record, kMoveIntentSize)) {
        *output = {};
        return Status::Ok;
    }
    for (size_t index = 0U; index < sizeof(kMoveIntentMagic); ++index) {
        if (record[index] != kMoveIntentMagic[index]) {
            return Status::CorruptSuperblock;
        }
    }
    if ((geometry.feature_flags & FEATURE_MOVE_INTENT) == 0U ||
        load_u32(record + 8U) != kMoveIntentVersion ||
        load_u32(record + 12U) != kMoveIntentSize) {
        return Status::CorruptSuperblock;
    }

    MoveIntent move{};
    move.active = true;
    move.child_id = load_u64(record + 16U);
    move.child_generation = load_u32(record + 24U);
    move.child_type = static_cast<InodeType>(load_u32(record + 28U));
    move.source = decode_move_parent(record + 32U);
    move.destination = decode_move_parent(record + 96U);
    uint64_t expected_source_before = 0U;
    uint64_t expected_destination_after = 0U;
    if (move.child_id == 0U ||
        move.child_id > static_cast<uint64_t>(geometry.inode_count) ||
        move.child_generation == 0U ||
        (move.child_type != InodeType::Regular &&
         move.child_type != InodeType::Directory) ||
        !move_parent_valid(geometry, move.source) ||
        !move_parent_valid(geometry, move.destination) ||
        move.source.id == move.destination.id ||
        !add_u64(move.source.size_after, DIRECTORY_ENTRY_SIZE,
                 &expected_source_before) ||
        expected_source_before != move.source.size_before ||
        !add_u64(move.destination.size_before, DIRECTORY_ENTRY_SIZE,
                 &expected_destination_after) ||
        expected_destination_after != move.destination.size_after) {
        return Status::CorruptSuperblock;
    }
    *output = move;
    return Status::Ok;
}

Status decode_superblock(
    const uint8_t* sector,
    const storage::block::Device* device,
    SuperblockState* output) {
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
    MoveIntent move{};
    const Status move_status = decode_move_intent(sector, geometry, &move);
    if (move_status != Status::Ok) return move_status;
    output->geometry = geometry;
    output->move = move;
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
    SuperblockState* output) {
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

bool inode_extents_overlap(const Inode& left, const Inode& right) {
    if (left.extent_blocks == 0U || right.extent_blocks == 0U) return false;
    uint64_t left_end = 0U;
    uint64_t right_end = 0U;
    if (!add_u64(left.extent_start, left.extent_blocks, &left_end) ||
        !add_u64(right.extent_start, right.extent_blocks, &right_end)) {
        return true;
    }
    return left.extent_start < right_end && right.extent_start < left_end;
}

Status prepare_directory_without_entry(
    FileSystem* filesystem,
    const Inode& current,
    uint64_t removed_index,
    Inode* output) {
    if (filesystem == nullptr || output == nullptr ||
        current.type != InodeType::Directory ||
        (current.size % DIRECTORY_ENTRY_SIZE) != 0U) {
        return Status::InvalidArgument;
    }
    const uint64_t entry_count = current.size / DIRECTORY_ENTRY_SIZE;
    if (removed_index >= entry_count) return Status::NotFound;
    const uint64_t new_size = current.size - DIRECTORY_ENTRY_SIZE;
    const uint64_t new_blocks = divide_round_up(new_size, kSectorSize);
    uint64_t new_extent = 0U;
    if (new_blocks != 0U) {
        Status status = allocate_blocks(filesystem, new_blocks, &new_extent);
        if (status != Status::Ok) return status;
        uint64_t output_index = 0U;
        for (uint64_t index = 0U; index < entry_count; ++index) {
            if (index == removed_index) continue;
            DirectoryEntry verified{};
            status = directory_entry_at(filesystem, &current, index, &verified);
            if (status != Status::Ok) {
                const Status cleanup = release_extent(
                    filesystem, new_extent, new_blocks);
                return cleanup == Status::Ok ? status : cleanup;
            }
            static_cast<void>(verified);
            uint64_t source_offset = 0U;
            uint64_t destination_offset = 0U;
            if (!multiply_u64(index, DIRECTORY_ENTRY_SIZE, &source_offset) ||
                !multiply_u64(
                    output_index, DIRECTORY_ENTRY_SIZE, &destination_offset)) {
                const Status cleanup = release_extent(
                    filesystem, new_extent, new_blocks);
                return cleanup == Status::Ok
                    ? Status::ArithmeticOverflow : cleanup;
            }
            uint8_t record[DIRECTORY_ENTRY_SIZE]{};
            size_t read = 0U;
            status = read_inode_data(
                filesystem, &current, source_offset,
                record, sizeof(record), &read);
            if (status == Status::Ok && read != sizeof(record)) {
                status = Status::CorruptDirectory;
            }
            if (status == Status::Ok) {
                status = write_extent_data(
                    filesystem, new_extent, new_blocks,
                    destination_offset, record, sizeof(record));
            }
            if (status != Status::Ok) {
                const Status cleanup = release_extent(
                    filesystem, new_extent, new_blocks);
                return cleanup == Status::Ok ? status : cleanup;
            }
            ++output_index;
        }
    }
    Inode candidate = current;
    candidate.size = new_size;
    candidate.extent_start = new_extent;
    candidate.extent_blocks = new_blocks;
    *output = candidate;
    return Status::Ok;
}

Status prepare_directory_with_entry(
    FileSystem* filesystem,
    const Inode& current,
    const char* name,
    size_t name_length,
    const Inode& child,
    Inode* output) {
    if (filesystem == nullptr || name == nullptr || output == nullptr ||
        current.type != InodeType::Directory ||
        (current.size % DIRECTORY_ENTRY_SIZE) != 0U) {
        return Status::InvalidArgument;
    }
    uint64_t new_size = 0U;
    if (!add_u64(current.size, DIRECTORY_ENTRY_SIZE, &new_size)) {
        return Status::ArithmeticOverflow;
    }
    const uint64_t new_blocks = divide_round_up(new_size, kSectorSize);
    uint64_t new_extent = 0U;
    Status status = allocate_blocks(filesystem, new_blocks, &new_extent);
    if (status != Status::Ok) return status;
    const uint64_t entry_count = current.size / DIRECTORY_ENTRY_SIZE;
    for (uint64_t index = 0U; index < entry_count; ++index) {
        DirectoryEntry verified{};
        status = directory_entry_at(filesystem, &current, index, &verified);
        if (status != Status::Ok) break;
        static_cast<void>(verified);
        uint64_t offset = 0U;
        if (!multiply_u64(index, DIRECTORY_ENTRY_SIZE, &offset)) {
            status = Status::ArithmeticOverflow;
            break;
        }
        uint8_t record[DIRECTORY_ENTRY_SIZE]{};
        size_t read = 0U;
        status = read_inode_data(
            filesystem, &current, offset, record, sizeof(record), &read);
        if (status == Status::Ok && read != sizeof(record)) {
            status = Status::CorruptDirectory;
        }
        if (status == Status::Ok) {
            status = write_extent_data(
                filesystem, new_extent, new_blocks,
                offset, record, sizeof(record));
        }
        if (status != Status::Ok) break;
    }
    if (status == Status::Ok) {
        uint8_t record[DIRECTORY_ENTRY_SIZE]{};
        encode_directory_entry(name, name_length, child, record);
        status = write_extent_data(
            filesystem, new_extent, new_blocks,
            current.size, record, sizeof(record));
    }
    if (status != Status::Ok) {
        const Status cleanup = release_extent(
            filesystem, new_extent, new_blocks);
        return cleanup == Status::Ok ? status : cleanup;
    }
    Inode candidate = current;
    candidate.size = new_size;
    candidate.extent_start = new_extent;
    candidate.extent_blocks = new_blocks;
    *output = candidate;
    return Status::Ok;
}

Status find_unique_parent(
    FileSystem* filesystem,
    uint64_t child_inode_id,
    uint64_t* out_parent_id) {
    if (filesystem == nullptr || out_parent_id == nullptr ||
        child_inode_id == ROOT_INODE) {
        return Status::InvalidArgument;
    }
    uint64_t parent_id = 0U;
    uint64_t parent_references = 0U;
    for (uint64_t inode_id = ROOT_INODE;
         inode_id <= static_cast<uint64_t>(filesystem->geometry.inode_count);
         ++inode_id) {
        Inode candidate{};
        Status status = read_inode(filesystem, inode_id, &candidate);
        if (status == Status::NotFound) continue;
        if (status != Status::Ok) return status;
        if (candidate.type != InodeType::Directory) continue;
        if ((candidate.size % DIRECTORY_ENTRY_SIZE) != 0U) {
            return Status::CorruptDirectory;
        }
        const uint64_t entry_count = candidate.size / DIRECTORY_ENTRY_SIZE;
        for (uint64_t index = 0U; index < entry_count; ++index) {
            DirectoryEntry entry{};
            status = directory_entry_at(
                filesystem, &candidate, index, &entry);
            if (status != Status::Ok) return status;
            if (entry.inode_id != child_inode_id) continue;
            ++parent_references;
            if (parent_references > 1U) {
                return Status::CorruptDirectory;
            }
            parent_id = candidate.id;
        }
    }
    if (parent_id == 0U) return Status::NotFound;
    *out_parent_id = parent_id;
    return Status::Ok;
}

Status inode_reaches_root(
    FileSystem* filesystem,
    uint64_t inode_id,
    bool* output) {
    if (filesystem == nullptr || output == nullptr || inode_id == 0U ||
        inode_id > static_cast<uint64_t>(filesystem->geometry.inode_count)) {
        return Status::InvalidArgument;
    }
    if (inode_id == ROOT_INODE) {
        *output = true;
        return Status::Ok;
    }

    uint64_t current = inode_id;
    for (uint64_t depth = 0U;
         depth < static_cast<uint64_t>(filesystem->geometry.inode_count);
         ++depth) {
        uint64_t parent = 0U;
        const Status status = find_unique_parent(filesystem, current, &parent);
        if (status == Status::NotFound) {
            *output = false;
            return Status::Ok;
        }
        if (status != Status::Ok) return status;
        if (parent == ROOT_INODE) {
            *output = true;
            return Status::Ok;
        }
        current = parent;
    }
    return Status::CorruptDirectory;
}

Status destination_creates_cycle(
    FileSystem* filesystem,
    uint64_t moved_directory_id,
    uint64_t destination_parent_id,
    bool* output) {
    if (filesystem == nullptr || output == nullptr ||
        moved_directory_id == ROOT_INODE) {
        return Status::InvalidArgument;
    }
    uint64_t current = destination_parent_id;
    for (uint64_t depth = 0U;
         depth <= static_cast<uint64_t>(filesystem->geometry.inode_count);
         ++depth) {
        if (current == moved_directory_id) {
            *output = true;
            return Status::Ok;
        }
        if (current == ROOT_INODE) {
            *output = false;
            return Status::Ok;
        }
        uint64_t parent = 0U;
        const Status status = find_unique_parent(filesystem, current, &parent);
        if (status == Status::NotFound) return Status::CorruptDirectory;
        if (status != Status::Ok) return status;
        current = parent;
    }
    return Status::CorruptDirectory;
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

Status write_superblock_pair(
    FileSystem* filesystem,
    const Geometry& geometry,
    const MoveIntent* move) {
    if (filesystem == nullptr || filesystem->device == nullptr) {
        return Status::InvalidArgument;
    }
    uint8_t superblock[kSectorSize]{};
    encode_superblock(geometry, move, superblock);
    Status status = write_one(filesystem->device, kSecondarySuperblock, superblock);
    if (status != Status::Ok) return status;
    status = block_status(storage::block::flush(filesystem->device));
    if (status != Status::Ok) return status;
    status = write_one(filesystem->device, kPrimarySuperblock, superblock);
    if (status != Status::Ok) return status;
    return block_status(storage::block::flush(filesystem->device));
}

Status ensure_feature(FileSystem* filesystem, uint64_t feature) {
    if (filesystem == nullptr || feature == 0U ||
        (feature & ~kSupportedFeatures) != 0U) {
        return Status::InvalidArgument;
    }
    if ((filesystem->geometry.feature_flags & feature) == feature) {
        return Status::Ok;
    }
    if (filesystem->geometry.generation == UINT64_MAX) {
        return Status::ArithmeticOverflow;
    }
    Geometry upgraded = filesystem->geometry;
    ++upgraded.generation;
    upgraded.feature_flags |= feature;
    const Status status = write_superblock_pair(filesystem, upgraded, nullptr);
    if (status != Status::Ok) {
        filesystem->mounted = false;
        return status;
    }
    filesystem->geometry = upgraded;
    return Status::Ok;
}

Status ensure_move_intent_feature(FileSystem* filesystem) {
    return ensure_feature(filesystem, FEATURE_MOVE_INTENT);
}

Status normalize_inode_ownership(FileSystem* filesystem) {
    Status status = ensure_feature(filesystem, FEATURE_INODE_OWNERSHIP);
    if (status != Status::Ok) return status;

    for (uint64_t inode_id = ROOT_INODE;
         inode_id <= static_cast<uint64_t>(filesystem->geometry.inode_count);
         ++inode_id) {
        Inode inode{};
        status = read_inode(filesystem, inode_id, &inode);
        if (status == Status::NotFound) continue;
        if (status != Status::Ok) return status;
        if (!valid_inode_ownership_flags(inode.flags)) {
            return Status::InvalidInodeMetadata;
        }
        if (inode_id == ROOT_INODE) {
            if (inode.flags != 0U) return Status::InvalidInodeMetadata;
            continue;
        }

        uint64_t parent = 0U;
        status = find_unique_parent(filesystem, inode_id, &parent);
        const bool attached = status == Status::Ok;
        if (!attached && status != Status::NotFound) return status;
        const uint32_t normalized_flags =
            attached ? 0U : INODE_FLAG_ORPHAN;
        if (inode.flags == normalized_flags) continue;
        inode.flags = normalized_flags;
        status = update_inode(filesystem, &inode);
        if (status != Status::Ok) return status;
    }
    return Status::Ok;
}

bool inode_matches_move_parent(
    const Inode& inode,
    const MoveParent& parent,
    bool after) {
    return inode.id == parent.id && inode.type == InodeType::Directory &&
        inode.generation == parent.generation && inode.link_count == 1U &&
        inode.revision == parent.revision_before + (after ? 1U : 0U) &&
        inode.size == (after ? parent.size_after : parent.size_before) &&
        inode.extent_start == (after
            ? parent.extent_start_after : parent.extent_start_before) &&
        inode.extent_blocks == (after
            ? parent.extent_blocks_after : parent.extent_blocks_before);
}

Inode make_move_parent_after(
    const Inode& before,
    const MoveParent& parent) {
    Inode after = before;
    after.size = parent.size_after;
    after.extent_start = parent.extent_start_after;
    after.extent_blocks = parent.extent_blocks_after;
    return after;
}

Status release_move_extent(
    FileSystem* filesystem,
    uint64_t first_block,
    uint64_t block_count) {
    return block_count == 0U
        ? Status::Ok
        : release_extent(filesystem, first_block, block_count);
}

Status recover_move_intent(
    FileSystem* filesystem,
    const MoveIntent& move) {
    if (filesystem == nullptr || !move.active) return Status::InvalidArgument;
    Inode child{};
    Status status = read_inode(filesystem, move.child_id, &child);
    if (status != Status::Ok || child.generation != move.child_generation ||
        child.type != move.child_type) {
        return Status::CorruptDirectory;
    }

    Inode source{};
    Inode destination{};
    status = read_inode(filesystem, move.source.id, &source);
    if (status != Status::Ok) return status;
    status = read_inode(filesystem, move.destination.id, &destination);
    if (status != Status::Ok) return status;
    const bool source_before = inode_matches_move_parent(source, move.source, false);
    const bool source_after = inode_matches_move_parent(source, move.source, true);
    const bool destination_before =
        inode_matches_move_parent(destination, move.destination, false);
    const bool destination_after =
        inode_matches_move_parent(destination, move.destination, true);

    if (source_before && destination_before) {
        status = write_superblock_pair(filesystem, filesystem->geometry, nullptr);
        if (status != Status::Ok) return status;
        status = release_move_extent(
            filesystem, move.source.extent_start_after,
            move.source.extent_blocks_after);
        if (status != Status::Ok) return status;
        return release_move_extent(
            filesystem, move.destination.extent_start_after,
            move.destination.extent_blocks_after);
    }
    if (source_after && destination_before) {
        return Status::CorruptDirectory;
    }
    if (!source_before && !source_after) return Status::CorruptDirectory;
    if (!destination_before && !destination_after) return Status::CorruptDirectory;

    if (source_before && destination_after) {
        Inode source_candidate = make_move_parent_after(source, move.source);
        status = update_inode(filesystem, &source_candidate);
        if (status != Status::Ok) return status;
        source = source_candidate;
    }
    if (!inode_matches_move_parent(source, move.source, true) ||
        !inode_matches_move_parent(destination, move.destination, true)) {
        return Status::CorruptDirectory;
    }

    status = write_superblock_pair(filesystem, filesystem->geometry, nullptr);
    if (status != Status::Ok) return status;
    status = release_move_extent(
        filesystem, move.source.extent_start_before,
        move.source.extent_blocks_before);
    if (status != Status::Ok) return status;
    return release_move_extent(
        filesystem, move.destination.extent_start_before,
        move.destination.extent_blocks_before);
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
    encode_superblock(geometry, nullptr, superblock);
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

    SuperblockState primary{};
    SuperblockState secondary{};
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
        if (primary.geometry.generation == secondary.geometry.generation &&
            !geometry_equal(primary.geometry, secondary.geometry)) {
            return Status::CorruptSuperblock;
        }
        selected = primary.geometry.generation >= secondary.geometry.generation
            ? primary.geometry : secondary.geometry;
    } else {
        selected = primary_valid ? primary.geometry : secondary.geometry;
    }

    MoveIntent selected_move{};
    const bool primary_current = primary_valid &&
        geometry_equal(primary.geometry, selected);
    const bool secondary_current = secondary_valid &&
        geometry_equal(secondary.geometry, selected);
    if (primary_current && secondary_current && primary.move.active &&
        secondary.move.active &&
        !move_intent_equal(primary.move, secondary.move)) {
        return Status::CorruptSuperblock;
    }
    if (primary_current && primary.move.active) selected_move = primary.move;
    if (secondary_current && secondary.move.active) selected_move = secondary.move;

    FileSystem candidate{};
    candidate.device = device;
    candidate.geometry = selected;
    candidate.mounted = true;
    if (selected_move.active) {
        const Status recovery = recover_move_intent(&candidate, selected_move);
        if (recovery != Status::Ok) return recovery;
    }
    const Status ownership_status = normalize_inode_ownership(&candidate);
    if (ownership_status != Status::Ok) return ownership_status;
    Inode root{};
    const Status root_status = read_inode(&candidate, selected.root_inode, &root);
    if (root_status != Status::Ok || root.type != InodeType::Directory) {
        return Status::InvalidRootInode;
    }
    const Status consistency = validate_consistency(&candidate);
    if (consistency != Status::Ok) return consistency;
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

Status inode_ownership(
    FileSystem* filesystem,
    uint64_t inode_id,
    InodeOwnership* output) {
    if (filesystem == nullptr || output == nullptr || inode_id == 0U ||
        inode_id > static_cast<uint64_t>(filesystem->geometry.inode_count)) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;

    uint64_t table_block = 0U;
    size_t offset = 0U;
    Status status = locate_inode(
        filesystem, inode_id, &table_block, &offset);
    if (status != Status::Ok) return status;
    uint8_t sector[kSectorSize]{};
    status = read_one(filesystem->device, table_block, sector);
    if (status != Status::Ok) return status;
    const uint8_t* const encoded = sector + offset;
    if (all_zero(encoded, INODE_SIZE)) {
        *output = InodeOwnership::Free;
        return Status::Ok;
    }
    uint32_t generation = 0U;
    if (decode_free_inode(encoded, inode_id, &generation)) {
        static_cast<void>(generation);
        *output = InodeOwnership::Tombstoned;
        return Status::Ok;
    }

    Inode inode{};
    status = decode_inode(encoded, inode_id, &inode);
    if (status != Status::Ok) return status;
    if (!valid_inode_ownership_flags(inode.flags)) {
        return Status::InvalidInodeMetadata;
    }
    if (inode_id == ROOT_INODE) {
        if (inode.flags != 0U) return Status::InvalidInodeMetadata;
        *output = InodeOwnership::Live;
        return Status::Ok;
    }
    if (inode.flags == INODE_FLAG_PENDING) {
        *output = InodeOwnership::Pending;
        return Status::Ok;
    }
    if (inode.flags == INODE_FLAG_ORPHAN) {
        *output = InodeOwnership::Orphan;
        return Status::Ok;
    }

    uint64_t parent = 0U;
    status = find_unique_parent(filesystem, inode_id, &parent);
    if (status == Status::NotFound) {
        *output = InodeOwnership::Orphan;
        return Status::Ok;
    }
    if (status != Status::Ok) return status;
    *output = InodeOwnership::Live;
    return Status::Ok;
}

Status scan_inode_ownership(
    FileSystem* filesystem,
    InodeOwnershipSummary* output) {
    if (filesystem == nullptr || output == nullptr) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;
    InodeOwnershipSummary summary{};
    for (uint64_t inode_id = ROOT_INODE;
         inode_id <= static_cast<uint64_t>(filesystem->geometry.inode_count);
         ++inode_id) {
        InodeOwnership ownership = InodeOwnership::Free;
        const Status status = inode_ownership(
            filesystem, inode_id, &ownership);
        if (status != Status::Ok) return status;
        switch (ownership) {
            case InodeOwnership::Free: ++summary.free; break;
            case InodeOwnership::Pending: ++summary.pending; break;
            case InodeOwnership::Live: ++summary.live; break;
            case InodeOwnership::Tombstoned: ++summary.tombstoned; break;
            case InodeOwnership::Orphan: ++summary.orphan; break;
        }
    }
    *output = summary;
    return Status::Ok;
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
    const Status feature_status = ensure_feature(
        filesystem, FEATURE_INODE_OWNERSHIP);
    if (feature_status != Status::Ok) return feature_status;
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
            inode.flags = INODE_FLAG_PENDING;
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
    if (!valid_inode_ownership_flags(inode->flags) ||
        (inode->flags != 0U &&
         (filesystem->geometry.feature_flags & FEATURE_INODE_OWNERSHIP) == 0U)) {
        return Status::InvalidInodeMetadata;
    }

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

Status write_inode_data(
    FileSystem* filesystem,
    Inode* inode,
    uint64_t offset,
    const void* source,
    size_t size) {
    if (filesystem == nullptr || inode == nullptr ||
        (source == nullptr && size != 0U)) return Status::InvalidArgument;
    if (!is_mounted(filesystem)) return Status::NotMounted;

    Inode current{};
    Status status = require_current_snapshot(filesystem, inode, &current);
    if (status != Status::Ok) return status;
    if (current.type != InodeType::Regular) return Status::InvalidArgument;
    status = validate_inode_extent(filesystem, current);
    if (status != Status::Ok || size == 0U) return status;

    uint64_t end = 0U;
    if (!add_u64(offset, static_cast<uint64_t>(size), &end)) {
        return Status::ArithmeticOverflow;
    }
    const uint64_t resulting_size = end > current.size ? end : current.size;
    const uint64_t replacement_blocks =
        divide_round_up(resulting_size, kSectorSize);
    const uint64_t available_blocks = filesystem->geometry.total_blocks -
        filesystem->geometry.data_start;
    if (replacement_blocks == 0U || replacement_blocks > available_blocks) {
        return Status::NoSpace;
    }

    uint64_t replacement_extent = 0U;
    status = allocate_blocks(
        filesystem, replacement_blocks, &replacement_extent);
    if (status != Status::Ok) return status;

    const auto abandon_replacement = [&](Status failure) {
        const Status cleanup = release_extent(
            filesystem, replacement_extent, replacement_blocks);
        return cleanup == Status::Ok ? failure : cleanup;
    };

    uint64_t copied = 0U;
    while (copied < current.size) {
        uint8_t chunk[kSectorSize]{};
        const uint64_t remaining = current.size - copied;
        const size_t wanted = remaining < kSectorSize
            ? static_cast<size_t>(remaining) : kSectorSize;
        size_t read = 0U;
        status = read_inode_data(
            filesystem, &current, copied, chunk, wanted, &read);
        if (status != Status::Ok) return abandon_replacement(status);
        if (read != wanted) {
            return abandon_replacement(Status::InvalidExtent);
        }
        status = write_extent_data(
            filesystem, replacement_extent, replacement_blocks,
            copied, chunk, read);
        if (status != Status::Ok) return abandon_replacement(status);
        copied += static_cast<uint64_t>(read);
    }
    status = write_extent_data(
        filesystem, replacement_extent, replacement_blocks,
        offset, source, size);
    if (status != Status::Ok) return abandon_replacement(status);

    Inode candidate = current;
    candidate.size = resulting_size;
    candidate.extent_start = replacement_extent;
    candidate.extent_blocks = replacement_blocks;
    status = update_inode(filesystem, &candidate);
    if (status != Status::Ok) return abandon_replacement(status);
    *inode = candidate;
    if (current.extent_blocks == 0U) return Status::Ok;
    return release_extent(
        filesystem, current.extent_start, current.extent_blocks);
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
    Status status = ensure_feature(filesystem, FEATURE_INODE_OWNERSHIP);
    if (status != Status::Ok) return status;
    size_t name_length = 0U;
    status = parse_directory_name(name, &name_length);
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
    if (child.id == ROOT_INODE ||
        !valid_inode_ownership_flags(child.flags)) {
        return Status::InvalidInodeMetadata;
    }
    uint64_t existing_parent = 0U;
    status = find_unique_parent(filesystem, child.id, &existing_parent);
    if (status == Status::Ok) return Status::AlreadyExists;
    if (status != Status::NotFound) return status;
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
    if (child.flags != 0U) {
        child.flags = 0U;
        status = update_inode(filesystem, &child);
        if (status != Status::Ok) return status;
    }
    if (candidate.extent_start != current.extent_start &&
        current.extent_blocks != 0U) {
        return release_extent(
            filesystem, current.extent_start, current.extent_blocks);
    }
    return Status::Ok;
}

Status directory_create(
    FileSystem* filesystem,
    Inode* directory,
    const char* name,
    InodeType type,
    Inode* out_child) {
    if (filesystem == nullptr || directory == nullptr || out_child == nullptr ||
        (type != InodeType::Regular && type != InodeType::Directory)) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;

    Inode parent{};
    Status status = require_current_snapshot(filesystem, directory, &parent);
    if (status != Status::Ok) return status;
    if (parent.type != InodeType::Directory) return Status::NotDirectory;
    DirectoryEntry existing{};
    status = directory_lookup(filesystem, &parent, name, &existing);
    if (status == Status::Ok) return Status::AlreadyExists;
    if (status != Status::NotFound) return status;

    uint64_t inode_id = 0U;
    status = allocate_inode(filesystem, type, &inode_id);
    if (status != Status::Ok) return status;
    Inode child{};
    status = read_inode(filesystem, inode_id, &child);
    if (status != Status::Ok) return status;

    status = directory_append(filesystem, directory, name, inode_id);
    if (status == Status::Ok) {
        return read_inode(filesystem, inode_id, out_child);
    }
    // directory_append updates the caller snapshot immediately after durable
    // namespace publication and can then report only reclamation failure. In
    // that case creation is already committed and must not retire its child.
    if (directory->revision != parent.revision) {
        Inode committed_child{};
        if (read_inode(filesystem, inode_id, &committed_child) == Status::Ok) {
            *out_child = committed_child;
        } else {
            *out_child = child;
        }
        return Status::Ok;
    }
    const Status retirement = retire_inode(filesystem, child);
    return retirement == Status::Ok ? status : retirement;
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

Status directory_rename(
    FileSystem* filesystem,
    Inode* directory,
    const char* old_name,
    const char* new_name) {
    if (filesystem == nullptr || directory == nullptr) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;
    size_t old_length = 0U;
    size_t new_length = 0U;
    Status status = parse_directory_name(old_name, &old_length);
    if (status != Status::Ok) return status;
    status = parse_directory_name(new_name, &new_length);
    if (status != Status::Ok) return status;

    Inode current{};
    status = require_current_snapshot(filesystem, directory, &current);
    if (status != Status::Ok) return status;
    if (current.type != InodeType::Directory) return Status::NotDirectory;
    if ((current.size % DIRECTORY_ENTRY_SIZE) != 0U) {
        return Status::CorruptDirectory;
    }

    const uint64_t entry_count = current.size / DIRECTORY_ENTRY_SIZE;
    uint64_t renamed_index = UINT64_MAX;
    DirectoryEntry renamed_entry{};
    bool destination_exists = false;
    for (uint64_t index = 0U; index < entry_count; ++index) {
        DirectoryEntry entry{};
        status = directory_entry_at(filesystem, &current, index, &entry);
        if (status != Status::Ok) return status;
        if (same_name(entry, old_name, old_length)) {
            renamed_index = index;
            renamed_entry = entry;
        }
        if (same_name(entry, new_name, new_length)) destination_exists = true;
    }
    if (renamed_index == UINT64_MAX) return Status::NotFound;
    if (old_length == new_length &&
        same_name(renamed_entry, new_name, new_length)) return Status::Ok;
    if (destination_exists) return Status::AlreadyExists;

    const uint64_t replacement_blocks =
        divide_round_up(current.size, kSectorSize);
    if (replacement_blocks == 0U) return Status::CorruptDirectory;
    uint64_t replacement_extent = 0U;
    status = allocate_blocks(
        filesystem, replacement_blocks, &replacement_extent);
    if (status != Status::Ok) return status;

    for (uint64_t index = 0U; index < entry_count; ++index) {
        uint64_t offset = 0U;
        if (!multiply_u64(index, DIRECTORY_ENTRY_SIZE, &offset)) {
            return Status::ArithmeticOverflow;
        }
        uint8_t record[DIRECTORY_ENTRY_SIZE]{};
        if (index == renamed_index) {
            Inode child{};
            status = read_inode(filesystem, renamed_entry.inode_id, &child);
            if (status != Status::Ok ||
                child.generation != renamed_entry.inode_generation ||
                child.type != renamed_entry.type) {
                return Status::CorruptDirectory;
            }
            encode_directory_entry(new_name, new_length, child, record);
        } else {
            size_t read = 0U;
            status = read_inode_data(
                filesystem, &current, offset,
                record, sizeof(record), &read);
            if (status != Status::Ok) return status;
            if (read != sizeof(record)) return Status::CorruptDirectory;
        }
        status = write_extent_data(
            filesystem, replacement_extent, replacement_blocks,
            offset, record, sizeof(record));
        if (status != Status::Ok) return status;
    }

    Inode candidate = current;
    candidate.extent_start = replacement_extent;
    candidate.extent_blocks = replacement_blocks;
    status = update_inode(filesystem, &candidate);
    if (status != Status::Ok) return status;
    *directory = candidate;
    return release_extent(
        filesystem, current.extent_start, current.extent_blocks);
}

Status directory_move(
    FileSystem* filesystem,
    Inode* source_directory,
    const char* source_name,
    Inode* destination_directory,
    const char* destination_name) {
    if (filesystem == nullptr || source_directory == nullptr ||
        destination_directory == nullptr) {
        return Status::InvalidArgument;
    }
    if (!is_mounted(filesystem)) return Status::NotMounted;
    size_t source_name_length = 0U;
    size_t destination_name_length = 0U;
    Status status = parse_directory_name(source_name, &source_name_length);
    if (status != Status::Ok) return status;
    status = parse_directory_name(destination_name, &destination_name_length);
    if (status != Status::Ok) return status;

    if (source_directory->id == destination_directory->id) {
        if (!same_inode_snapshot(*source_directory, *destination_directory)) {
            return Status::StaleInode;
        }
        status = directory_rename(
            filesystem, source_directory, source_name, destination_name);
        if (status == Status::Ok &&
            source_directory != destination_directory) {
            *destination_directory = *source_directory;
        }
        return status;
    }

    Inode source{};
    Inode destination{};
    status = require_current_snapshot(filesystem, source_directory, &source);
    if (status != Status::Ok) return status;
    status = require_current_snapshot(
        filesystem, destination_directory, &destination);
    if (status != Status::Ok) return status;
    if (source.type != InodeType::Directory ||
        destination.type != InodeType::Directory) {
        return Status::NotDirectory;
    }
    if ((source.size % DIRECTORY_ENTRY_SIZE) != 0U ||
        (destination.size % DIRECTORY_ENTRY_SIZE) != 0U) {
        return Status::CorruptDirectory;
    }
    if (source.revision == UINT32_MAX || destination.revision == UINT32_MAX) {
        return Status::ArithmeticOverflow;
    }

    const uint64_t source_entry_count = source.size / DIRECTORY_ENTRY_SIZE;
    uint64_t source_index = UINT64_MAX;
    DirectoryEntry moved_entry{};
    for (uint64_t index = 0U; index < source_entry_count; ++index) {
        DirectoryEntry entry{};
        status = directory_entry_at(filesystem, &source, index, &entry);
        if (status != Status::Ok) return status;
        if (same_name(entry, source_name, source_name_length)) {
            source_index = index;
            moved_entry = entry;
        }
    }
    if (source_index == UINT64_MAX) return Status::NotFound;

    const uint64_t destination_entry_count =
        destination.size / DIRECTORY_ENTRY_SIZE;
    for (uint64_t index = 0U; index < destination_entry_count; ++index) {
        DirectoryEntry entry{};
        status = directory_entry_at(filesystem, &destination, index, &entry);
        if (status != Status::Ok) return status;
        if (same_name(entry, destination_name, destination_name_length) ||
            entry.inode_id == moved_entry.inode_id) {
            return Status::AlreadyExists;
        }
    }

    Inode child{};
    status = read_inode(filesystem, moved_entry.inode_id, &child);
    if (status != Status::Ok ||
        child.generation != moved_entry.inode_generation ||
        child.type != moved_entry.type) {
        return Status::CorruptDirectory;
    }
    if (inode_extents_overlap(source, destination) ||
        inode_extents_overlap(source, child) ||
        inode_extents_overlap(destination, child)) {
        return Status::InvalidExtent;
    }
    if (child.type == InodeType::Directory) {
        bool cycle = false;
        status = destination_creates_cycle(
            filesystem, child.id, destination.id, &cycle);
        if (status != Status::Ok) return status;
        if (cycle) return Status::WouldCreateCycle;
    }

    status = ensure_move_intent_feature(filesystem);
    if (status != Status::Ok) return status;

    Inode source_after{};
    status = prepare_directory_without_entry(
        filesystem, source, source_index, &source_after);
    if (status != Status::Ok) return status;
    Inode destination_after{};
    status = prepare_directory_with_entry(
        filesystem, destination,
        destination_name, destination_name_length,
        child, &destination_after);
    if (status != Status::Ok) {
        const Status cleanup = release_move_extent(
            filesystem, source_after.extent_start,
            source_after.extent_blocks);
        return cleanup == Status::Ok ? status : cleanup;
    }

    MoveIntent move{};
    move.active = true;
    move.child_id = child.id;
    move.child_generation = child.generation;
    move.child_type = child.type;
    move.source = {
        source.id, source.generation, source.revision,
        source.size, source.extent_start, source.extent_blocks,
        source_after.size, source_after.extent_start,
        source_after.extent_blocks};
    move.destination = {
        destination.id, destination.generation, destination.revision,
        destination.size, destination.extent_start,
        destination.extent_blocks,
        destination_after.size, destination_after.extent_start,
        destination_after.extent_blocks};

    status = write_superblock_pair(filesystem, filesystem->geometry, &move);
    if (status != Status::Ok) {
        filesystem->mounted = false;
        return status;
    }
    status = update_inode(filesystem, &destination_after);
    if (status != Status::Ok) {
        filesystem->mounted = false;
        return status;
    }
    status = update_inode(filesystem, &source_after);
    if (status != Status::Ok) {
        filesystem->mounted = false;
        return status;
    }
    *source_directory = source_after;
    *destination_directory = destination_after;

    status = write_superblock_pair(filesystem, filesystem->geometry, nullptr);
    if (status != Status::Ok) {
        filesystem->mounted = false;
        return status;
    }
    status = release_move_extent(
        filesystem, source.extent_start, source.extent_blocks);
    if (status != Status::Ok) return status;
    return release_move_extent(
        filesystem, destination.extent_start, destination.extent_blocks);
}

Status validate_consistency(FileSystem* filesystem) {
    if (filesystem == nullptr) return Status::InvalidArgument;
    if (!is_mounted(filesystem)) return Status::NotMounted;

    for (uint64_t inode_id = ROOT_INODE;
         inode_id <= static_cast<uint64_t>(filesystem->geometry.inode_count);
         ++inode_id) {
        Inode inode{};
        Status status = read_inode(filesystem, inode_id, &inode);
        if (status == Status::NotFound) continue;
        if (status != Status::Ok) return status;
        status = validate_inode_extent(filesystem, inode);
        if (status != Status::Ok) return status;
        if (!valid_inode_ownership_flags(inode.flags) ||
            inode.link_count != 1U ||
            (inode.id == ROOT_INODE && inode.flags != 0U)) {
            return Status::InvalidInodeMetadata;
        }
        if (inode.type == InodeType::Directory) {
            if ((inode.size % DIRECTORY_ENTRY_SIZE) != 0U) {
                return Status::CorruptDirectory;
            }
            const uint64_t entry_count = inode.size / DIRECTORY_ENTRY_SIZE;
            for (uint64_t index = 0U; index < entry_count; ++index) {
                DirectoryEntry entry{};
                status = directory_entry_at(
                    filesystem, &inode, index, &entry);
                if (status != Status::Ok) return status;
                if (entry.inode_id == ROOT_INODE || entry.inode_id == inode.id) {
                    return Status::CorruptDirectory;
                }
                for (uint64_t previous_index = 0U;
                     previous_index < index;
                     ++previous_index) {
                    DirectoryEntry previous{};
                    status = directory_entry_at(
                        filesystem, &inode, previous_index, &previous);
                    if (status != Status::Ok) return status;
                    if (previous.inode_id == entry.inode_id ||
                        (previous.name_length == entry.name_length &&
                         same_name(previous, entry.name, entry.name_length))) {
                        return Status::CorruptDirectory;
                    }
                }
            }
        }
        if (inode.extent_blocks == 0U) continue;
        for (uint64_t other_id = inode_id + 1U;
             other_id <= static_cast<uint64_t>(filesystem->geometry.inode_count);
             ++other_id) {
            Inode other{};
            status = read_inode(filesystem, other_id, &other);
            if (status == Status::NotFound) continue;
            if (status != Status::Ok) return status;
            if (inode_extents_overlap(inode, other)) {
                return Status::OverlappingExtents;
            }
        }
    }

    for (uint64_t inode_id = ROOT_INODE + 1U;
         inode_id <= static_cast<uint64_t>(filesystem->geometry.inode_count);
         ++inode_id) {
        Inode inode{};
        Status status = read_inode(filesystem, inode_id, &inode);
        if (status == Status::NotFound) continue;
        if (status != Status::Ok) return status;
        uint64_t parent = 0U;
        status = find_unique_parent(filesystem, inode.id, &parent);
        const bool attached = status == Status::Ok;
        if (!attached && status != Status::NotFound) return status;
        if ((attached && inode.flags != 0U) ||
            (!attached && inode.flags != INODE_FLAG_ORPHAN)) {
            return Status::InvalidInodeMetadata;
        }
        if (inode.type == InodeType::Directory) {
            bool reaches_root = false;
            status = inode_reaches_root(
                filesystem, inode.id, &reaches_root);
            if (status != Status::Ok) return status;
            static_cast<void>(reaches_root);
        }
    }
    return Status::Ok;
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
        case Status::InvalidInodeMetadata: return "invalid KuroFS inode metadata";
        case Status::InvalidExtent: return "invalid or unallocated KuroFS extent";
        case Status::OverlappingExtents: return "overlapping live KuroFS extents";
        case Status::StaleInode: return "stale KuroFS inode generation";
        case Status::NotFound: return "KuroFS entry not found";
        case Status::AlreadyExists: return "KuroFS entry already exists";
        case Status::NotDirectory: return "KuroFS inode is not a directory";
        case Status::DirectoryNotEmpty: return "KuroFS directory is not empty";
        case Status::WouldCreateCycle: return "KuroFS move would create a directory cycle";
        case Status::NameTooLong: return "KuroFS name too long";
        case Status::CorruptDirectory: return "corrupt KuroFS directory";
        case Status::NoSpace: return "no free KuroFS space";
        case Status::BlockDeviceError: return "block device error";
    }
    return "unknown KuroFS status";
}

} // namespace fs::kurofs
