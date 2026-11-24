#include "../kernel/storage/nvme.cpp"

#include <cassert>
#include <cstring>

namespace {
using namespace storage::nvme;

constexpr uint32_t controller_identity = UINT32_C(0x58451B36);
constexpr uint32_t replacement_identity = UINT32_C(0x12348086);
constexpr uint16_t firmware_command = UINT16_C(0x0105);
constexpr uint16_t driver_command = firmware_command | PCI_COMMAND_MEMORY_SPACE;
constexpr uint64_t physical_bar = UINT64_C(0xFEBF0000);
constexpr size_t page_size = memory::virtual_memory::PAGE_SIZE;
constexpr size_t page_count = 6U;

alignas(page_size) uint32_t registers[MMIO_REQUIRED_BYTES / sizeof(uint32_t)];
alignas(page_size) uint8_t frames[page_count][page_size];
bool live[page_count];
bool mapped[MMIO_PAGE_COUNT];
uint32_t identity;
uint16_t command;
size_t release_attempts[page_count];
size_t unmap_attempts[MMIO_PAGE_COUNT];
size_t scan_calls;
size_t command_reads;
size_t command_writes;
size_t replace_on_command_read;
size_t pages_allocated;
size_t fail_allocation;
unsigned registry_count;
unsigned unregister_count;
bool pci_present;
bool assert_fatal_on_third_allocation;
bool reject_pci_restore;
int fail_release_page;
int fail_map_page;
int fail_unmap_page;

const pci::Device device = {
    {0U, 3U, 0U},
    static_cast<uint16_t>(controller_identity),
    static_cast<uint16_t>(controller_identity >> 16U),
    PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_NVM, PCI_PROG_IF_NVME,
    0U, 0U, 0U,
};

uint32_t& reg(size_t offset) { return registers[offset / sizeof(uint32_t)]; }

void reset_fixture() {
    g_controller = {};
    std::memset(registers, 0, sizeof(registers));
    std::memset(live, 0, sizeof(live));
    std::memset(mapped, 0, sizeof(mapped));
    std::memset(release_attempts, 0, sizeof(release_attempts));
    std::memset(unmap_attempts, 0, sizeof(unmap_attempts));
    identity = controller_identity;
    command = firmware_command;
    scan_calls = command_reads = command_writes = replace_on_command_read = 0U;
    pages_allocated = fail_allocation = 0U;
    registry_count = unregister_count = 0U;
    pci_present = assert_fatal_on_third_allocation = reject_pci_restore = false;
    fail_release_page = fail_map_page = fail_unmap_page = -1;
}

void set_mapped_controller() {
    g_controller.pci_device = device;
    g_controller.pci_enabled = true;
    g_controller.original_pci_command = firmware_command;
    g_controller.registers = reinterpret_cast<volatile uint8_t*>(registers);
    g_controller.mapped_pages = MMIO_PAGE_COUNT;
    for (bool& mapping : mapped) mapping = true;
    g_controller.bar.size = MMIO_REQUIRED_BYTES;
    command = driver_command;
    reg(REG_CAP) = 63U;
    reg(REG_CAP + 4U) = UINT32_C(1) << 5U; // NVM command set.
}

void set_running_controller() {
    set_mapped_controller();
    storage::dma::Page* const pages[] = {
        &g_controller.admin_submission_queue,
        &g_controller.admin_completion_queue,
        &g_controller.identify_page,
        &g_controller.io_submission_queue,
        &g_controller.io_completion_queue,
        &g_controller.io_page,
    };
    for (auto* page : pages) {
        assert(storage::dma::allocate_page(true, page) ==
            storage::dma::Status::Ok);
    }

    g_controller.controller_enabled = g_controller.io_ready =
        g_controller.ready = true;
    g_controller.block.context = &g_controller;
    reg(REG_CC) = CC_ENABLE;
    reg(REG_CSTS) = CSTS_READY;
    registry_count = 1U;
}

void expect_all_live() {
    for (bool allocated : live) assert(allocated);
    for (bool mapping : mapped) assert(mapping);
}

void expect_all_released() {
    for (bool allocated : live) assert(!allocated);
    for (bool mapping : mapped) assert(!mapping);
}

void test_stuck_ready_retains_then_retires() {
    reset_fixture();
    set_running_controller();

    shutdown();
    expect_all_live();
    assert(!initialized() && block_device() == nullptr && registry_count == 0U);
    assert(unregister_count == 1U);
    assert((reg(REG_CC) & CC_ENABLE) == 0U);
    assert(command == (driver_command & ~PCI_COMMAND_BUS_MASTER));

    // CC.EN has already cleared, but CSTS.RDY remains set. Retry must
    // neither enumerate for a new startup nor restore firmware's BME.
    assert(initialize() == Status::ResourceReleaseFailed);
    expect_all_live();
    assert(command == (driver_command & ~PCI_COMMAND_BUS_MASTER));
    assert(scan_calls == 0U && command_writes == 1U);

    reg(REG_CSTS) = 0U;
    assert(initialize() == Status::NoController);
    expect_all_released();
    assert(scan_calls == 1U && command == firmware_command);
    for (size_t attempts : release_attempts) assert(attempts == 1U);
    for (size_t attempts : unmap_attempts) assert(attempts == 1U);

    shutdown();
    assert(unregister_count == 1U && command_writes == 2U);
}

void test_invalid_stop_proof() {
    reset_fixture();
    set_running_controller();
    reg(REG_CSTS) = UINT32_MAX;
    shutdown();
    expect_all_live();
    assert(command == (driver_command & ~PCI_COMMAND_BUS_MASTER));

    // MMIO decoding disabled can make an absent register appear as zero.
    command &= ~PCI_COMMAND_MEMORY_SPACE;
    reg(REG_CC) = 0U;
    reg(REG_CSTS) = 0U;
    shutdown();
    expect_all_live();

    // Hardware replaced the function before the final status readback.
    command = driver_command;
    replace_on_command_read = command_reads + 2U;
    shutdown();
    expect_all_live();
    assert(command == driver_command && command_writes == 1U);
    assert(initialize() == Status::ResourceReleaseFailed);
    assert(scan_calls == 0U);

    // A function with the same PCI IDs cannot prove continuity after loss.
    identity = controller_identity;
    shutdown();
    expect_all_live();
    assert(command_writes == 1U);
}

void test_startup_failure_after_queue_publication() {
    reset_fixture();
    set_mapped_controller();
    assert_fatal_on_third_allocation = true;
    assert(start_controller() == Status::ResourceReleaseFailed);
    assert(reg(REG_ASQ) != 0U && reg(REG_ACQ) != 0U);
    assert((reg(REG_CC) & CC_ENABLE) != 0U);
    assert(live[0] && live[1] && live[2] && !live[3]);
    assert(!initialized() && registry_count == 0U);
    assert(command == (driver_command & ~PCI_COMMAND_BUS_MASTER));

    // The controller clears its state after an external reset.
    reg(REG_CC) = 0U;
    reg(REG_CSTS) = 0U;
    shutdown();
    expect_all_released();
    assert(command == firmware_command);
}

void test_unpublished_pages_and_mapping_rollback() {
    reset_fixture();
    set_mapped_controller();
    fail_allocation = 3U;
    assert(start_controller() == Status::DmaAllocationFailed);
    assert(reg(REG_ASQ) == 0U && reg(REG_ACQ) == 0U);
    expect_all_released();
    assert(command == firmware_command);

    reset_fixture();
    pci_present = true;
    fail_map_page = 1;
    fail_unmap_page = 0;
    assert(initialize() == Status::ResourceReleaseFailed);
    assert(mapped[0] && !mapped[1] && pages_allocated == 0U);
    assert(unmap_attempts[0] == 1U);

    pci_present = false;
    fail_unmap_page = -1;
    assert(initialize() == Status::NoController);
    expect_all_released();
    assert(unmap_attempts[0] == 2U && unmap_attempts[1] == 0U);
    assert(command == firmware_command);
}

void test_release_and_pci_restore_retry() {
    reset_fixture();
    set_running_controller();
    reg(REG_CC) = 0U;
    reg(REG_CSTS) = 0U;
    fail_release_page = 4;
    fail_unmap_page = 0;
    reject_pci_restore = true;

    shutdown();
    assert(live[4] && mapped[0] && !mapped[1]);
    assert(command_writes == 0U);
    assert(initialize() == Status::ResourceReleaseFailed);
    assert(scan_calls == 0U);

    fail_release_page = fail_unmap_page = -1;
    shutdown();
    expect_all_released();
    assert(command == driver_command && command_writes == 1U);
    assert(initialize() == Status::ResourceReleaseFailed);
    assert(scan_calls == 0U);

    reject_pci_restore = false;
    assert(initialize() == Status::NoController);
    assert(command == firmware_command);
    for (size_t index = 0; index < page_count; ++index) {
        assert(release_attempts[index] == (index == 4U ? 3U : 1U));
    }
    assert(unmap_attempts[0] == 3U && unmap_attempts[1] == 1U);
    assert(unregister_count == 1U);
}
} // namespace

