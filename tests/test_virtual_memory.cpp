#include "../kernel/memory/virtual_memory.hpp"

#include <stdio.h>

namespace {

namespace vm = memory::virtual_memory;

constexpr size_t TEST_TABLE_COUNT = 64;
constexpr uint64_t TEST_PHYSICAL_BASE = UINT64_C(0x00100000);
constexpr uint64_t ENTRY_PRESENT = UINT64_C(1) << 0;
constexpr uint64_t ENTRY_WRITABLE = UINT64_C(1) << 1;
constexpr uint64_t ENTRY_USER = UINT64_C(1) << 2;
constexpr uint64_t ENTRY_ACCESSED = UINT64_C(1) << 5;
constexpr uint64_t ENTRY_DIRTY = UINT64_C(1) << 6;
constexpr uint64_t ENTRY_HUGE = UINT64_C(1) << 7;
constexpr uint64_t ENTRY_PROTECTION_KEY = UINT64_C(1) << 59;
constexpr uint64_t ENTRY_NO_EXECUTE = UINT64_C(1) << 63;
constexpr uint64_t ENTRY_ADDRESS_MASK = UINT64_C(0x000FFFFFFFFFF000);

struct alignas(4096) TestTable {
    uint64_t entries[vm::PAGE_TABLE_ENTRY_COUNT];
    bool in_use;
};

struct TestPool {
    TestTable tables[TEST_TABLE_COUNT];
    size_t allocation_limit;
    size_t allocation_successes;
    size_t free_count;
    size_t invalidation_count;
    size_t live_tables_at_last_invalidation;
    size_t frees_at_last_invalidation;
    uint64_t last_invalidated;
    uint64_t root_entry_at_last_invalidation;
    uint64_t failed_translation;
};

TestPool pool;

uint64_t table_physical(size_t index) {
    return TEST_PHYSICAL_BASE + index * vm::PAGE_SIZE;
}

void reset_pool() {
    for (size_t table = 0; table < TEST_TABLE_COUNT; ++table) {
        for (size_t entry = 0;
             entry < vm::PAGE_TABLE_ENTRY_COUNT;
             ++entry) {
            pool.tables[table].entries[entry] = 0;
        }
        pool.tables[table].in_use = false;
    }
    pool.tables[0].in_use = true;
    pool.allocation_limit = SIZE_MAX;
    pool.allocation_successes = 0;
    pool.free_count = 0;
    pool.invalidation_count = 0;
    pool.live_tables_at_last_invalidation = 0;
    pool.frees_at_last_invalidation = 0;
    pool.last_invalidated = 0;
    pool.root_entry_at_last_invalidation = 0;
    pool.failed_translation = UINT64_MAX;
}

size_t table_index_from_physical(uint64_t physical_address) {
    if (physical_address < TEST_PHYSICAL_BASE ||
        (physical_address - TEST_PHYSICAL_BASE) % vm::PAGE_SIZE != 0) {
        return TEST_TABLE_COUNT;
    }
    const uint64_t index =
        (physical_address - TEST_PHYSICAL_BASE) / vm::PAGE_SIZE;
    return index < TEST_TABLE_COUNT
        ? static_cast<size_t>(index)
        : TEST_TABLE_COUNT;
}

bool allocate_table(void* context, uint64_t* physical_address) {
    TestPool* test_pool = static_cast<TestPool*>(context);
    if (!physical_address ||
        test_pool->allocation_successes >= test_pool->allocation_limit) {
        return false;
    }

    for (size_t index = 1; index < TEST_TABLE_COUNT; ++index) {
        if (test_pool->tables[index].in_use) {
            continue;
        }
        test_pool->tables[index].in_use = true;
        for (size_t entry = 0;
             entry < vm::PAGE_TABLE_ENTRY_COUNT;
             ++entry) {
            test_pool->tables[index].entries[entry] =
                UINT64_C(0xA5A5A5A5A5A5A5A5);
        }
        *physical_address = table_physical(index);
        ++test_pool->allocation_successes;
        return true;
    }
    return false;
}

void free_table(void* context, uint64_t physical_address) {
    TestPool* test_pool = static_cast<TestPool*>(context);
    const size_t index = table_index_from_physical(physical_address);
    if (index == 0 || index == TEST_TABLE_COUNT ||
        !test_pool->tables[index].in_use) {
        return;
    }
    test_pool->tables[index].in_use = false;
    ++test_pool->free_count;
}

void* physical_to_virtual(void* context, uint64_t physical_address) {
    TestPool* test_pool = static_cast<TestPool*>(context);
    if (physical_address == test_pool->failed_translation) {
        return nullptr;
    }
    const size_t index = table_index_from_physical(physical_address);
    if (index == TEST_TABLE_COUNT || !test_pool->tables[index].in_use) {
        return nullptr;
    }
    return test_pool->tables[index].entries;
}

void invalidate_page(void* context, uint64_t virtual_address) {
    TestPool* test_pool = static_cast<TestPool*>(context);
    ++test_pool->invalidation_count;
    test_pool->last_invalidated = virtual_address;
    test_pool->frees_at_last_invalidation = test_pool->free_count;
    test_pool->live_tables_at_last_invalidation = 0;
    for (size_t index = 0; index < TEST_TABLE_COUNT; ++index) {
        if (test_pool->tables[index].in_use) {
            ++test_pool->live_tables_at_last_invalidation;
        }
    }
    const size_t root_index = static_cast<size_t>(
        (virtual_address >> 39) & UINT64_C(0x1FF));
    test_pool->root_entry_at_last_invalidation =
        test_pool->tables[0].entries[root_index];
}

vm::Backend make_backend(uint8_t physical_address_bits = 48) {
    return {
        &pool,
        allocate_table,
        free_table,
        physical_to_virtual,
        invalidate_page,
        physical_address_bits
    };
}

bool initialize_space(
    vm::AddressSpace* address_space,
    uint8_t physical_address_bits = 48) {
    const vm::Backend backend = make_backend(physical_address_bits);
    return vm::initialize(
               address_space,
               TEST_PHYSICAL_BASE,
               &backend) == vm::Status::Ok;
}

size_t live_table_count() {
    size_t count = 0;
    for (size_t index = 0; index < TEST_TABLE_COUNT; ++index) {
        if (pool.tables[index].in_use) {
            ++count;
        }
    }
    return count;
}

uint64_t* reserve_external_table(size_t index) {
    if (index == 0 || index >= TEST_TABLE_COUNT) {
        return nullptr;
    }
    pool.tables[index].in_use = true;
    for (size_t entry = 0;
         entry < vm::PAGE_TABLE_ENTRY_COUNT;
         ++entry) {
        pool.tables[index].entries[entry] = 0;
    }
    return pool.tables[index].entries;
}

size_t virtual_index(uint64_t virtual_address, size_t level) {
    constexpr uint8_t shifts[4] = {39, 30, 21, 12};
    return static_cast<size_t>(
        (virtual_address >> shifts[level]) & UINT64_C(0x1FF));
}

uint64_t* table_from_entry(uint64_t entry) {
    const size_t index = table_index_from_physical(
        entry & ENTRY_ADDRESS_MASK);
    if (index == TEST_TABLE_COUNT || !pool.tables[index].in_use) {
        return nullptr;
    }
    return pool.tables[index].entries;
}

uint64_t* find_leaf(uint64_t virtual_address) {
    uint64_t* table = pool.tables[0].entries;
    for (size_t level = 0; level < 3; ++level) {
        const uint64_t entry = table[virtual_index(virtual_address, level)];
        if ((entry & ENTRY_PRESENT) == 0 || (entry & ENTRY_HUGE) != 0) {
            return nullptr;
        }
        table = table_from_entry(entry);
        if (!table) {
            return nullptr;
        }
    }
    return &table[virtual_index(virtual_address, 3)];
}

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("check failed at line %d: %s\n", __LINE__, #condition);  \
            return false;                                                    \
        }                                                                    \
    } while (false)

