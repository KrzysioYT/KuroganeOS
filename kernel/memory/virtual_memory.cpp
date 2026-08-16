#include "virtual_memory.hpp"

namespace memory::virtual_memory {

namespace {

constexpr uint64_t ENTRY_PRESENT = UINT64_C(1) << 0;
constexpr uint64_t ENTRY_WRITABLE = UINT64_C(1) << 1;
constexpr uint64_t ENTRY_USER = UINT64_C(1) << 2;
constexpr uint64_t ENTRY_WRITE_THROUGH = UINT64_C(1) << 3;
constexpr uint64_t ENTRY_CACHE_DISABLE = UINT64_C(1) << 4;
constexpr uint64_t ENTRY_ACCESSED = UINT64_C(1) << 5;
constexpr uint64_t ENTRY_DIRTY = UINT64_C(1) << 6;
constexpr uint64_t ENTRY_HUGE_OR_PAT = UINT64_C(1) << 7;
constexpr uint64_t ENTRY_GLOBAL = UINT64_C(1) << 8;

// Bit 9 is ignored by x86 page translation and records that the child table was
// allocated by this module. It is meaningful only in non-leaf entries.
constexpr uint64_t ENTRY_MODULE_OWNED = UINT64_C(1) << 9;
constexpr uint64_t ENTRY_NO_EXECUTE = UINT64_C(1) << 63;

constexpr uint64_t ARCHITECTURAL_ADDRESS_MASK =
    UINT64_C(0x000FFFFFFFFFF000);
constexpr uint64_t PAGE_OFFSET_MASK = PAGE_SIZE - 1;
constexpr uint64_t VALID_MAP_FLAGS =
    static_cast<uint64_t>(MapFlags::Writable) |
    static_cast<uint64_t>(MapFlags::User) |
    static_cast<uint64_t>(MapFlags::WriteThrough) |
    static_cast<uint64_t>(MapFlags::CacheDisable) |
    static_cast<uint64_t>(MapFlags::Global) |
    static_cast<uint64_t>(MapFlags::NoExecute);

struct CreatedTable {
    uint64_t* parent_entry;
    uint64_t physical_address;
};

struct WalkPath {
    uint64_t* tables[4];
    uint64_t* parent_entries[3];
    uint64_t child_physical[3];
    uint64_t leaf_value;
    bool effective_writable;
    bool effective_user;
    bool effective_no_execute;
};

uint64_t load_entry(const uint64_t* entry) {
    return __atomic_load_n(entry, __ATOMIC_ACQUIRE);
}

void store_entry(uint64_t* entry, uint64_t value) {
    __atomic_store_n(entry, value, __ATOMIC_RELEASE);
}

bool canonical_address(uint64_t address) {
    const uint64_t upper = address >> 48;
    const bool sign = ((address >> 47) & UINT64_C(1)) != 0;
    return sign ? upper == UINT64_C(0xFFFF) : upper == 0;
}

Status validate_virtual_page(uint64_t address) {
    if (!canonical_address(address)) {
        return Status::NonCanonicalAddress;
    }
    if ((address & PAGE_OFFSET_MASK) != 0) {
        return Status::UnalignedAddress;
    }
    if (address > UINT64_MAX - PAGE_OFFSET_MASK) {
        return Status::AddressOverflow;
    }
    const uint64_t last_byte = address + PAGE_OFFSET_MASK;
    if (!canonical_address(last_byte)) {
        return Status::NonCanonicalAddress;
    }
    return Status::Ok;
}

Status validate_physical_page(
    const AddressSpace& address_space,
    uint64_t address) {
    if ((address & PAGE_OFFSET_MASK) != 0) {
        return Status::UnalignedAddress;
    }
    if (address > UINT64_MAX - PAGE_OFFSET_MASK) {
        return Status::AddressOverflow;
    }
    if ((address & ~address_space.physical_address_mask) != 0) {
        return Status::PhysicalAddressOutOfRange;
    }
    return Status::Ok;
}

bool valid_map_flags(MapFlags flags) {
    if ((static_cast<uint64_t>(flags) & ~VALID_MAP_FLAGS) != 0) {
        return false;
    }

    // Global translations are shared across address-space switches and must
    // never be used for pages accessible from ring 3.
    return !(has_flag(flags, MapFlags::User) &&
             has_flag(flags, MapFlags::Global));
}

uint64_t table_index(uint64_t virtual_address, size_t level) {
    static constexpr uint8_t shifts[4] = {39, 30, 21, 12};
    return (virtual_address >> shifts[level]) & UINT64_C(0x1FF);
}

Status table_for_physical(
    const AddressSpace& address_space,
    uint64_t physical_address,
    uint64_t** table) {
    if (!table) {
        return Status::InvalidArgument;
    }
    const Status address_status =
        validate_physical_page(address_space, physical_address);
    if (address_status != Status::Ok) {
        return Status::CorruptPageTable;
    }

    void* virtual_address = address_space.backend.physical_to_virtual(
        address_space.backend.context,
        physical_address);
    if (!virtual_address ||
        (reinterpret_cast<uintptr_t>(virtual_address) &
         (alignof(uint64_t) - 1)) != 0) {
        return Status::BackendFailure;
    }
    *table = static_cast<uint64_t*>(virtual_address);
    return Status::Ok;
}

Status child_table_from_entry(
    const AddressSpace& address_space,
    uint64_t entry,
    uint64_t* child_physical,
    uint64_t** child_table) {
    if ((entry & (ARCHITECTURAL_ADDRESS_MASK &
                  ~address_space.physical_address_mask)) != 0) {
        return Status::CorruptPageTable;
    }
    const uint64_t physical = entry & address_space.physical_address_mask;
    const Status status =
        table_for_physical(address_space, physical, child_table);
    if (status != Status::Ok) {
        return status;
    }
    *child_physical = physical;
    return Status::Ok;
}

void clear_table(uint64_t* table) {
    for (size_t index = 0; index < PAGE_TABLE_ENTRY_COUNT; ++index) {
        store_entry(&table[index], 0);
    }
}

bool table_empty(const uint64_t* table) {
    for (size_t index = 0; index < PAGE_TABLE_ENTRY_COUNT; ++index) {
        if (load_entry(&table[index]) != 0) {
            return false;
        }
    }
    return true;
}

uint64_t hardware_leaf_flags(MapFlags flags) {
    uint64_t result = ENTRY_PRESENT;
    if (has_flag(flags, MapFlags::Writable)) {
        result |= ENTRY_WRITABLE;
    }
    if (has_flag(flags, MapFlags::User)) {
        result |= ENTRY_USER;
    }
    if (has_flag(flags, MapFlags::WriteThrough)) {
        result |= ENTRY_WRITE_THROUGH;
    }
    if (has_flag(flags, MapFlags::CacheDisable)) {
        result |= ENTRY_CACHE_DISABLE;
    }
    if (has_flag(flags, MapFlags::Global)) {
        result |= ENTRY_GLOBAL;
    }
    if (has_flag(flags, MapFlags::NoExecute)) {
        result |= ENTRY_NO_EXECUTE;
    }
    return result;
}

void rollback_created_tables(
    const AddressSpace& address_space,
    CreatedTable* created,
    size_t created_count,
    uint64_t virtual_address) {
    if (created_count == 0) {
        return;
    }

    // First make every published table unreachable. Invalidation must happen
    // after all page-table writes and before any physical page is recycled.
    for (size_t index = created_count; index != 0; --index) {
        CreatedTable& table = created[index - 1];
        store_entry(table.parent_entry, 0);
    }
    address_space.backend.invalidate_page(
        address_space.backend.context,
        virtual_address);
    for (size_t index = created_count; index != 0; --index) {
        CreatedTable& table = created[index - 1];
        address_space.backend.free_table(
            address_space.backend.context,
            table.physical_address);
    }
}

Status allocate_child_table(
    const AddressSpace& address_space,
    uint64_t* parent_entry,
    CreatedTable* created,
    size_t* created_count,
    uint64_t** child_table) {
    uint64_t physical_address = 0;
    if (!address_space.backend.allocate_table(
            address_space.backend.context,
            &physical_address)) {
        return Status::OutOfMemory;
    }

    const Status physical_status =
        validate_physical_page(address_space, physical_address);
    if (physical_status != Status::Ok) {
        address_space.backend.free_table(
            address_space.backend.context,
            physical_address);
        return Status::BackendFailure;
    }

    uint64_t* table = nullptr;
    const Status table_status =
        table_for_physical(address_space, physical_address, &table);
    if (table_status != Status::Ok) {
        address_space.backend.free_table(
            address_space.backend.context,
            physical_address);
        return Status::BackendFailure;
    }

    clear_table(table);
    store_entry(
        parent_entry,
        physical_address | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_USER |
            ENTRY_MODULE_OWNED);
    created[*created_count] = {parent_entry, physical_address};
    ++(*created_count);
    *child_table = table;
    return Status::Ok;
}

Status walk_to_leaf(
    const AddressSpace& address_space,
    uint64_t virtual_address,
    WalkPath* path) {
    if (!path) {
        return Status::InvalidArgument;
    }

    uint64_t* table = nullptr;
    Status status = table_for_physical(
        address_space,
        address_space.root_table_physical,
        &table);
    if (status != Status::Ok) {
        return status;
    }

    path->tables[0] = table;
    path->effective_writable = true;
    path->effective_user = true;
    path->effective_no_execute = false;

    for (size_t level = 0; level < 3; ++level) {
        uint64_t* entry = &table[table_index(virtual_address, level)];
        const uint64_t value = load_entry(entry);
        if ((value & ENTRY_PRESENT) == 0) {
            return Status::NotMapped;
        }
        if ((value & ENTRY_HUGE_OR_PAT) != 0) {
            return level == 0
                ? Status::CorruptPageTable
                : Status::HugePageConflict;
        }

        path->effective_writable =
            path->effective_writable && (value & ENTRY_WRITABLE) != 0;
        path->effective_user =
            path->effective_user && (value & ENTRY_USER) != 0;
        path->effective_no_execute =
            path->effective_no_execute || (value & ENTRY_NO_EXECUTE) != 0;
        path->parent_entries[level] = entry;

        status = child_table_from_entry(
            address_space,
            value,
            &path->child_physical[level],
            &table);
        if (status != Status::Ok) {
            return status;
        }
        path->tables[level + 1] = table;
    }

    path->leaf_value =
        load_entry(&table[table_index(virtual_address, 3)]);
    if ((path->leaf_value & ENTRY_PRESENT) == 0) {
        return Status::NotMapped;
    }
    if ((path->leaf_value & (ARCHITECTURAL_ADDRESS_MASK &
                             ~address_space.physical_address_mask)) != 0) {
        return Status::CorruptPageTable;
    }

    path->effective_writable =
        path->effective_writable &&
        (path->leaf_value & ENTRY_WRITABLE) != 0;
    path->effective_user =
        path->effective_user && (path->leaf_value & ENTRY_USER) != 0;
    path->effective_no_execute =
        path->effective_no_execute ||
        (path->leaf_value & ENTRY_NO_EXECUTE) != 0;
    return Status::Ok;
}

Mapping mapping_from_path(
    uint64_t virtual_address,
    const AddressSpace& address_space,
    const WalkPath& path) {
    MapFlags flags = MapFlags::None;
    if (path.effective_writable) {
        flags |= MapFlags::Writable;
    }
    if (path.effective_user) {
        flags |= MapFlags::User;
    }
    if ((path.leaf_value & ENTRY_WRITE_THROUGH) != 0) {
        flags |= MapFlags::WriteThrough;
    }
    if ((path.leaf_value & ENTRY_CACHE_DISABLE) != 0) {
        flags |= MapFlags::CacheDisable;
    }
    if ((path.leaf_value & ENTRY_GLOBAL) != 0) {
        flags |= MapFlags::Global;
    }
    if (path.effective_no_execute) {
        flags |= MapFlags::NoExecute;
    }

    return {
        virtual_address & ~PAGE_OFFSET_MASK,
        path.leaf_value & address_space.physical_address_mask,
        flags,
        (path.leaf_value & ENTRY_ACCESSED) != 0,
        (path.leaf_value & ENTRY_DIRTY) != 0
    };
}

} // namespace

Status initialize(
    AddressSpace* address_space,
    uint64_t root_table_physical,
    const Backend* backend) {
    if (!address_space || !backend) {
        return Status::InvalidArgument;
    }

    if (!backend->allocate_table ||
        !backend->free_table ||
        !backend->physical_to_virtual ||
        !backend->invalidate_page) {
        return Status::InvalidArgument;
    }

    uint8_t physical_bits = backend->physical_address_bits;
    if (physical_bits == 0) {
        physical_bits = ARCHITECTURAL_PHYSICAL_ADDRESS_BITS;
    }
    if (physical_bits < 12 ||
        physical_bits > ARCHITECTURAL_PHYSICAL_ADDRESS_BITS) {
        return Status::InvalidArgument;
    }

    const uint64_t address_mask =
        ((UINT64_C(1) << physical_bits) - 1) & ~PAGE_OFFSET_MASK;
    if ((root_table_physical & PAGE_OFFSET_MASK) != 0) {
        return Status::UnalignedAddress;
    }
    if ((root_table_physical & ~address_mask) != 0) {
        return Status::PhysicalAddressOutOfRange;
    }

    AddressSpace candidate = {};
    candidate.root_table_physical = root_table_physical;
    candidate.physical_address_mask = address_mask;
    candidate.backend = *backend;
    uint64_t* root_table = nullptr;
    const Status root_status = table_for_physical(
        candidate,
        root_table_physical,
        &root_table);
    if (root_status != Status::Ok) {
        return root_status;
    }

    candidate.initialized = true;
    *address_space = candidate;
    return Status::Ok;
}

Status map_page(
    AddressSpace* address_space,
    uint64_t virtual_address,
    uint64_t physical_address,
    MapFlags flags) {
    if (!address_space) {
        return Status::InvalidArgument;
    }
    if (!address_space->initialized) {
        return Status::NotInitialized;
    }
    if (!valid_map_flags(flags)) {
        return Status::InvalidFlags;
    }

    Status status = validate_virtual_page(virtual_address);
    if (status != Status::Ok) {
        return status;
    }
    status = validate_physical_page(*address_space, physical_address);
    if (status != Status::Ok) {
        return status;
    }

    uint64_t* table = nullptr;
    status = table_for_physical(
        *address_space,
        address_space->root_table_physical,
        &table);
    if (status != Status::Ok) {
        return status;
    }

    CreatedTable created[3] = {};
    size_t created_count = 0;

    for (size_t level = 0; level < 3; ++level) {
        uint64_t* entry = &table[table_index(virtual_address, level)];
        const uint64_t value = load_entry(entry);

        if ((value & ENTRY_PRESENT) == 0) {
            if (value != 0) {
                rollback_created_tables(
                    *address_space,
                    created,
                    created_count,
                    virtual_address);
                return Status::CorruptPageTable;
            }
            status = allocate_child_table(
                *address_space,
                entry,
                created,
                &created_count,
                &table);
            if (status != Status::Ok) {
                rollback_created_tables(
                    *address_space,
                    created,
                    created_count,
                    virtual_address);
                return status;
            }
            continue;
        }

        if ((value & ENTRY_HUGE_OR_PAT) != 0) {
            rollback_created_tables(
                *address_space,
                created,
                created_count,
                virtual_address);
            return level == 0
                ? Status::CorruptPageTable
                : Status::HugePageConflict;
        }
        // Never broaden an existing ancestor: sibling mappings may rely on
        // its restrictive permission bits. Newly allocated ancestors already
        // grant writable and user traversal, with leaf entries restricting the
        // individual mapping.
        if ((has_flag(flags, MapFlags::Writable) &&
             (value & ENTRY_WRITABLE) == 0) ||
            (has_flag(flags, MapFlags::User) &&
             (value & ENTRY_USER) == 0) ||
            (!has_flag(flags, MapFlags::NoExecute) &&
             (value & ENTRY_NO_EXECUTE) != 0)) {
            rollback_created_tables(
                *address_space,
                created,
                created_count,
                virtual_address);
            return Status::PermissionConflict;
        }

        uint64_t child_physical = 0;
        status = child_table_from_entry(
            *address_space,
            value,
            &child_physical,
            &table);
        if (status != Status::Ok) {
            rollback_created_tables(
                *address_space,
                created,
                created_count,
                virtual_address);
            return status;
        }
    }

    uint64_t* leaf = &table[table_index(virtual_address, 3)];
    const uint64_t old_leaf = load_entry(leaf);
    if ((old_leaf & ENTRY_PRESENT) != 0) {
        rollback_created_tables(
            *address_space,
            created,
            created_count,
            virtual_address);
        return Status::AlreadyMapped;
    }
    if (old_leaf != 0) {
        rollback_created_tables(
            *address_space,
            created,
            created_count,
            virtual_address);
        return Status::CorruptPageTable;
    }
    store_entry(
        leaf,
        physical_address | hardware_leaf_flags(flags));
    address_space->backend.invalidate_page(
        address_space->backend.context,
        virtual_address);
    return Status::Ok;
}

Status unmap_page(
    AddressSpace* address_space,
    uint64_t virtual_address,
    Mapping* removed_mapping) {
    if (removed_mapping) {
        *removed_mapping = {};
    }
    if (!address_space) {
        return Status::InvalidArgument;
    }
    if (!address_space->initialized) {
        return Status::NotInitialized;
    }
    const Status address_status = validate_virtual_page(virtual_address);
    if (address_status != Status::Ok) {
        return address_status;
    }

    WalkPath path = {};
    const Status walk_status =
        walk_to_leaf(*address_space, virtual_address, &path);
    if (walk_status != Status::Ok) {
        return walk_status;
    }

    if (removed_mapping) {
        *removed_mapping =
            mapping_from_path(virtual_address, *address_space, path);
    }

    uint64_t* leaf =
        &path.tables[3][table_index(virtual_address, 3)];
    store_entry(leaf, 0);

    // Collapse only tables tagged as module-owned. The root PML4 is always
    // caller-owned and is never released here. Detach all reclaimable tables
    // before invalidation, then recycle them only after the shootdown returns.
    uint64_t detached_tables[3] = {};
    size_t detached_count = 0;
    for (size_t depth = 3; depth > 0; --depth) {
        const size_t parent_level = depth - 1;
        uint64_t* child_table = path.tables[depth];
        if (!table_empty(child_table)) {
            break;
        }

        uint64_t* parent_entry = path.parent_entries[parent_level];
        const uint64_t parent_value = load_entry(parent_entry);
        if ((parent_value & ENTRY_PRESENT) == 0 ||
            (parent_value & ENTRY_MODULE_OWNED) == 0 ||
            (parent_value & address_space->physical_address_mask) !=
                path.child_physical[parent_level]) {
            break;
        }

        store_entry(parent_entry, 0);
        detached_tables[detached_count++] =
            path.child_physical[parent_level];
    }

    address_space->backend.invalidate_page(
        address_space->backend.context,
        virtual_address);
    for (size_t index = 0; index < detached_count; ++index) {
        address_space->backend.free_table(
            address_space->backend.context,
            detached_tables[index]);
    }
    return Status::Ok;
}

Status query_page(
    const AddressSpace* address_space,
    uint64_t virtual_address,
    Mapping* mapping) {
    if (mapping) {
        *mapping = {};
    }
    if (!address_space || !mapping) {
        return Status::InvalidArgument;
    }
    if (!address_space->initialized) {
        return Status::NotInitialized;
    }
    if (!canonical_address(virtual_address)) {
        return Status::NonCanonicalAddress;
    }

    WalkPath path = {};
    const Status status =
        walk_to_leaf(*address_space, virtual_address, &path);
    if (status != Status::Ok) {
        return status;
    }
    *mapping = mapping_from_path(
        virtual_address,
        *address_space,
        path);
    return Status::Ok;
}

Status translate(
    const AddressSpace* address_space,
    uint64_t virtual_address,
    uint64_t* physical_address) {
    if (physical_address) {
        *physical_address = 0;
    }
    if (!physical_address) {
        return Status::InvalidArgument;
    }

    Mapping mapping = {};
    const Status status =
        query_page(address_space, virtual_address, &mapping);
    if (status != Status::Ok) {
        return status;
    }

    const uint64_t offset = virtual_address & PAGE_OFFSET_MASK;
    if (mapping.physical_address > UINT64_MAX - offset) {
        return Status::AddressOverflow;
    }
    *physical_address = mapping.physical_address + offset;
    return Status::Ok;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "not initialized";
        case Status::InvalidArgument: return "invalid argument";
        case Status::InvalidFlags: return "invalid flags";
        case Status::NonCanonicalAddress: return "non-canonical address";
        case Status::UnalignedAddress: return "unaligned address";
        case Status::AddressOverflow: return "address overflow";
        case Status::PhysicalAddressOutOfRange:
            return "physical address out of range";
        case Status::AlreadyMapped: return "already mapped";
        case Status::NotMapped: return "not mapped";
        case Status::OutOfMemory: return "out of memory";
        case Status::BackendFailure: return "backend failure";
        case Status::HugePageConflict: return "huge-page conflict";
        case Status::PermissionConflict: return "permission conflict";
        case Status::CorruptPageTable: return "corrupt page table";
    }
    return "unknown status";
}

} // namespace memory::virtual_memory