namespace pci {
size_t device_count() {
    ++scan_calls;
    return pci_present ? 1U : 0U;
}

const Device* device_at(size_t index) {
    assert(index == 0U && pci_present);
    return &device;
}

uint32_t read32(Address, uint8_t offset) {
    assert(offset == 0U);
    return identity;
}

uint16_t read16(Address, uint8_t offset) {
    assert(offset == 4U);
    if (++command_reads == replace_on_command_read) identity = replacement_identity;
    return command;
}

void write16(Address, uint8_t offset, uint16_t value) {
    assert(offset == 4U && identity == controller_identity);
    ++command_writes;
    if (value != firmware_command || !reject_pci_restore) command = value;
}

namespace bar {
Status probe_disabled(const Device&, uint8_t index, Info* output) {
    assert(index == 0U && (command & COMMAND_DECODE_MASK) == 0U);
    *output = {Kind::Memory64, 0U, 2U, false, physical_bar, MMIO_REQUIRED_BYTES};
    return Status::Ok;
}
} // namespace bar
} // namespace pci

namespace memory::kernel_virtual_memory {
virtual_memory::AddressSpace* address_space() {
    static virtual_memory::AddressSpace address_space{};
    return &address_space;
}
} // namespace memory::kernel_virtual_memory

namespace memory::virtual_memory {
Status query_page(const AddressSpace*, uint64_t address, Mapping*) {
    const size_t index = (address - MMIO_VIRTUAL_BASE) / PAGE_SIZE;
    assert(index < MMIO_PAGE_COUNT);
    return mapped[index] ? Status::Ok : Status::NotMapped;
}

Status map_page(AddressSpace*, uint64_t address, uint64_t physical, MapFlags) {
    const size_t index = (address - MMIO_VIRTUAL_BASE) / PAGE_SIZE;
    assert(index < MMIO_PAGE_COUNT && !mapped[index]);
    assert(physical == physical_bar + index * PAGE_SIZE);
    if (static_cast<int>(index) == fail_map_page) return Status::InvalidArgument;
    mapped[index] = true;
    return Status::Ok;
}

Status unmap_page(AddressSpace*, uint64_t address, Mapping*) {
    const size_t index = (address - MMIO_VIRTUAL_BASE) / PAGE_SIZE;
    assert(index < MMIO_PAGE_COUNT && mapped[index]);
    ++unmap_attempts[index];
    if (static_cast<int>(index) == fail_unmap_page) return Status::InvalidArgument;
    mapped[index] = false;
    return Status::Ok;
}
} // namespace memory::virtual_memory