bool test_initialization_and_validation() {
    reset_pool();
    vm::AddressSpace address_space = {};
    vm::Mapping mapping = {};
    uint64_t translated = UINT64_MAX;

    CHECK(vm::query_page(&address_space, 0, &mapping) ==
          vm::Status::NotInitialized);
    CHECK(vm::translate(&address_space, 0, &translated) ==
          vm::Status::NotInitialized);
    CHECK(translated == 0);
    CHECK(vm::initialize(nullptr, TEST_PHYSICAL_BASE, nullptr) ==
          vm::Status::InvalidArgument);

    vm::Backend backend = make_backend();
    CHECK(vm::initialize(&address_space, TEST_PHYSICAL_BASE, nullptr) ==
          vm::Status::InvalidArgument);
    backend.invalidate_page = nullptr;
    CHECK(vm::initialize(&address_space, TEST_PHYSICAL_BASE, &backend) ==
          vm::Status::InvalidArgument);

    backend = make_backend(11);
    CHECK(vm::initialize(&address_space, TEST_PHYSICAL_BASE, &backend) ==
          vm::Status::InvalidArgument);
    backend = make_backend(53);
    CHECK(vm::initialize(&address_space, TEST_PHYSICAL_BASE, &backend) ==
          vm::Status::InvalidArgument);
    backend = make_backend();
    CHECK(vm::initialize(&address_space, TEST_PHYSICAL_BASE + 1, &backend) ==
          vm::Status::UnalignedAddress);
    backend = make_backend(20);
    CHECK(vm::initialize(&address_space, TEST_PHYSICAL_BASE, &backend) ==
          vm::Status::PhysicalAddressOutOfRange);

    backend = make_backend();
    pool.failed_translation = TEST_PHYSICAL_BASE;
    CHECK(vm::initialize(&address_space, TEST_PHYSICAL_BASE, &backend) ==
          vm::Status::BackendFailure);
    pool.failed_translation = UINT64_MAX;
    CHECK(vm::initialize(&address_space, TEST_PHYSICAL_BASE, &backend) ==
          vm::Status::Ok);

    const vm::AddressSpace initialized_copy = address_space;
    backend.invalidate_page = nullptr;
    CHECK(vm::initialize(&address_space, TEST_PHYSICAL_BASE, &backend) ==
          vm::Status::InvalidArgument);
    CHECK(address_space.initialized == initialized_copy.initialized);
    CHECK(address_space.root_table_physical ==
          initialized_copy.root_table_physical);
    CHECK(address_space.backend.invalidate_page ==
          initialized_copy.backend.invalidate_page);

    CHECK(vm::map_page(
              &address_space,
              UINT64_C(0x0000800000000000),
              UINT64_C(0x00200000),
              vm::MapFlags::None) == vm::Status::NonCanonicalAddress);
    CHECK(vm::query_page(
              &address_space,
              UINT64_C(0xFFFF7FFFFFFFFFFF),
              &mapping) == vm::Status::NonCanonicalAddress);
    CHECK(vm::map_page(
              &address_space,
              UINT64_C(0x400001),
              UINT64_C(0x00200000),
              vm::MapFlags::None) == vm::Status::UnalignedAddress);
    CHECK(vm::map_page(
              &address_space,
              UINT64_C(0x400000),
              UINT64_C(0x00200001),
              vm::MapFlags::None) == vm::Status::UnalignedAddress);
    CHECK(vm::map_page(
              &address_space,
              UINT64_C(0x400000),
              UINT64_C(1) << 48,
              vm::MapFlags::None) ==
          vm::Status::PhysicalAddressOutOfRange);
    CHECK(vm::map_page(
              &address_space,
              UINT64_C(0x400000),
              UINT64_C(0x00200000),
              static_cast<vm::MapFlags>(UINT64_C(1) << 40)) ==
          vm::Status::InvalidFlags);
    CHECK(vm::query_page(&address_space, 0, nullptr) ==
          vm::Status::InvalidArgument);
    CHECK(vm::translate(&address_space, 0, nullptr) ==
          vm::Status::InvalidArgument);
    CHECK(vm::status_message(vm::Status::HugePageConflict)[0] != '\0');
    CHECK(live_table_count() == 1);
    return true;
}

