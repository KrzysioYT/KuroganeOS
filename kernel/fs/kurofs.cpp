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
    store_u32(bytes + kInodeChecksumOffset, crc32(bytes, kInodeChecksumOffset));
}

Status decode_inode(const uint8_t* bytes, uint64_t expected_id, Inode* output) {
    if (bytes == nullptr || output == nullptr) return Status::InvalidArgument;
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
    if (inode.id != expected_id ||
        (inode.type != InodeType::Regular && inode.type != InodeType::Directory) ||
        inode.link_count == 0U || inode.generation == 0U) {
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

bool all_zero(const uint8_t* bytes, size_t size) {
    if (bytes == nullptr) return false;
    for (size_t index = 0U; index < size; ++index) {
        if (bytes[index] != 0U) return false;
    }
    return true;
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
            if (!all_zero(sector + offset, INODE_SIZE)) continue;
            Inode inode{};
            inode.id = inode_id;
            inode.type = type;
            inode.flags = 0U;
            inode.size = 0U;
            inode.extent_start = 0U;
            inode.extent_blocks = 0U;
            inode.link_count = 1U;
            inode.generation = 1U;
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
        case Status::NoSpace: return "no free KuroFS space";
        case Status::BlockDeviceError: return "block device error";
    }
    return "unknown KuroFS status";
}

} // namespace fs::kurofs