namespace storage::dma {
Status allocate_page(bool supports_64_bit_addressing, Page* page) {
    assert(supports_64_bit_addressing && pages_allocated < page_count);
    if (pages_allocated + 1U == fail_allocation) return Status::OutOfMemory;
    const size_t index = pages_allocated++;
    live[index] = true;
    *page = {frames[index], reinterpret_cast<uintptr_t>(frames[index]), true};
    if (pages_allocated == 3U && assert_fatal_on_third_allocation) {
        reg(REG_CSTS) = CSTS_FATAL;
    }
    return Status::Ok;
}

Status release_page(Page* page) {
    size_t index = 0U;
    while (index < page_count && page->virtual_address != frames[index]) ++index;
    assert(index < page_count && live[index]);
    ++release_attempts[index];
    if (static_cast<int>(index) == fail_release_page) return Status::ReleaseFailed;
    live[index] = false;
    *page = {};
    return Status::Ok;
}
} // namespace storage::dma

namespace storage::device_registry {
bool register_device(Backend, size_t, const block::Device*, const char*) {
    assert(registry_count == 0U);
    ++registry_count;
    return true;
}

bool unregister_device(const block::Device*) {
    assert(registry_count == 1U);
    --registry_count;
    ++unregister_count;
    return true;
}
} // namespace storage::device_registry

namespace terminal {
void println(const char*) {}
}

int main() {
    test_stuck_ready_retains_then_retires();
    test_invalid_stop_proof();
    test_startup_failure_after_queue_publication();
    test_unpublished_pages_and_mapping_rollback();
    test_release_and_pci_restore_retry();
}