bool test_map_unmap_and_reuse() {
    reset_pool();
    vm::AddressSpace address_space = {};
    CHECK(initialize_space(&address_space));

    constexpr uint64_t virtual_address = UINT64_C(0x0000000040000000);
    constexpr uint64_t physical_address = UINT64_C(0x00200000);
    const vm::MapFlags first_flags =
        vm::MapFlags::Writable |
        vm::MapFlags::CacheDisable |
        vm::MapFlags::NoExecute;

    CHECK(vm::map_page(
              &address_space,
              virtual_address,
              physical_address,
              first_flags) == vm::Status::Ok);
    CHECK(pool.allocation_successes == 3);
    CHECK(live_table_count() == 4);
    CHECK(pool.invalidation_count == 1);
    CHECK(pool.last_invalidated == virtual_address);

    const uint64_t first_root_entry =
        pool.tables[0].entries[virtual_index(virtual_address, 0)];
    vm::Mapping mapping = {};
    CHECK(vm::query_page(
              &address_space,
              virtual_address + UINT64_C(0x321),
              &mapping) == vm::Status::Ok);
    CHECK(mapping.virtual_address == virtual_address);
    CHECK(mapping.physical_address == physical_address);
    CHECK(mapping.flags == first_flags);
    CHECK(!mapping.accessed && !mapping.dirty);

    uint64_t translated = 0;
    CHECK(vm::translate(
              &address_space,
              virtual_address + UINT64_C(0xFED),
              &translated) == vm::Status::Ok);
    CHECK(translated == physical_address + UINT64_C(0xFED));

    CHECK(vm::map_page(
              &address_space,
              virtual_address,
              UINT64_C(0x00300000),
              vm::MapFlags::None) == vm::Status::AlreadyMapped);
    CHECK(pool.allocation_successes == 3);
    CHECK(pool.invalidation_count == 1);

    const vm::MapFlags second_flags =
        vm::MapFlags::User | vm::MapFlags::Global;
    CHECK(vm::map_page(
              &address_space,
              virtual_address + vm::PAGE_SIZE,
              physical_address + vm::PAGE_SIZE,
              second_flags) == vm::Status::Ok);
    CHECK(pool.allocation_successes == 3);

    vm::Mapping removed = {};
    CHECK(vm::unmap_page(
              &address_space,
              virtual_address,
              &removed) == vm::Status::Ok);
    CHECK(removed.virtual_address == virtual_address);
    CHECK(removed.physical_address == physical_address);
    CHECK(removed.flags == first_flags);
    CHECK(pool.free_count == 0);
    CHECK(vm::query_page(&address_space, virtual_address, &mapping) ==
          vm::Status::NotMapped);
    CHECK(vm::query_page(
              &address_space,
              virtual_address + vm::PAGE_SIZE,
              &mapping) == vm::Status::Ok);
    CHECK(mapping.physical_address == physical_address + vm::PAGE_SIZE);
    CHECK(mapping.flags == second_flags);

    CHECK(vm::unmap_page(
              &address_space,
              virtual_address + vm::PAGE_SIZE) == vm::Status::Ok);
    CHECK(pool.free_count == 3);
    CHECK(live_table_count() == 1);
    CHECK(pool.tables[0].entries[virtual_index(virtual_address, 0)] == 0);
    CHECK(pool.root_entry_at_last_invalidation == 0);
    CHECK(pool.live_tables_at_last_invalidation == 4);
    CHECK(pool.frees_at_last_invalidation == 0);
    CHECK(vm::unmap_page(&address_space, virtual_address) ==
          vm::Status::NotMapped);
    CHECK(pool.invalidation_count == 4);

    CHECK(vm::map_page(
              &address_space,
              virtual_address,
              physical_address,
              first_flags) == vm::Status::Ok);
    const uint64_t reused_root_entry =
        pool.tables[0].entries[virtual_index(virtual_address, 0)];
    CHECK((reused_root_entry & ENTRY_ADDRESS_MASK) ==
          (first_root_entry & ENTRY_ADDRESS_MASK));
    CHECK(vm::unmap_page(&address_space, virtual_address) == vm::Status::Ok);
    CHECK(live_table_count() == 1);
    return true;
}

