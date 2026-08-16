#include "kernel_virtual_memory.hpp"

#include "physical_memory.hpp"

namespace memory::kernel_virtual_memory {

namespace {

virtual_memory::AddressSpace g_address_space = {};
uint8_t g_physical_address_bits = 0;
void* g_root_frame = nullptr;

void cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t& eax,
    uint32_t& ebx,
    uint32_t& ecx,
    uint32_t& edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(subleaf)
        : "memory");
}

uint8_t detect_physical_address_bits() {
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    cpuid(UINT32_C(0x80000000), 0, eax, ebx, ecx, edx);
    if (eax < UINT32_C(0x80000008)) {
        return 0;
    }
    cpuid(UINT32_C(0x80000008), 0, eax, ebx, ecx, edx);
    const uint8_t bits = static_cast<uint8_t>(eax & UINT32_C(0xFF));
    return bits >= 12 &&
                   bits <= virtual_memory::ARCHITECTURAL_PHYSICAL_ADDRESS_BITS
               ? bits
               : 0;
}

uint64_t read_control_register_3() {
    uint64_t value = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value) : : "memory");
    return value;
}

uint64_t read_control_register_4() {
    uint64_t value = 0;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value) : : "memory");
    return value;
}

void write_control_register_3(uint64_t value) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

bool allocate_table(void*, uint64_t* physical_address) {
    if (!physical_address) {
        return false;
    }
    void* frame = memory::alloc_frame();
    if (!frame) {
        *physical_address = 0;
        return false;
    }
    *physical_address = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(frame));
    return true;
}

void free_table(void*, uint64_t physical_address) {
    static_cast<void>(memory::try_free_frame(
        reinterpret_cast<void*>(static_cast<uintptr_t>(physical_address))));
}

void* physical_to_virtual(void*, uint64_t physical_address) {
    if (physical_address > UINTPTR_MAX) {
        return nullptr;
    }
    return reinterpret_cast<void*>(
        static_cast<uintptr_t>(physical_address));
}

void invalidate_page(void*, uint64_t virtual_address) {
    __asm__ volatile(
        "invlpg (%0)"
        :
        : "r"(static_cast<uintptr_t>(virtual_address))
        : "memory");
}

virtual_memory::Backend page_table_backend() {
    return {
        nullptr,
        allocate_table,
        free_table,
        physical_to_virtual,
        invalidate_page,
        g_physical_address_bits
    };
}

uint64_t active_root_physical() {
    constexpr uint64_t address_mask = UINT64_C(0x000FFFFFFFFFF000);
    return read_control_register_3() & address_mask;
}

} // namespace

Status initialize() {
    if (g_address_space.initialized) {
        return Status::Ok;
    }
    if (!memory::physical_memory_initialized() ||
        memory::physical_frame_size() != virtual_memory::PAGE_SIZE) {
        return Status::PhysicalMemoryUnavailable;
    }

    constexpr uint64_t cr4_la57 = UINT64_C(1) << 12;
    if ((read_control_register_4() & cr4_la57) != 0) {
        return Status::FiveLevelPagingUnsupported;
    }

    constexpr uint64_t cr3_address_mask = UINT64_C(0x000FFFFFFFFFF000);
    constexpr uint64_t cr3_low_control_mask = UINT64_C(0xFFF);
    const uint64_t previous_cr3 = read_control_register_3();
    const uint64_t firmware_root = previous_cr3 & cr3_address_mask;
    if (firmware_root == 0 || firmware_root > UINTPTR_MAX) {
        return Status::InvalidRootTable;
    }

    g_physical_address_bits = detect_physical_address_bits();
    void* root_frame = memory::alloc_frame();
    if (!root_frame) {
        g_physical_address_bits = 0;
        return Status::BackendInitializationFailed;
    }
    const uint64_t private_root = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(root_frame));
    auto* source = reinterpret_cast<const uint64_t*>(
        static_cast<uintptr_t>(firmware_root));
    auto* destination = static_cast<uint64_t*>(root_frame);
    for (size_t index = 0;
         index < virtual_memory::PAGE_TABLE_ENTRY_COUNT;
         ++index) {
        destination[index] = source[index];
    }

    // Keeping the firmware PCID/PWT/PCD low bits preserves the current x86
    // mode. A CR3 write without the no-flush hint synchronizes translation
    // caches before the private root is modified.
    const uint64_t private_cr3 =
        private_root | (previous_cr3 & cr3_low_control_mask);
    write_control_register_3(private_cr3);

    const virtual_memory::Backend backend = page_table_backend();
    const virtual_memory::Status status = virtual_memory::initialize(
        &g_address_space,
        private_root,
        &backend);
    if (status != virtual_memory::Status::Ok) {
        write_control_register_3(previous_cr3);
        memory::free_frame(root_frame);
        g_physical_address_bits = 0;
        return status == virtual_memory::Status::UnalignedAddress ||
                       status ==
                           virtual_memory::Status::PhysicalAddressOutOfRange
                   ? Status::InvalidRootTable
                   : Status::BackendInitializationFailed;
    }
    g_root_frame = root_frame;
    return Status::Ok;
}