bool test_flags_and_effective_permissions() {
    reset_pool();
    vm::AddressSpace address_space = {};
    CHECK(initialize_space(&address_space));

    constexpr uint64_t virtual_address = UINT64_C(0x12345000);
    constexpr uint64_t physical_address = UINT64_C(0x00ABC000);
    const vm::MapFlags all_flags =
        vm::MapFlags::Writable |
        vm::MapFlags::User |
        vm::MapFlags::WriteThrough |
        vm::MapFlags::CacheDisable |
        vm::MapFlags::Global |
        vm::MapFlags::NoExecute;
    CHECK(vm::map_page(
              &address_space,
              virtual_address,
              physical_address,
              all_flags) == vm::Status::Ok);

    uint64_t* leaf = find_leaf(virtual_address);
    CHECK(leaf != nullptr);
    *leaf |= ENTRY_ACCESSED | ENTRY_DIRTY;
    vm::Mapping mapping = {};
    CHECK(vm::query_page(
              &address_space,
              virtual_address,
              &mapping) == vm::Status::Ok);
    CHECK(mapping.flags == all_flags);
    CHECK(mapping.accessed && mapping.dirty);
    CHECK(vm::unmap_page(&address_space, virtual_address) == vm::Status::Ok);

    reset_pool();
    uint64_t* external_pdpt = reserve_external_table(1);
    CHECK(external_pdpt != nullptr);
    pool.tables[0].entries[0] = table_physical(1) | ENTRY_PRESENT;
    CHECK(initialize_space(&address_space));
    CHECK(vm::map_page(
              &address_space,
              0,
              physical_address,
              vm::MapFlags::Writable | vm::MapFlags::User) ==
          vm::Status::PermissionConflict);
    CHECK(pool.tables[0].entries[0] ==
          (table_physical(1) | ENTRY_PRESENT));
    CHECK(pool.allocation_successes == 0);
    CHECK(vm::map_page(
              &address_space,
              0,
              physical_address,
              vm::MapFlags::None) == vm::Status::Ok);
    CHECK(vm::query_page(&address_space, 0, &mapping) == vm::Status::Ok);
    CHECK(mapping.flags == vm::MapFlags::None);
    CHECK(vm::unmap_page(&address_space, 0) == vm::Status::Ok);
    CHECK(pool.tables[1].in_use);
    CHECK(pool.free_count == 2);

    reset_pool();
    external_pdpt = reserve_external_table(1);
    CHECK(external_pdpt != nullptr);
    pool.tables[0].entries[0] =
        table_physical(1) | ENTRY_PRESENT | ENTRY_WRITABLE |
        ENTRY_USER | ENTRY_NO_EXECUTE;
    CHECK(initialize_space(&address_space));
    CHECK(vm::map_page(
              &address_space,
              0,
              physical_address,
              vm::MapFlags::None) == vm::Status::PermissionConflict);
    CHECK(pool.allocation_successes == 0);
    CHECK(pool.free_count == 0);
    CHECK(external_pdpt[0] == 0);
    CHECK(vm::map_page(
              &address_space,
              0,
              physical_address,
              vm::MapFlags::NoExecute) == vm::Status::Ok);
    CHECK(vm::query_page(&address_space, 0, &mapping) == vm::Status::Ok);
    CHECK(mapping.flags == vm::MapFlags::NoExecute);
    CHECK(vm::unmap_page(&address_space, 0) == vm::Status::Ok);
    CHECK(pool.tables[1].in_use);
    return true;
}

bool test_boundaries_and_non_overlap() {
    reset_pool();
    vm::AddressSpace address_space = {};
    CHECK(initialize_space(&address_space));

    constexpr uint64_t virtual_addresses[] = {
        UINT64_C(0x00000000001FF000),
        UINT64_C(0x0000000000200000),
        UINT64_C(0x000000003FFFF000),
        UINT64_C(0x0000000040000000),
        UINT64_C(0x00007FFFFFFFF000),
        UINT64_C(0xFFFF800000000000),
        UINT64_C(0xFFFFFFFFFFFFF000)
    };
    constexpr uint64_t physical_addresses[] = {
        UINT64_C(0x00200000),
        UINT64_C(0x00210000),
        UINT64_C(0x00220000),
        UINT64_C(0x00230000),
        UINT64_C(0x00240000),
        UINT64_C(0x00250000),
        UINT64_C(0x0000FFFFFFFFF000)
    };
    constexpr size_t mapping_count =
        sizeof(virtual_addresses) / sizeof(virtual_addresses[0]);

    for (size_t index = 0; index < mapping_count; ++index) {
        const vm::MapFlags flags = (index & 1) != 0
            ? vm::MapFlags::Writable
            : vm::MapFlags::None;
        CHECK(vm::map_page(
                  &address_space,
                  virtual_addresses[index],
                  physical_addresses[index],
                  flags) == vm::Status::Ok);
    }

    for (size_t index = 0; index < mapping_count; ++index) {
        vm::Mapping mapping = {};
        CHECK(vm::query_page(
                  &address_space,
                  virtual_addresses[index] + UINT64_C(0xA5),
                  &mapping) == vm::Status::Ok);
        CHECK(mapping.virtual_address == virtual_addresses[index]);
        CHECK(mapping.physical_address == physical_addresses[index]);
        const vm::MapFlags expected_flags = (index & 1) != 0
            ? vm::MapFlags::Writable
            : vm::MapFlags::None;
        CHECK(mapping.flags == expected_flags);

        uint64_t translated = 0;
        CHECK(vm::translate(
                  &address_space,
                  virtual_addresses[index] + UINT64_C(0xA5),
                  &translated) == vm::Status::Ok);
        CHECK(translated == physical_addresses[index] + UINT64_C(0xA5));
    }

    vm::Mapping mapping = {};
    CHECK(vm::query_page(&address_space, UINT64_C(0x1000), &mapping) ==
          vm::Status::NotMapped);
    CHECK(vm::query_page(
              &address_space,
              UINT64_C(0x0000800000000000),
              &mapping) == vm::Status::NonCanonicalAddress);
    CHECK(vm::query_page(
              &address_space,
              UINT64_C(0xFFFF7FFFFFFFFFFF),
              &mapping) == vm::Status::NonCanonicalAddress);

    for (size_t index = 0; index < mapping_count; ++index) {
        CHECK(vm::unmap_page(
                  &address_space,
                  virtual_addresses[index]) == vm::Status::Ok);
    }
    CHECK(live_table_count() == 1);
    return true;
}