Status create_address_space(OwnedAddressSpace* output) {
    if (!output || !initialized()) {
        return Status::BackendInitializationFailed;
    }
    *output = {};

    void* root_frame = memory::alloc_frame();
    if (!root_frame) {
        return Status::AddressSpaceAllocationFailed;
    }
    auto* source = reinterpret_cast<const uint64_t*>(
        static_cast<uintptr_t>(g_address_space.root_table_physical));
    auto* destination = static_cast<uint64_t*>(root_frame);
    for (size_t index = 0;
         index < virtual_memory::PAGE_TABLE_ENTRY_COUNT;
         ++index) {
        destination[index] = source[index];
    }

    const virtual_memory::Backend backend = page_table_backend();
    const virtual_memory::Status status = virtual_memory::initialize(
        &output->address_space,
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(root_frame)),
        &backend);
    if (status != virtual_memory::Status::Ok) {
        memory::free_frame(root_frame);
        *output = {};
        return Status::AddressSpaceInitializationFailed;
    }
    output->root_frame = root_frame;
    output->initialized = true;
    return Status::Ok;
}

Status activate(OwnedAddressSpace* address_space) {
    if (!address_space || !address_space->initialized ||
        !address_space->address_space.initialized ||
        address_space->root_frame == nullptr) {
        return Status::AddressSpaceInitializationFailed;
    }
    constexpr uint64_t low_control_mask = UINT64_C(0xFFF);
    const uint64_t controls = read_control_register_3() & low_control_mask;
    write_control_register_3(
        address_space->address_space.root_table_physical | controls);
    return Status::Ok;
}

Status activate_kernel() {
    if (!initialized()) {
        return Status::BackendInitializationFailed;
    }
    constexpr uint64_t low_control_mask = UINT64_C(0xFFF);
    const uint64_t controls = read_control_register_3() & low_control_mask;
    write_control_register_3(g_address_space.root_table_physical | controls);
    return Status::Ok;
}

bool is_active(const OwnedAddressSpace* address_space) {
    return address_space != nullptr && address_space->initialized &&
        active_root_physical() ==
            address_space->address_space.root_table_physical;
}

Status destroy_address_space(OwnedAddressSpace* address_space) {
    if (!address_space || !address_space->initialized ||
        address_space->root_frame == nullptr) {
        return Status::AddressSpaceInitializationFailed;
    }
    if (is_active(address_space)) {
        return Status::AddressSpaceActive;
    }
    memory::free_frame(address_space->root_frame);
    *address_space = {};
    return Status::Ok;
}