bool test_conflicts_and_external_tables() {
    reset_pool();
    uint64_t* pdpt = reserve_external_table(1);
    uint64_t* pd = reserve_external_table(2);
    uint64_t* pt = reserve_external_table(3);
    CHECK(pdpt && pd && pt);
    pool.tables[0].entries[0] =
        table_physical(1) | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_USER;
    pdpt[0] = table_physical(2) | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_USER;
    pd[0] = table_physical(3) | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_USER;
    pt[0] = UINT64_C(0x00600000) | ENTRY_PRESENT | ENTRY_WRITABLE;

    vm::AddressSpace address_space = {};
    CHECK(initialize_space(&address_space));
    CHECK(vm::map_page(
              &address_space,
              0,
              UINT64_C(0x00700000),
              vm::MapFlags::None) == vm::Status::AlreadyMapped);
    CHECK(pool.allocation_successes == 0);

    vm::Mapping removed = {};
    CHECK(vm::unmap_page(&address_space, 0, &removed) == vm::Status::Ok);
    CHECK(removed.physical_address == UINT64_C(0x00600000));
    CHECK(pool.free_count == 0);
    CHECK(pool.tables[1].in_use &&
          pool.tables[2].in_use &&
          pool.tables[3].in_use);
    CHECK(pd[0] != 0);

    // Existing leaf metadata outside the physical-address field (for example
    // protection-key bits) must not be mistaken for a corrupt address.
    pt[0] = UINT64_C(0x00601000) | ENTRY_PRESENT | ENTRY_PROTECTION_KEY;
    vm::Mapping mapping = {};
    CHECK(vm::query_page(&address_space, 0, &mapping) == vm::Status::Ok);
    CHECK(mapping.physical_address == UINT64_C(0x00601000));
    CHECK(vm::unmap_page(&address_space, 0) == vm::Status::Ok);
    CHECK(pool.free_count == 0);

    reset_pool();
    pdpt = reserve_external_table(1);
    CHECK(pdpt != nullptr);
    pool.tables[0].entries[0] =
        table_physical(1) | ENTRY_PRESENT | ENTRY_WRITABLE;
    pdpt[0] =
        UINT64_C(0x40000000) | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_HUGE;
    CHECK(initialize_space(&address_space));
    mapping = {};
    CHECK(vm::map_page(
              &address_space,
              0,
              UINT64_C(0x00200000),
              vm::MapFlags::None) == vm::Status::HugePageConflict);
    CHECK(vm::query_page(&address_space, 0, &mapping) ==
          vm::Status::HugePageConflict);
    CHECK(vm::unmap_page(&address_space, 0) ==
          vm::Status::HugePageConflict);
    CHECK(pool.allocation_successes == 0);
    CHECK(pool.invalidation_count == 0);

    reset_pool();
    pdpt = reserve_external_table(1);
    pd = reserve_external_table(2);
    CHECK(pdpt && pd);
    pool.tables[0].entries[0] =
        table_physical(1) | ENTRY_PRESENT | ENTRY_WRITABLE;
    pdpt[0] = table_physical(2) | ENTRY_PRESENT | ENTRY_WRITABLE;
    pd[0] = UINT64_C(0x00200000) |
        ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_HUGE;
    CHECK(initialize_space(&address_space));
    CHECK(vm::map_page(
              &address_space,
              0,
              UINT64_C(0x00300000),
              vm::MapFlags::None) == vm::Status::HugePageConflict);
    CHECK(vm::query_page(&address_space, 0, &mapping) ==
          vm::Status::HugePageConflict);

    reset_pool();
    pdpt = reserve_external_table(1);
    CHECK(pdpt != nullptr);
    pool.tables[0].entries[0] =
        table_physical(1) | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_HUGE;
    CHECK(initialize_space(&address_space));
    CHECK(vm::map_page(
              &address_space,
              0,
              UINT64_C(0x00200000),
              vm::MapFlags::None) == vm::Status::CorruptPageTable);
    CHECK(vm::query_page(&address_space, 0, &mapping) ==
          vm::Status::CorruptPageTable);
    CHECK(pool.allocation_successes == 0);

    reset_pool();
    pool.tables[0].entries[0] = ENTRY_WRITABLE;
    CHECK(initialize_space(&address_space));
    CHECK(vm::map_page(
              &address_space,
              0,
              UINT64_C(0x00200000),
              vm::MapFlags::None) == vm::Status::CorruptPageTable);
    CHECK(pool.allocation_successes == 0);
    return true;
}

bool test_oom_and_backend_rollback() {
    reset_pool();
    vm::AddressSpace address_space = {};
    CHECK(initialize_space(&address_space));
    pool.allocation_limit = 2;
    CHECK(vm::map_page(
              &address_space,
              UINT64_C(0x00800000),
              UINT64_C(0x00900000),
              vm::MapFlags::Writable) == vm::Status::OutOfMemory);
    CHECK(pool.allocation_successes == 2);
    CHECK(pool.free_count == 2);
    CHECK(live_table_count() == 1);
    CHECK(pool.tables[0].entries[0] == 0);
    CHECK(pool.invalidation_count == 1);
    CHECK(pool.root_entry_at_last_invalidation == 0);
    CHECK(pool.live_tables_at_last_invalidation == 3);
    CHECK(pool.frees_at_last_invalidation == 0);

    reset_pool();
    uint64_t* external_pdpt = reserve_external_table(1);
    CHECK(external_pdpt != nullptr);
    const uint64_t original_root_entry =
        table_physical(1) | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_USER;
    pool.tables[0].entries[0] = original_root_entry;
    CHECK(initialize_space(&address_space));
    pool.allocation_limit = 1;
    CHECK(vm::map_page(
              &address_space,
              0,
              UINT64_C(0x00200000),
              vm::MapFlags::Writable | vm::MapFlags::User) ==
          vm::Status::OutOfMemory);
    CHECK(pool.allocation_successes == 1);
    CHECK(pool.free_count == 1);
    CHECK(live_table_count() == 2);
    CHECK(pool.tables[0].entries[0] == original_root_entry);
    CHECK(external_pdpt[0] == 0);
    CHECK(pool.invalidation_count == 1);

    reset_pool();
    CHECK(initialize_space(&address_space));
    pool.failed_translation = table_physical(1);
    CHECK(vm::map_page(
              &address_space,
              0,
              UINT64_C(0x00200000),
              vm::MapFlags::None) == vm::Status::BackendFailure);
    CHECK(pool.allocation_successes == 1);
    CHECK(pool.free_count == 1);
    CHECK(live_table_count() == 1);
    CHECK(pool.tables[0].entries[0] == 0);
    return true;
}

struct NamedTest {
    const char* name;
    bool (*run)();
};

} // namespace

int main() {
    const NamedTest tests[] = {
        {"initialization and validation", test_initialization_and_validation},
        {"map, unmap, and reuse", test_map_unmap_and_reuse},
        {"flags and effective permissions", test_flags_and_effective_permissions},
        {"boundaries and non-overlap", test_boundaries_and_non_overlap},
        {"conflicts and external tables", test_conflicts_and_external_tables},
        {"OOM and backend rollback", test_oom_and_backend_rollback}
    };

    for (const NamedTest& test : tests) {
        if (!test.run()) {
            printf("FAIL: %s\n", test.name);
            return 1;
        }
        printf("PASS: %s\n", test.name);
    }
    return 0;
}