Status self_test() {
    if (!g_address_space.initialized) {
        return Status::BackendInitializationFailed;
    }

    // Each address occupies a different upper-half PML4 slot. The test never
    // overwrites an existing mapping and therefore remains safe on firmware
    // with a non-minimal identity map.
    constexpr uint64_t candidates[] = {
        UINT64_C(0xFFFF800000000000),
        UINT64_C(0xFFFF900000000000),
        UINT64_C(0xFFFFA00000000000),
        UINT64_C(0xFFFFC00000000000),
        UINT64_C(0xFFFFF00000000000)
    };
    uint64_t test_virtual_address = 0;
    for (uint64_t candidate : candidates) {
        virtual_memory::Mapping existing = {};
        if (virtual_memory::query_page(
                &g_address_space, candidate, &existing) ==
            virtual_memory::Status::NotMapped) {
            test_virtual_address = candidate;
            break;
        }
    }
    if (test_virtual_address == 0) {
        return Status::NoTestVirtualAddress;
    }

    const size_t frames_before = memory::used_frames();
    void* frame = memory::alloc_frame();
    if (!frame) {
        return Status::TestFrameAllocationFailed;
    }
    const uint64_t physical_address = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(frame));
    const virtual_memory::Status map_status = virtual_memory::map_page(
        &g_address_space,
        test_virtual_address,
        physical_address,
        virtual_memory::MapFlags::Writable);
    if (map_status != virtual_memory::Status::Ok) {
        memory::free_frame(frame);
        return Status::TestMappingFailed;
    }

    uint64_t translated = 0;
    const virtual_memory::Status translate_status =
        virtual_memory::translate(
            &g_address_space,
            test_virtual_address + sizeof(uint64_t),
            &translated);
    const bool translation_ok =
        translate_status == virtual_memory::Status::Ok &&
        translated == physical_address + sizeof(uint64_t);

    constexpr uint64_t test_pattern = UINT64_C(0x4B55524F564D4D31);
    auto* physical_word = static_cast<volatile uint64_t*>(frame);
    auto* virtual_word = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(test_virtual_address));
    *physical_word = 0;
    *virtual_word = test_pattern;
    const bool data_ok = *physical_word == test_pattern;

    virtual_memory::Mapping removed = {};
    const virtual_memory::Status unmap_status =
        virtual_memory::unmap_page(
            &g_address_space,
            test_virtual_address,
            &removed);
    const bool removed_expected_page =
        removed.physical_address == physical_address;
    memory::free_frame(frame);

    if (!translation_ok) {
        return Status::TestTranslationFailed;
    }
    if (!data_ok) {
        return Status::TestDataMismatch;
    }
    if (unmap_status != virtual_memory::Status::Ok ||
        !removed_expected_page) {
        return Status::TestUnmapFailed;
    }
    if (memory::used_frames() != frames_before) {
        return Status::TestFrameLeak;
    }
    return Status::Ok;
}

bool initialized() {
    return g_address_space.initialized && g_root_frame != nullptr;
}

uint64_t root_table_physical() {
    return g_address_space.root_table_physical;
}

uint64_t active_root_table_physical() {
    return active_root_physical();
}

uint8_t physical_address_bits() {
    return g_physical_address_bits;
}

virtual_memory::AddressSpace* address_space() {
    return g_address_space.initialized ? &g_address_space : nullptr;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::PhysicalMemoryUnavailable:
            return "physical memory manager unavailable";
        case Status::FiveLevelPagingUnsupported:
            return "five-level paging is not supported";
        case Status::InvalidRootTable: return "invalid CR3 root table";
        case Status::BackendInitializationFailed:
            return "page-table backend initialization failed";
        case Status::NoTestVirtualAddress:
            return "no unused self-test virtual address";
        case Status::TestFrameAllocationFailed:
            return "self-test frame allocation failed";
        case Status::TestMappingFailed: return "self-test map failed";
        case Status::TestTranslationFailed:
            return "self-test translation failed";
        case Status::TestDataMismatch:
            return "self-test alias data mismatch";
        case Status::TestUnmapFailed: return "self-test unmap failed";
        case Status::TestFrameLeak:
            return "self-test leaked page-table frames";
        case Status::AddressSpaceAllocationFailed:
            return "address-space root allocation failed";
        case Status::AddressSpaceInitializationFailed:
            return "address-space initialization failed";
        case Status::AddressSpaceActive:
            return "address space is still active";
    }
    return "unknown virtual-memory status";
}

} // namespace memory::kernel_virtual_memory
