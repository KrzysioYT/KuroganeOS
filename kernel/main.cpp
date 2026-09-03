#include "include/kernel.hpp"

#include "apps/builtin.hpp"
#include "apps/framework.hpp"
#include "arch/x86_64/acpi.hpp"
#include "arch/x86_64/apic.hpp"
#include "arch/x86_64/gdt.hpp"
#include "arch/x86_64/interrupts.hpp"
#include "arch/x86_64/io.hpp"
#include "core/string.hpp"
#include "core/log.hpp"
#include "drivers/keyboard.hpp"
#include "drivers/mouse.hpp"
#include "drivers/framebuffer.hpp"
#include "drivers/core/device_manager.hpp"
#include "drivers/core/driver_manager.hpp"
#include "drivers/pci.hpp"
#include "drivers/pic.hpp"
#include "drivers/pit.hpp"
#include "drivers/serial.hpp"
#include "drivers/usb/xhci.hpp"
#include "fs/root_volume.hpp"
#include "fs/ramfs.hpp"
#include "memory/allocator.hpp"
#include "memory/kernel_virtual_memory.hpp"
#include "memory/physical_memory.hpp"
#include "input/input.hpp"
#include "install/installer.hpp"
#include "net/service.hpp"
#include "shell/shell.hpp"
#include "storage/ahci.hpp"
#include "storage/gpt.hpp"
#include "storage/scratch_test.hpp"
#include "task/scheduler.hpp"
#include "task/process.hpp"
#include "task/thread.hpp"
#include "terminal.hpp"
#include "user/console.hpp"
#include "user/runtime.hpp"
#include "../common/version.h"

extern "C" unsigned char kernel_stack_bottom[];
extern "C" unsigned char kernel_stack_top[];
extern "C" bool kurogane_start_desktop_session();

namespace {

constexpr size_t kKernelHeapSize = 2 * 1024 * 1024;
alignas(64) uint8_t g_kernel_heap[kKernelHeapSize];
alignas(64) storage::scratch_test::Workspace g_scratch_test_workspace{};
drivers::device::DriverId g_ahci_driver_id =
    drivers::device::INVALID_DRIVER_ID;
drivers::device::DriverId g_xhci_driver_id =
    drivers::device::INVALID_DRIVER_ID;
bool g_required_runtime_test_failed = false;

struct KernelThreadProbe {
    char trace[4];
    size_t trace_size;
    uintptr_t first_local;
    uintptr_t second_local;
    bool yield_ok;
};

KernelThreadProbe g_thread_probe{};

struct PreemptionProbe {
    volatile uint64_t first_iterations;
    volatile uint64_t second_iterations;
    volatile bool first_started;
    volatile bool second_started;
    volatile bool first_observed_second;
    volatile bool second_observed_first;
    uint64_t deadline;
};

PreemptionProbe g_preemption_probe{};

struct BootContext {
    KuroganeFramebuffer framebuffer;
    const KuroganeBootInfo* boot_info;
    bool safe_mode;
    bool force_desktop;
    bool diagnostics;
    bool installer;
};

bool valid_framebuffer(const KuroganeFramebuffer& framebuffer) {
    if (!framebuffer.base || framebuffer.width == 0 ||
        framebuffer.height == 0 ||
        framebuffer.width > static_cast<uint32_t>(INT32_MAX) ||
        framebuffer.height > static_cast<uint32_t>(INT32_MAX) ||
        framebuffer.width > UINT32_MAX / sizeof(uint32_t) ||
        framebuffer.bpp != 32 ||
        (framebuffer.pitch & (alignof(uint32_t) - 1u)) != 0 ||
        framebuffer.pitch < framebuffer.width * sizeof(uint32_t) ||
        (framebuffer.pixel_format != KUROGANE_PIXEL_BGRX8 &&
         framebuffer.pixel_format != KUROGANE_PIXEL_RGBX8)) {
        return false;
    }

    const uintptr_t base =
        reinterpret_cast<uintptr_t>(framebuffer.base);
    if ((base & (alignof(uint32_t) - 1u)) != 0) {
        return false;
    }
    if (framebuffer.height >
        UINT64_MAX / static_cast<uint64_t>(framebuffer.pitch)) {
        return false;
    }
    const uint64_t extent =
        static_cast<uint64_t>(framebuffer.height) * framebuffer.pitch;
    return extent <= UINTPTR_MAX &&
           base <= UINTPTR_MAX - static_cast<uintptr_t>(extent);
}

bool valid_memory_map(const KuroganeBootInfo& boot_info) {
    if (!boot_info.memory_regions ||
        boot_info.memory_region_count == 0 ||
        boot_info.memory_region_count > KUROGANE_MAX_MEMORY_REGIONS ||
        boot_info.memory_region_count >
            static_cast<uint64_t>(
                SIZE_MAX / sizeof(KuroganeMemoryRegion))) {
        return false;
    }

    const uintptr_t map_address =
        reinterpret_cast<uintptr_t>(boot_info.memory_regions);
    const size_t map_bytes =
        static_cast<size_t>(boot_info.memory_region_count) *
        sizeof(KuroganeMemoryRegion);
    if ((map_address & (alignof(KuroganeMemoryRegion) - 1u)) != 0 ||
        map_address > UINTPTR_MAX - map_bytes) {
        return false;
    }

    for (uint64_t index = 0;
         index < boot_info.memory_region_count; ++index) {
        const KuroganeMemoryRegion& region =
            boot_info.memory_regions[index];
        if (region.page_count == 0 || region.reserved != 0 ||
            region.type > KUROGANE_MEMORY_FRAMEBUFFER ||
            (region.physical_start & (KUROGANE_PAGE_SIZE - 1u)) != 0 ||
            region.page_count > UINT64_MAX / KUROGANE_PAGE_SIZE) {
            return false;
        }
        const uint64_t region_bytes =
            region.page_count * KUROGANE_PAGE_SIZE;
        if (region.physical_start > UINT64_MAX - region_bytes) {
            return false;
        }
        const uint64_t region_end =
            region.physical_start + region_bytes;
        if (region.type == KUROGANE_MEMORY_USABLE &&
            boot_info.kernel_physical_start < region_end &&
            region.physical_start < boot_info.kernel_physical_end) {
            return false;
        }
    }
    return true;
}

bool parse_boot_argument(void* argument, BootContext& context) {
    context = {};
    if (!argument) {
        return false;
    }

    const auto* boot_info = static_cast<const KuroganeBootInfo*>(argument);
    if (boot_info->magic != KUROGANE_BOOT_MAGIC ||
        boot_info->version != KUROGANE_BOOT_PROTOCOL_VERSION ||
        boot_info->size < sizeof(KuroganeBootInfo) ||
        boot_info->kernel_physical_start == 0 ||
        boot_info->kernel_physical_start >=
            boot_info->kernel_physical_end ||
        (boot_info->kernel_physical_start &
         (KUROGANE_PAGE_SIZE - 1u)) != 0 ||
        (boot_info->kernel_physical_end &
         (KUROGANE_PAGE_SIZE - 1u)) != 0 ||
        (boot_info->flags & ~KUROGANE_BOOT_KNOWN_FLAGS) != 0 ||
        !valid_framebuffer(boot_info->framebuffer) ||
        !valid_memory_map(*boot_info)) {
        return false;
    }

    const uint64_t flags = boot_info->flags;
    const bool installer =
        (flags & KUROGANE_BOOT_FLAG_INSTALLER) != 0;
    const uintptr_t package_address = reinterpret_cast<uintptr_t>(
        boot_info->installation_package);
    if (installer) {
        if (package_address == 0 ||
            (package_address & (KUROGANE_PAGE_SIZE - 1U)) != 0 ||
            boot_info->installation_package_size == 0 ||
            boot_info->installation_package_size > 16U * 1024U * 1024U ||
            package_address > UINTPTR_MAX -
                static_cast<uintptr_t>(boot_info->installation_package_size)) {
            return false;
        }
    } else if (package_address != 0 ||
               boot_info->installation_package_size != 0) {
        return false;
    }
    context.framebuffer = boot_info->framebuffer;
    context.boot_info = boot_info;
    context.safe_mode =
        (flags & KUROGANE_BOOT_FLAG_SAFE_MODE) != 0 ||
        (flags & KUROGANE_BOOT_FLAG_DIAGNOSTICS) != 0;
    context.force_desktop =
        (flags & KUROGANE_BOOT_FLAG_FORCE_DESKTOP) != 0;
    context.diagnostics =
        (flags & KUROGANE_BOOT_FLAG_DIAGNOSTICS) != 0;
    context.installer = installer;
    return true;
}

void print_banner(bool safe_mode, bool diagnostics, bool installer) {
    terminal::set_colors(0x00F97316, 0x000C1018);
    terminal::println("KUROGANE OS");
    terminal::reset_colors();
    terminal::println(
        "x86-64 UEFI kernel " KUROGANE_VERSION_STRING);
    terminal::println(
        "boot protocol: v3 (memory map + boot flags + installer payload)");
    if (installer) {
        terminal::println("INSTALLER MODE: explicit virtual-disk deployment");
    } else if (diagnostics) {
        terminal::println(
            "DIAGNOSTICS MODE: limited init and diagnostic shell");
    } else if (safe_mode) {
        terminal::println("SAFE MODE: minimal drivers and diagnostic shell");
    }
}

bool initialize_physical_memory(const KuroganeBootInfo* boot_info) {
    if (!boot_info || !boot_info->memory_regions ||
        boot_info->memory_region_count == 0) {
        return false;
    }

    const KuroganeMemoryRegion* best = nullptr;
    uint64_t best_pages = 0;
    const uint64_t capacity = memory::static_bitmap_capacity_frames();
    for (uint64_t index = 0; index < boot_info->memory_region_count; ++index) {
        const auto& region = boot_info->memory_regions[index];
        if (region.type != KUROGANE_MEMORY_USABLE ||
            region.page_count == 0 ||
            (region.physical_start & (KUROGANE_PAGE_SIZE - 1)) != 0) {
            continue;
        }
        const uint64_t usable_pages =
            region.page_count < capacity ? region.page_count : capacity;
        if (usable_pages > best_pages) {
            best = &region;
            best_pages = usable_pages;
        }
    }
    if (!best || best_pages == 0 ||
        best_pages > static_cast<uint64_t>(SIZE_MAX / KUROGANE_PAGE_SIZE)) {
        return false;
    }

    memory::init_physical_memory(
        static_cast<uintptr_t>(best->physical_start),
        static_cast<size_t>(best_pages * KUROGANE_PAGE_SIZE),
        static_cast<size_t>(KUROGANE_PAGE_SIZE));
    return memory::physical_memory_initialized();
}

bool memory_self_test() {
    const size_t before = memory::used_bytes();
    void* first = memory::kmalloc(31, 16);
    void* second = memory::kmalloc(79, 64);
    const bool heap_ok =
        first && second && first != second &&
        (reinterpret_cast<uintptr_t>(first) & 15u) == 0 &&
        (reinterpret_cast<uintptr_t>(second) & 63u) == 0;
    memory::kfree(second);
    memory::kfree(first);
    const bool heap_recovered = memory::used_bytes() == before;

    bool physical_ok = memory::physical_memory_initialized();
    void* frame_a = physical_ok ? memory::alloc_frame() : nullptr;
    void* frame_b = physical_ok ? memory::alloc_frame() : nullptr;
    physical_ok = frame_a && frame_b && frame_a != frame_b;
    if (frame_b) memory::free_frame(frame_b);
    if (frame_a) memory::free_frame(frame_a);
    return heap_ok && heap_recovered && physical_ok;
}

bool seed_filesystem() {
    if (fs::initialize_ramfs() != fs::Status::Ok) {
        return false;
    }
    const char* const directories[] = {
        "/system", "/home", "/apps", "/etc", "/tmp", "/var"
    };
    for (const char* directory : directories) {
        const auto status = fs::create_directory_at(directory);
        if (status != fs::Status::Ok &&
            status != fs::Status::AlreadyExists) {
            return false;
        }
    }

    constexpr char version[] = KUROGANE_PRODUCT_STRING "\n";
    if (fs::write_file_data(
            "/system/version", version, sizeof(version) - 1, true) !=
        fs::Status::Ok) {
        return false;
    }
    constexpr char readme[] =
        "Welcome to KuroganeOS.\n"
        "Type 'help' to list the available console commands.\n";
    return fs::write_file_data(
               "/home/readme.txt", readme, sizeof(readme) - 1, true) ==
           fs::Status::Ok;
}

[[noreturn]] void boot_failure(const char* module, const char* reason);

drivers::device::Type pci_device_type(const pci::Device& device) {
    switch (device.class_code) {
        case 0x01: return drivers::device::Type::StorageController;
        case 0x02: return drivers::device::Type::Network;
        case 0x03: return drivers::device::Type::Display;
        case 0x06: return drivers::device::Type::Bridge;
        case 0x0C:
            return device.subclass == 0x03
                ? drivers::device::Type::UsbController
                : drivers::device::Type::Unknown;
        default: return drivers::device::Type::Unknown;
    }
}

bool ahci_driver_match(const drivers::device::Device& device, void*) {
    return device.bus == drivers::device::Bus::Pci &&
        device.class_code == 0x01 && device.subclass == 0x06 &&
        device.programming_interface == 0x01;
}

void register_platform_devices() {
    const auto* topology = arch::x86_64::acpi::topology();
    if (topology == nullptr) return;

    drivers::device::DeviceId platform =
        drivers::device::INVALID_DEVICE_ID;
    const drivers::device::Descriptor platform_descriptor{
        drivers::device::Type::Platform,
        drivers::device::Bus::Platform,
        "ACPI MADT platform",
        0U, 0U, 0U, 0U, 0U, {},
        drivers::device::INVALID_DEVICE_ID,
        nullptr, 0U,
    };
    if (drivers::device::register_device(platform_descriptor, &platform) !=
        KStatus::Ok) {
        log::write(log::Level::Warn, "DEVICE", "ACPI platform registration failed");
        return;
    }

    for (size_t index = 0U; index < topology->processor_count; ++index) {
        const drivers::device::Resource resource{
            drivers::device::ResourceType::Irq,
            topology->processors[index].apic_id,
            1U,
            topology->processors[index].flags,
        };
        const drivers::device::Descriptor descriptor{
            drivers::device::Type::Processor,
            drivers::device::Bus::Platform,
            "ACPI Local APIC processor",
            0U, topology->processors[index].apic_id,
            0U, 0U, 0U,
            {0U, 0U, topology->processors[index].acpi_id, 0U},
            platform, &resource, 1U,
        };
        drivers::device::DeviceId id = drivers::device::INVALID_DEVICE_ID;
        static_cast<void>(drivers::device::register_device(descriptor, &id));
    }
    for (size_t index = 0U; index < topology->io_apic_count; ++index) {
        const drivers::device::Resource resource{
            drivers::device::ResourceType::Mmio,
            topology->io_apics[index].address,
            memory::virtual_memory::PAGE_SIZE,
            topology->io_apics[index].global_interrupt_base,
        };
        const drivers::device::Descriptor descriptor{
            drivers::device::Type::Platform,
            drivers::device::Bus::Platform,
            "ACPI I/O APIC",
            0U, topology->io_apics[index].id,
            0U, 0U, 0U,
            {0U, 0U, topology->io_apics[index].id, 0U},
            platform, &resource, 1U,
        };
        drivers::device::DeviceId id = drivers::device::INVALID_DEVICE_ID;
        static_cast<void>(drivers::device::register_device(descriptor, &id));
    }
    if (topology->legacy_pic_present) {
        const drivers::device::Resource resources[] = {
            {drivers::device::ResourceType::IoPort, 0x20U, 2U, 0U},
            {drivers::device::ResourceType::IoPort, 0xA0U, 2U, 0U},
        };
        const drivers::device::Descriptor descriptor{
            drivers::device::Type::Platform,
            drivers::device::Bus::Platform,
            "8259 PIC fallback",
            0U, 0U, 0U, 0U, 0U, {}, platform,
            resources, sizeof(resources) / sizeof(resources[0]),
        };
        drivers::device::DeviceId id = drivers::device::INVALID_DEVICE_ID;
        static_cast<void>(drivers::device::register_device(descriptor, &id));
    }
}

KStatus ahci_driver_probe(
    const drivers::device::Device& device,
    uint32_t timeout_ticks,
    void*) {
    if (timeout_ticks == 0 || !ahci_driver_match(device, nullptr)) {
        return KStatus::NotSupported;
    }
    return KStatus::Ok;
}

KStatus ahci_driver_attach(
    const drivers::device::Device&,
    uint32_t timeout_ticks,
    void*) {
    if (timeout_ticks == 0) {
        return KStatus::InvalidArgument;
    }
    const storage::ahci::Status status = storage::ahci::initialize();
    if (status == storage::ahci::Status::Ok ||
        status == storage::ahci::Status::AlreadyInitialized ||
        (status == storage::ahci::Status::NoSataDevice &&
         storage::ahci::active_controller_count() != 0)) {
        return KStatus::Ok;
    }
    switch (status) {
        case storage::ahci::Status::NoController:
        case storage::ahci::Status::NoSataDevice:
            return KStatus::NoDevice;
        case storage::ahci::Status::BiosHandoffTimeout:
        case storage::ahci::Status::ControllerResetTimeout:
        case storage::ahci::Status::PortStopTimeout:
        case storage::ahci::Status::PortStartTimeout:
        case storage::ahci::Status::DeviceBusyTimeout:
        case storage::ahci::Status::CommandTimeout:
            return KStatus::Timeout;
        case storage::ahci::Status::InvalidArgument:
            return KStatus::InvalidArgument;
        case storage::ahci::Status::DmaAllocationFailed:
            return KStatus::NoMemory;
        default:
            return KStatus::DeviceFault;
    }
}

bool xhci_driver_match(const drivers::device::Device& device, void*) {
    return device.bus == drivers::device::Bus::Pci &&
        device.class_code == 0x0CU && device.subclass == 0x03U &&
        device.programming_interface == 0x30U;
}

KStatus xhci_driver_probe(
    const drivers::device::Device& device,
    uint32_t timeout_ticks,
    void*) {
    if (timeout_ticks == 0U || !xhci_driver_match(device, nullptr)) {
        return KStatus::NotSupported;
    }
    const pci::Device* source = nullptr;
    for (size_t index = 0U; index < pci::device_count(); ++index) {
        const pci::Device* candidate = pci::device_at(index);
        if (candidate != nullptr &&
            candidate->address.bus == device.bus_address.bus &&
            candidate->address.slot == device.bus_address.slot &&
            candidate->address.function == device.bus_address.function) {
            source = candidate;
            break;
        }
    }
    if (source == nullptr) return KStatus::NoDevice;
    bool is_io = false;
    return pci::bar_address(*source, 0U, &is_io) != 0U && !is_io
        ? KStatus::Ok
        : KStatus::NotSupported;
}

KStatus xhci_driver_attach(
    const drivers::device::Device& device,
    uint32_t timeout_ticks,
    void*) {
    if (timeout_ticks == 0U) return KStatus::InvalidArgument;
    const pci::Device* source = nullptr;
    for (size_t index = 0U; index < pci::device_count(); ++index) {
        const pci::Device* candidate = pci::device_at(index);
        if (candidate != nullptr &&
            candidate->address.bus == device.bus_address.bus &&
            candidate->address.slot == device.bus_address.slot &&
            candidate->address.function == device.bus_address.function) {
            source = candidate;
            break;
        }
    }
    if (source == nullptr) return KStatus::NoDevice;
    const auto status = drivers::usb::xhci::initialize(
        *source, device.id, device.driver);
    if (status != drivers::usb::xhci::Status::Ok &&
        status != drivers::usb::xhci::Status::AlreadyInitialized) {
        log::write(
            log::Level::Warn,
            "XHCI",
            drivers::usb::xhci::status_message(status));
    }
    switch (status) {
        case drivers::usb::xhci::Status::Ok:
        case drivers::usb::xhci::Status::AlreadyInitialized:
            return KStatus::Ok;
        case drivers::usb::xhci::Status::NoDevice:
        case drivers::usb::xhci::Status::HidKeyboardNotFound:
            return KStatus::NoDevice;
        case drivers::usb::xhci::Status::BiosHandoffTimeout:
        case drivers::usb::xhci::Status::ControllerResetTimeout:
        case drivers::usb::xhci::Status::ControllerStartTimeout:
        case drivers::usb::xhci::Status::PortResetTimeout:
            return KStatus::Timeout;
        case drivers::usb::xhci::Status::DmaAllocationFailed:
            return KStatus::NoMemory;
        case drivers::usb::xhci::Status::InvalidArgument:
            return KStatus::InvalidArgument;
        case drivers::usb::xhci::Status::UnsupportedController:
            return KStatus::NotSupported;
        case drivers::usb::xhci::Status::DeviceRegistrationFailed:
            return KStatus::BadState;
        default:
            return KStatus::DeviceFault;
    }
}

void initialize_device_framework(bool safe_mode) {
    if (drivers::device::initialize() != KStatus::Ok ||
        drivers::driver::initialize() != KStatus::Ok) {
        boot_failure("DEVICE", "device framework initialization failed");
    }
    register_platform_devices();
    if (safe_mode) {
        log::write(
            log::Level::Info,
            "DEVICE",
            "safe mode: PCI device registration skipped");
        return;
    }

    for (size_t index = 0; index < pci::device_count(); ++index) {
        const pci::Device* source = pci::device_at(index);
        if (source == nullptr) {
            continue;
        }
        const drivers::device::Descriptor descriptor{
            pci_device_type(*source),
            drivers::device::Bus::Pci,
            "PCI device",
            source->vendor_id,
            source->device_id,
            source->class_code,
            source->subclass,
            source->programming_interface,
            {0, source->address.bus, source->address.slot,
             source->address.function},
            drivers::device::INVALID_DEVICE_ID,
            nullptr,
            0,
        };
        drivers::device::DeviceId id = drivers::device::INVALID_DEVICE_ID;
        const KStatus status = drivers::device::register_device(descriptor, &id);
        if (status != KStatus::Ok) {
            log::write(
                log::Level::Warn,
                "DEVICE",
                "failed to register enumerated PCI function");
        }
    }

    const drivers::driver::Descriptor ahci_driver{
        "ahci",
        100,
        500,
        ahci_driver_match,
        ahci_driver_probe,
        ahci_driver_attach,
        nullptr,
        nullptr,
    };
    if (drivers::driver::register_driver(
            ahci_driver, &g_ahci_driver_id) != KStatus::Ok) {
        log::write(
            log::Level::Warn,
            "DRIVER",
            "AHCI driver registration failed");
    }
    const drivers::driver::Descriptor xhci_driver{
        "xhci",
        110,
        2000,
        xhci_driver_match,
        xhci_driver_probe,
        xhci_driver_attach,
        nullptr,
        nullptr,
    };
    if (drivers::driver::register_driver(
            xhci_driver, &g_xhci_driver_id) != KStatus::Ok) {
        log::write(
            log::Level::Warn,
            "DRIVER",
            "xHCI driver registration failed");
    }
    const KStatus bind_status = drivers::driver::bind_all();
    if (bind_status != KStatus::Ok && bind_status != KStatus::NoDevice) {
        log::write(
            log::Level::Warn,
            "DRIVER",
            kstatus_message(bind_status));
    }
    log::write_u64(
        log::Level::Info,
        "DEVICE",
        "registered devices=",
        drivers::device::count());
}

void initialize_platform_discovery(const KuroganeBootInfo* boot_info) {
    const uint64_t rsdp = boot_info != nullptr ? boot_info->rsdp_address : 0U;
    const auto acpi_status = arch::x86_64::acpi::discover(rsdp);
    if (acpi_status != arch::x86_64::acpi::Status::Ok) {
        log::write(
            log::Level::Warn,
            "ACPI",
            arch::x86_64::acpi::status_message(acpi_status));
        terminal::println("[TEST] acpi_madt: DEGRADED (PIC fallback)");
        return;
    }
    const auto* topology = arch::x86_64::acpi::topology();
    log::write_u64(
        log::Level::Info, "ACPI", "MADT processors=",
        topology != nullptr ? topology->processor_count : 0U);
    log::write_u64(
        log::Level::Info, "ACPI", "MADT I/O APICs=",
        topology != nullptr ? topology->io_apic_count : 0U);
    terminal::println("[TEST] acpi_madt: PASS");

    const auto apic_status = topology != nullptr
        ? arch::x86_64::apic::prepare(*topology)
        : arch::x86_64::apic::Status::InvalidTopology;
    if (apic_status == arch::x86_64::apic::Status::Ok) {
        log::write_u64(
            log::Level::Info, "APIC", "Local APIC id=",
            arch::x86_64::apic::local_apic_id());
        log::write_u64(
            log::Level::Info, "APIC", "mapped I/O APICs=",
            arch::x86_64::apic::io_apic_count());
        terminal::println("[TEST] apic_discovery: PASS (PIC fallback active)");
    } else {
        log::write(
            log::Level::Warn,
            "APIC",
            arch::x86_64::apic::status_message(apic_status));
        terminal::println("[TEST] apic_discovery: DEGRADED (PIC fallback)");
    }
}

bool run_fat32_persistence_probe() {
    constexpr char path[] = "/var/persist.dat";
    constexpr char payload[] =
        "KuroganeOS persistent FAT32 through VFS and AHCI v1\n";
    const auto report_failure = [](const char* stage, fs::vfs::Status status) {
        log::write(log::Level::Error, "PERSIST", stage);
        log::write(log::Level::Error, "PERSIST", fs::vfs::status_message(status));
    };
    fs::vfs::FileStat info{};
    const fs::vfs::Status stat_status = fs::root_volume::stat(path, &info);
    if (stat_status == fs::vfs::Status::NotFound) {
        const fs::vfs::Status create_status = fs::root_volume::create(path);
        if (create_status != fs::vfs::Status::Ok) {
            report_failure("create", create_status);
            return false;
        }
        fs::vfs::OpenFileHandle handle{};
        const fs::vfs::Status open_status = fs::root_volume::open(
            path, fs::vfs::OpenFlags::Write, &handle);
        if (open_status != fs::vfs::Status::Ok) {
            report_failure("open", open_status);
            return false;
        }
        size_t bytes_written = 0U;
        const fs::vfs::Status write_status = fs::root_volume::write(
            handle, payload, sizeof(payload) - 1U, &bytes_written);
        const fs::vfs::Status close_status = fs::root_volume::close(handle);
        const fs::vfs::Status sync_status = fs::root_volume::sync();
        if (write_status != fs::vfs::Status::Ok) {
            report_failure("write", write_status);
            return false;
        }
        if (close_status != fs::vfs::Status::Ok) {
            report_failure("close", close_status);
            return false;
        }
        if (bytes_written != sizeof(payload) - 1U) {
            report_failure("write byte count", fs::vfs::Status::IoError);
            return false;
        }
        if (sync_status != fs::vfs::Status::Ok) {
            report_failure("sync", sync_status);
            return false;
        }
        char restored[sizeof(payload)]{};
        size_t bytes_read = 0U;
        const fs::vfs::Status read_status = fs::root_volume::read_file(
            path, restored, sizeof(restored), &bytes_read);
        if (read_status != fs::vfs::Status::Ok) {
            report_failure("readback", read_status);
            return false;
        }
        if (bytes_read != sizeof(payload) - 1U) {
            report_failure("readback byte count", fs::vfs::Status::IoError);
            return false;
        }
        for (size_t index = 0U; index < bytes_read; ++index) {
            if (restored[index] != payload[index]) {
                log::write_u64(log::Level::Error, "PERSIST", "comparison index=", index);
                report_failure("readback comparison", fs::vfs::Status::IoError);
                return false;
            }
        }
        terminal::println("[TEST] fat32_persistence_prepare: PASS");
        return true;
    }
    if (stat_status != fs::vfs::Status::Ok ||
        info.type != fs::vfs::NodeType::Regular ||
        info.size != sizeof(payload) - 1U) {
        report_failure("verify stat", stat_status == fs::vfs::Status::Ok
            ? fs::vfs::Status::IoError : stat_status);
        return false;
    }
    char restored[sizeof(payload)]{};
    size_t bytes_read = 0U;
    const fs::vfs::Status read_status = fs::root_volume::read_file(
        path, restored, sizeof(restored), &bytes_read);
    if (read_status != fs::vfs::Status::Ok) {
        report_failure("verify readback", read_status);
        return false;
    }
    if (bytes_read != sizeof(payload) - 1U) {
        report_failure("verify readback byte count", fs::vfs::Status::IoError);
        return false;
    }
    for (size_t index = 0U; index < bytes_read; ++index) {
        if (restored[index] != payload[index]) {
            log::write_u64(log::Level::Error, "PERSIST", "verify comparison index=", index);
            report_failure("verify readback comparison", fs::vfs::Status::IoError);
            return false;
        }
    }
    terminal::println("[TEST] fat32_persistence_verify: PASS");
    return true;
}

bool handle_installed_first_boot() {
    if (!fs::root_volume::mounted()) return true;
    static constexpr char kPendingPath[] = "/etc/first.run";
    static constexpr char kCompletePath[] = "/etc/install.ok";
    static constexpr char kCompletePayload[] =
        "VERSION=2.0\nFIRST_BOOT=complete\n";
    fs::vfs::FileStat pending{};
    const fs::vfs::Status pending_status =
        fs::root_volume::stat(kPendingPath, &pending);
    fs::vfs::FileStat complete{};
    const fs::vfs::Status complete_status =
        fs::root_volume::stat(kCompletePath, &complete);

    if (pending_status != fs::vfs::Status::Ok) {
        if (pending_status != fs::vfs::Status::NotFound) return false;
        if (complete_status == fs::vfs::Status::NotFound) return true;
        if (complete_status != fs::vfs::Status::Ok) return false;
        char restored[sizeof(kCompletePayload)]{};
        size_t bytes_read = 0U;
        if (fs::root_volume::read_file(
                kCompletePath, restored, sizeof(restored), &bytes_read) !=
                fs::vfs::Status::Ok ||
            bytes_read != sizeof(kCompletePayload) - 1U) {
            return false;
        }
        for (size_t index = 0U; index < bytes_read; ++index) {
            if (restored[index] != kCompletePayload[index]) return false;
        }
        terminal::println("[TEST] installed_persistence: PASS");
        return true;
    }
    if (pending.type != fs::vfs::NodeType::Regular) return false;

    if (complete_status == fs::vfs::Status::NotFound) {
        if (fs::root_volume::create(kCompletePath) != fs::vfs::Status::Ok) {
            return false;
        }
        fs::vfs::OpenFileHandle handle{};
        if (fs::root_volume::open(
                kCompletePath, fs::vfs::OpenFlags::Write, &handle) !=
            fs::vfs::Status::Ok) {
            return false;
        }
        size_t bytes_written = 0U;
        const fs::vfs::Status write_status = fs::root_volume::write(
            handle, kCompletePayload, sizeof(kCompletePayload) - 1U,
            &bytes_written);
        const fs::vfs::Status close_status = fs::root_volume::close(handle);
        if (write_status != fs::vfs::Status::Ok ||
            close_status != fs::vfs::Status::Ok ||
            bytes_written != sizeof(kCompletePayload) - 1U ||
            fs::root_volume::sync() != fs::vfs::Status::Ok) {
            return false;
        }
    } else if (complete_status != fs::vfs::Status::Ok) {
        return false;
    }
    if (fs::root_volume::unlink(kPendingPath) != fs::vfs::Status::Ok ||
        fs::root_volume::sync() != fs::vfs::Status::Ok) {
        return false;
    }
    terminal::println("[TEST] installed_first_boot: PASS");
    return true;
}

void initialize_storage_probe() {
    const storage::ahci::Status ahci_status =
        storage::ahci::initialization_attempted()
        ? storage::ahci::initialization_status()
        : storage::ahci::initialize();
    log::write_u64(
        log::Level::Info,
        "AHCI",
        "PCI AHCI controllers detected=",
        storage::ahci::detected_controller_count());
    log::write_u64(
        log::Level::Info,
        "AHCI",
        "active AHCI controllers=",
        storage::ahci::active_controller_count());
    log::write_u64(
        log::Level::Info,
        "AHCI",
        "registered SATA block devices=",
        storage::ahci::device_count());
    if (ahci_status != storage::ahci::Status::Ok &&
        ahci_status != storage::ahci::Status::NoController) {
        log::write(
            log::Level::Warn,
            "AHCI",
            storage::ahci::status_message(ahci_status));
    }

    for (size_t index = 0U;
         index < storage::ahci::device_count();
         ++index) {
        const storage::ahci::DeviceInfo* const info =
            storage::ahci::device_info_at(index);
        const storage::block::Device* const device =
            storage::ahci::device_at(index);
        if (info == nullptr || device == nullptr) {
            continue;
        }
        const drivers::device::Device* parent =
            drivers::device::find_pci(
                info->pci_bus, info->pci_slot, info->pci_function);
        const drivers::device::Descriptor block_descriptor{
            drivers::device::Type::Block,
            drivers::device::Bus::Virtual,
            info->model[0] != '\0' ? info->model : "SATA disk",
            0,
            0,
            0x01,
            0,
            0,
            {0, info->pci_bus, info->pci_slot, info->pci_function},
            parent != nullptr
                ? parent->id
                : drivers::device::INVALID_DEVICE_ID,
            nullptr,
            0,
        };
        drivers::device::DeviceId block_id =
            drivers::device::INVALID_DEVICE_ID;
        if (drivers::device::register_device(block_descriptor, &block_id) ==
            KStatus::Ok) {
            if (g_ahci_driver_id != drivers::device::INVALID_DRIVER_ID) {
                static_cast<void>(drivers::device::claim(
                    block_id, g_ahci_driver_id, "ahci"));
            }
            static_cast<void>(drivers::device::set_status(
                block_id, drivers::device::Status::Ready));
        }
        log::write(
            log::Level::Info,
            "AHCI",
            info->model[0] != '\0' ? info->model : "unnamed ATA disk");
        log::write_u64(
            log::Level::Info,
            "AHCI",
            "logical sector bytes=",
            info->sector_size);
        log::write_u64(
            log::Level::Info,
            "AHCI",
            "logical sector count=",
            info->sector_count);

        storage::gpt::Table table{};
        const storage::gpt::ParseResult gpt_result =
            storage::gpt::parse_primary(device, &table);
        if (gpt_result.status == storage::gpt::Status::Ok) {
            log::write_u64(
                log::Level::Info,
                "GPT",
                "validated primary GPT partitions=",
                table.partition_count);
            if (!fs::root_volume::mounted() &&
                !fs::root_volume::initialization_attempted()) {
                const fs::root_volume::Status root_status =
                    fs::root_volume::initialize(device, &table);
                if (root_status == fs::root_volume::Status::Ok) {
                    log::write(
                        log::Level::Info,
                        "VFS",
                        "persistent FAT32 root mounted read-write");
                    log::write_u64(
                        log::Level::Info,
                        "VFS",
                        "/etc/system.cfg bytes=",
                        fs::root_volume::configuration_size());
                    terminal::println("[TEST] fat32_vfs_read: PASS");
                    if (!handle_installed_first_boot()) {
                        terminal::println("[TEST] installed_first_boot: FAIL");
                        g_required_runtime_test_failed = true;
                    }
                    if (!run_fat32_persistence_probe()) {
                        terminal::println(
                            "[TEST] fat32_persistence: FAIL");
                        g_required_runtime_test_failed = true;
                    }
                } else if (root_status !=
                           fs::root_volume::Status::RootPartitionNotFound) {
                    log::write(
                        log::Level::Warn,
                        "VFS",
                        fs::root_volume::status_message(root_status));
                    log::write(
                        log::Level::Warn,
                        "VFS",
                        fs::root_volume::detail_message());
                    terminal::println("[TEST] fat32_vfs_read: FAIL");
                    g_required_runtime_test_failed = true;
                }
            }
        } else {
            log::write(
                log::Level::Info,
                "GPT",
                storage::gpt::status_message(gpt_result.status));
        }

        const storage::scratch_test::Result scratch_result =
            storage::scratch_test::run(
                device, &g_scratch_test_workspace);
        if (scratch_result.status ==
            storage::scratch_test::Status::NotTagged) {
            continue;
        }
        if (scratch_result.status == storage::scratch_test::Status::Ok &&
            scratch_result.write_attempted && scratch_result.restored) {
            log::write(
                log::Level::Info,
                "AHCI",
                "tagged scratch write/flush/readback/restore passed");
            terminal::println(
                "[TEST] ahci_write_flush_readback_restore: PASS");
            continue;
        }

        log::write(
            log::Level::Error,
            "AHCI",
            storage::scratch_test::status_message(scratch_result.status));
        log::write(
            log::Level::Error,
            "AHCI",
            storage::block::status_message(scratch_result.block_status));
        terminal::println(
            "[TEST] ahci_write_flush_readback_restore: FAIL");
        boot_failure("AHCI", "tagged scratch storage test failed");
    }
}

void run_userland_probe() {
    if (!fs::root_volume::mounted()) {
        log::write(
            log::Level::Info,
            "USER",
            "persistent root unavailable; ring-3 image probe skipped");
        return;
    }

    user::runtime::Result hello{};
    const user::runtime::Status hello_status =
        user::runtime::run("/apps/hello", &hello);
    const bool hello_ok = hello_status == user::runtime::Status::Ok &&
        hello.exit_code == 0 && hello.fault_vector == 0xFFU &&
        hello.bytes_written != 0U && hello.entered_ring3 &&
        hello.invalid_pointer_rejected && hello.resources_reclaimed;
    terminal::println(
        hello_ok ? "[TEST] ring3_hello_syscalls: PASS"
                 : "[TEST] ring3_hello_syscalls: FAIL");
    if (!hello_ok) {
        log::write(
            log::Level::Error,
            "USER",
            user::runtime::status_message(hello_status));
        boot_failure("USER", "ring-3 hello/syscall proof failed");
    }

    user::runtime::Result bad{};
    const user::runtime::Status bad_status =
        user::runtime::run("/apps/bad", &bad);
    const bool bad_ok = bad_status == user::runtime::Status::Ok &&
        bad.exit_code == 142 && bad.fault_vector == 14U &&
        bad.entered_ring3 && bad.fault_isolated &&
        bad.resources_reclaimed;
    terminal::println(
        bad_ok ? "[TEST] ring3_fault_isolation: PASS"
               : "[TEST] ring3_fault_isolation: FAIL");
    if (!bad_ok) {
        log::write(
            log::Level::Error,
            "USER",
            user::runtime::status_message(bad_status));
        boot_failure("USER", "ring-3 fault isolation proof failed");
    }
}

void kernel_thread_probe_first(void*) {
    uint64_t stack_local = UINT64_C(0xA1);
    g_thread_probe.first_local = reinterpret_cast<uintptr_t>(&stack_local);
    g_thread_probe.trace[g_thread_probe.trace_size++] = 'A';
    if (threading::yield() != threading::Status::Ok) {
        g_thread_probe.yield_ok = false;
    }
    g_thread_probe.trace[g_thread_probe.trace_size++] = 'C';
}

void kernel_thread_probe_second(void*) {
    uint64_t stack_local = UINT64_C(0xB1);
    g_thread_probe.second_local = reinterpret_cast<uintptr_t>(&stack_local);
    g_thread_probe.trace[g_thread_probe.trace_size++] = 'B';
    if (threading::yield() != threading::Status::Ok) {
        g_thread_probe.yield_ok = false;
    }
    g_thread_probe.trace[g_thread_probe.trace_size++] = 'D';
}

bool run_kernel_thread_probe() {
    g_thread_probe = {};
    g_thread_probe.yield_ok = true;
    if (threading::initialize() != threading::Status::Ok) {
        return false;
    }
    threading::ThreadId first = threading::INVALID_THREAD_ID;
    threading::ThreadId second = threading::INVALID_THREAD_ID;
    if (threading::create(
            "probe-a", kernel_thread_probe_first, nullptr, &first) !=
            threading::Status::Ok ||
        threading::create(
            "probe-b", kernel_thread_probe_second, nullptr, &second) !=
            threading::Status::Ok) {
        return false;
    }
    threading::Stat first_stat{};
    threading::Stat second_stat{};
    if (threading::stat(first, &first_stat) != threading::Status::Ok ||
        threading::stat(second, &second_stat) != threading::Status::Ok ||
        first_stat.stack_bottom == second_stat.stack_bottom) {
        return false;
    }

    threading::RunResult result{};
    const threading::Status status =
        threading::run_until_idle(16U, &result);
    const bool trace_ok = g_thread_probe.trace_size == 4U &&
        g_thread_probe.trace[0] == 'A' &&
        g_thread_probe.trace[1] == 'B' &&
        g_thread_probe.trace[2] == 'C' &&
        g_thread_probe.trace[3] == 'D';
    const bool first_stack_ok =
        g_thread_probe.first_local >= first_stat.stack_bottom &&
        g_thread_probe.first_local < first_stat.stack_top;
    const bool second_stack_ok =
        g_thread_probe.second_local >= second_stat.stack_bottom &&
        g_thread_probe.second_local < second_stat.stack_top;
    return status == threading::Status::Ok && result.completed == 2U &&
        result.ready_remaining == 0U && result.switches >= 4U &&
        trace_ok && first_stack_ok && second_stack_ok &&
        g_thread_probe.first_local != g_thread_probe.second_local &&
        g_thread_probe.yield_ok &&
        threading::current() == threading::INVALID_THREAD_ID;
}

bool run_process_lifecycle_probe() {
    if (process::initialize() != process::Status::Ok) {
        return false;
    }
    process::ProcessId first = process::INVALID_PROCESS_ID;
    process::ProcessId second = process::INVALID_PROCESS_ID;
    if (process::spawn("/apps/hello", &first) != process::Status::Ok ||
        process::spawn("/apps/hello", &second) != process::Status::Ok ||
        first == second) {
        return false;
    }
    int32_t early_code = 0;
    if (process::wait(first, &early_code) != process::Status::WouldBlock) {
        return false;
    }
    process::RunResult run_result{};
    if (process::run_ready(16U, &run_result) != process::Status::Ok ||
        run_result.completed_threads != 2U || run_result.zombies != 2U) {
        return false;
    }
    process::Stat first_stat{};
    process::Stat second_stat{};
    if (process::stat(first, &first_stat) != process::Status::Ok ||
        process::stat(second, &second_stat) != process::Status::Ok ||
        first_stat.state != process::State::Zombie ||
        second_stat.state != process::State::Zombie ||
        first_stat.observed_pid != first ||
        second_stat.observed_pid != second) {
        return false;
    }
    int32_t first_code = -1;
    int32_t second_code = -1;
    return process::wait(first, &first_code) == process::Status::Ok &&
        process::wait(second, &second_code) == process::Status::Ok &&
        first_code == 0 && second_code == 0 &&
        process::stat(first, &first_stat) == process::Status::NotFound &&
        process::current() == process::INVALID_PROCESS_ID;
}

void preemption_probe_first(void*) {
    g_preemption_probe.first_started = true;
    while (drivers::pit::ticks() < g_preemption_probe.deadline) {
        ++g_preemption_probe.first_iterations;
        if (g_preemption_probe.second_started) {
            g_preemption_probe.first_observed_second = true;
        }
        arch::pause();
    }
}

void preemption_probe_second(void*) {
    g_preemption_probe.second_started = true;
    while (drivers::pit::ticks() < g_preemption_probe.deadline) {
        ++g_preemption_probe.second_iterations;
        if (g_preemption_probe.first_started) {
            g_preemption_probe.second_observed_first = true;
        }
        arch::pause();
    }
}

bool run_kernel_preemption_probe() {
    g_preemption_probe.first_iterations = 0U;
    g_preemption_probe.second_iterations = 0U;
    g_preemption_probe.first_started = false;
    g_preemption_probe.second_started = false;
    g_preemption_probe.first_observed_second = false;
    g_preemption_probe.second_observed_first = false;
    g_preemption_probe.deadline = drivers::pit::ticks() + 8U;
    if (threading::create(
            "preempt-a", preemption_probe_first, nullptr) !=
            threading::Status::Ok ||
        threading::create(
            "preempt-b", preemption_probe_second, nullptr) !=
            threading::Status::Ok) {
        return false;
    }
    threading::PreemptRunResult result{};
    const threading::Status status = threading::run_preemptive(&result);
    return status == threading::Status::Ok && result.completed == 2U &&
        result.preemptions >= 2U &&
        g_preemption_probe.first_iterations != 0U &&
        g_preemption_probe.second_iterations != 0U &&
        g_preemption_probe.first_observed_second &&
        g_preemption_probe.second_observed_first &&
        threading::current() == threading::INVALID_THREAD_ID;
}

bool run_user_process_preemption_probe() {
    process::ProcessId probe = process::INVALID_PROCESS_ID;
    process::ProcessId spinner = process::INVALID_PROCESS_ID;
    // Start the non-cooperating image first. Reaching hello then proves that
    // IRQ0, rather than a voluntary exit, transferred the CPU.
    if (process::spawn("/apps/spin", &spinner) != process::Status::Ok ||
        process::spawn("/apps/probe", &probe) != process::Status::Ok ||
        probe == spinner) {
        return false;
    }

    threading::PreemptRunResult first_run{};
    const process::Status first_status =
        process::run_preemptive_for(1000U, &first_run);
    log::write_u64(
        log::Level::Info,
        "PROCESS",
        "Ring-3 probe timer ticks=",
        first_run.timer_ticks);
    log::write_u64(
        log::Level::Info,
        "PROCESS",
        "Ring-3 probe preemptions=",
        first_run.preemptions);
    log::write_u64(
        log::Level::Info,
        "PROCESS",
        "Ring-3 probe completed threads=",
        first_run.completed);
    if (first_status != process::Status::Ok ||
        !first_run.timed_out || first_run.preemptions < 1U) {
        return false;
    }
    process::Stat probe_stat{};
    process::Stat spinner_stat{};
    if (process::stat(probe, &probe_stat) != process::Status::Ok ||
        process::stat(spinner, &spinner_stat) != process::Status::Ok ||
        probe_stat.state != process::State::Zombie ||
        probe_stat.observed_pid != probe ||
        spinner_stat.state != process::State::Running ||
        spinner_stat.address_space_root == 0U) {
        log::write_u64(
            log::Level::Error,
            "PROCESS",
            "Ring-3 ABI probe process state=",
            static_cast<uint64_t>(probe_stat.state));
        log::write_u64(
            log::Level::Error,
            "PROCESS",
            "Ring-3 spinner process state=",
            static_cast<uint64_t>(spinner_stat.state));
        return false;
    }

    constexpr int32_t forced_exit = 137;
    if (process::terminate(spinner, forced_exit) != process::Status::Ok) {
        return false;
    }
    threading::PreemptRunResult cleanup_run{};
    if (process::run_preemptive_for(0U, &cleanup_run) !=
            process::Status::Ok ||
        cleanup_run.completed != 1U) {
        return false;
    }

    int32_t probe_code = -1;
    int32_t spinner_code = -1;
    return process::wait(probe, &probe_code) == process::Status::Ok &&
        process::wait(spinner, &spinner_code) == process::Status::Ok &&
        probe_code == 0 && spinner_code == forced_exit &&
        threading::current() == threading::INVALID_THREAD_ID;
}

[[noreturn]] void boot_failure(const char* module, const char* reason) {
    log::write(log::Level::Fatal, module, reason);
    terminal::println("[TEST] ALL_REQUIRED_TESTS_PASSED: FAIL");
    arch::x86_64::interrupts::halt();
}

void print_stack_trace(uint64_t initial_frame_pointer) {
    constexpr size_t max_frames = 16;
    const uintptr_t stack_begin =
        reinterpret_cast<uintptr_t>(kernel_stack_bottom);
    const uintptr_t stack_end =
        reinterpret_cast<uintptr_t>(kernel_stack_top);
    uintptr_t frame_pointer =
        static_cast<uintptr_t>(initial_frame_pointer);

    terminal::println("stack trace:");
    if (frame_pointer < stack_begin ||
        frame_pointer > stack_end - 2 * sizeof(uint64_t) ||
        (frame_pointer & (alignof(uint64_t) - 1u)) != 0) {
        terminal::println("  unavailable (frame pointer outside kernel stack)");
        return;
    }

    size_t emitted = 0;
    while (emitted < max_frames) {
        const auto* frame =
            reinterpret_cast<const uint64_t*>(frame_pointer);
        const uintptr_t previous =
            static_cast<uintptr_t>(frame[0]);
        const uint64_t return_address = frame[1];
        if (return_address == 0) {
            break;
        }

        terminal::write("  #");
        terminal::write_u64(emitted);
        terminal::write(" ");
        terminal::write_hex(return_address);
        terminal::println();
        ++emitted;

        if (previous <= frame_pointer ||
            previous < stack_begin ||
            previous > stack_end - 2 * sizeof(uint64_t) ||
            (previous & (alignof(uint64_t) - 1u)) != 0) {
            break;
        }
        frame_pointer = previous;
    }
    if (emitted == 0) {
        terminal::println("  unavailable (empty frame chain)");
    }
}

void exception_handler(
    arch::x86_64::interrupts::InterruptFrame& frame) {
    if (user::runtime::handle_exception(frame)) {
        log::write_u64(
            log::Level::Warn,
            "USER",
            "isolated ring-3 exception vector=",
            frame.vector);
        return;
    }

    static uint32_t panic_active = 0;
    if (__atomic_exchange_n(
            &panic_active,
            static_cast<uint32_t>(1),
            __ATOMIC_ACQ_REL) != 0) {
        serial::write("fatal: recursive kernel panic\n");
        arch::x86_64::interrupts::halt();
    }

    static const char* const exception_names[32] = {
        "divide error", "debug", "non-maskable interrupt", "breakpoint",
        "overflow", "bound range exceeded", "invalid opcode",
        "device not available", "double fault", "coprocessor overrun",
        "invalid TSS", "segment not present", "stack-segment fault",
        "general protection fault", "page fault", "reserved",
        "x87 floating-point exception", "alignment check", "machine check",
        "SIMD floating-point exception", "virtualization exception",
        "control-protection exception", "reserved", "reserved", "reserved",
        "reserved", "reserved", "reserved", "hypervisor injection exception",
        "VMM communication exception", "security exception", "reserved",
    };
    const char* const exception_name =
        frame.vector < 32 ? exception_names[frame.vector]
                          : "unknown exception";
    const uintptr_t interrupted_rsp = static_cast<uintptr_t>(frame.rsp);

    terminal::set_colors(0x00FCA5A5, 0x000C1018);
    terminal::println();
    terminal::println("KERNEL PANIC");
    terminal::reset_colors();
    terminal::write("module=CPU reason=");
    terminal::println(exception_name);
    terminal::write("vector=");
    terminal::write_u64(frame.vector);
    terminal::write(" error=");
    terminal::write_hex(frame.error_code);
    terminal::println();
    terminal::write("rip=");
    terminal::write_hex(frame.rip);
    terminal::write(" rsp=");
    terminal::write_hex(interrupted_rsp);
    terminal::write(" rbp=");
    terminal::write_hex(frame.rbp);
    terminal::println();
    terminal::write("cs=");
    terminal::write_hex(frame.cs);
    terminal::write(" rflags=");
    terminal::write_hex(frame.rflags);
    if (frame.vector == 14) {
        terminal::write(" cr2=");
        terminal::write_hex(
            arch::x86_64::interrupts::last_page_fault_address());
    }
    terminal::println();
    terminal::write("rax=");
    terminal::write_hex(frame.rax);
    terminal::write(" rbx=");
    terminal::write_hex(frame.rbx);
    terminal::write(" rcx=");
    terminal::write_hex(frame.rcx);
    terminal::write(" rdx=");
    terminal::write_hex(frame.rdx);
    terminal::println();
    terminal::write("rsi=");
    terminal::write_hex(frame.rsi);
    terminal::write(" rdi=");
    terminal::write_hex(frame.rdi);
    terminal::write(" r8=");
    terminal::write_hex(frame.r8);
    terminal::write(" r9=");
    terminal::write_hex(frame.r9);
    terminal::println();
    terminal::write("r10=");
    terminal::write_hex(frame.r10);
    terminal::write(" r11=");
    terminal::write_hex(frame.r11);
    terminal::write(" r12=");
    terminal::write_hex(frame.r12);
    terminal::write(" r13=");
    terminal::write_hex(frame.r13);
    terminal::println();
    terminal::write("r14=");
    terminal::write_hex(frame.r14);
    terminal::write(" r15=");
    terminal::write_hex(frame.r15);
    terminal::println(" pid=0 tid=0");
    print_stack_trace(frame.rbp);
    log::write_hex(
        log::Level::Fatal, "CPU", "exception RIP=", frame.rip);
    if (frame.vector == 14) {
        log::write_hex(
            log::Level::Fatal,
            "MEMORY",
            "page fault address=",
            arch::x86_64::interrupts::last_page_fault_address());
    }
    arch::x86_64::interrupts::halt();
}

void initialize_exception_handling() {
    arch::x86_64::interrupts::initialize();
    log::write(log::Level::Info, "KERNEL", "IDT initialized");
    for (uint8_t vector = 0; vector < 32; ++vector) {
        arch::x86_64::interrupts::register_handler(
            vector, exception_handler);
    }
    log::write(log::Level::Info, "KERNEL", "CPU exception handlers installed");
}

bool initialize_hardware_interrupts() {
    drivers::pic::initialize();
    const bool timer_ready = drivers::pit::initialize(100);
    log::write(
        timer_ready ? log::Level::Info : log::Level::Error,
        "TIME",
        timer_ready ? "PIT monotonic clock ready"
                    : "PIT initialization failed");
    const bool schedule_hook_ready =
        arch::x86_64::interrupts::register_irq_schedule_hook(
            threading::timer_irq_schedule);
    const bool keyboard_ready = drivers::keyboard::initialize();
    const bool mouse_ready = drivers::mouse::initialize();
    const bool input_ready = input::initialize(
        graphics::width(), graphics::height());
    arch::x86_64::interrupts::enable();
    return timer_ready && schedule_hook_ready && keyboard_ready &&
        mouse_ready && input_ready;
}

void restore_shell_after_application() {
    terminal::clear();
    terminal::println("KuroganeOS application closed.");
    if (user::console::active()) {
        // The userspace shell owns its prompt. A synthetic empty line asks it
        // to redraw after the framebuffer application cleared the display.
        static_cast<void>(user::console::push('\n'));
    } else {
        shell::show_prompt();
    }
}

[[noreturn]] void kernel_loop() {
    uint64_t last_tick = drivers::pit::ticks();
    for (;;) {
        bool did_work = false;
        const uint64_t tick = drivers::pit::ticks();
        if (tick != last_tick) {
            last_tick = tick;
            scheduler::tick(tick);
            applications::dispatch_tick(tick);
            net::service::poll(4);
            did_work = true;
        }

        scheduler::RunResult run_result{};
        const auto run_status = scheduler::run_pending(8, &run_result);
        if ((run_status == scheduler::Status::Ok ||
             run_status == scheduler::Status::BudgetExhausted) &&
            run_result.executed != 0) {
            did_work = true;
        }

        if (user::console::active()) {
            threading::PreemptRunResult userspace_result{};
            const process::Status userspace_status =
                process::run_preemptive_for(2U, &userspace_result);
            if (userspace_status == process::Status::Ok &&
                (userspace_result.timer_ticks != 0U ||
                 userspace_result.completed != 0U)) {
                did_work = true;
            }
        }

        if (input::pump() != 0U) did_work = true;
        if (drivers::usb::xhci::poll(8U) != 0U) did_work = true;
        input::Event event{};
        while (input::try_read(&event)) {
            const bool application_was_running = applications::running();
            if (application_was_running) {
                applications::dispatch_input(event);
                if (!applications::running()) {
                    restore_shell_after_application();
                }
            } else if (event.type == input::EventType::KeyDown &&
                       event.character != 0) {
                if (user::console::active()) {
                    static_cast<void>(user::console::push(event.character));
                } else {
                    shell::feed(event.character);
                }
            }
            did_work = true;
        }

        if (!did_work) {
            if (arch::x86_64::interrupts::enabled() &&
                drivers::pit::initialized()) {
                arch::halt();
            } else {
                arch::pause();
            }
        }
    }
}

} // namespace

extern "C" KUROGANE_SYSV_ABI void kmain(void* boot_argument) {
    serial::init();
    log::set_minimum_level(log::Level::Info);
    log::write(log::Level::Info, "BOOT", "KuroganeOS kernel entry");

    BootContext context{};
    if (!parse_boot_argument(boot_argument, context) ||
        !terminal::configure(context.framebuffer)) {
        serial::write("fatal: invalid boot argument or framebuffer\n");
        for (;;) {
            arch::disable_interrupts();
            arch::halt();
        }
    }

    print_banner(context.safe_mode, context.diagnostics, context.installer);
    if (context.force_desktop && !context.safe_mode) {
        terminal::println("boot=desktop (DESKTOP ALPHA)");
        log::write(
            log::Level::Warn,
            "GUI",
            "boot=desktop enabled; Desktop Alpha session requested");
    } else {
        terminal::println("boot=console");
        log::write(log::Level::Info, "BOOT", "boot=console");
    }
    if (context.safe_mode) {
        log::write(log::Level::Warn, "BOOT", "safe mode enabled");
    }
    if (context.diagnostics) {
        log::write(log::Level::Warn, "BOOT", "diagnostics mode enabled");
    }
    arch::x86_64::gdt::initialize();
    log::write(log::Level::Info, "KERNEL", "GDT, TSS and IST stacks initialized");
    initialize_exception_handling();
    memory::init_kernel_heap(g_kernel_heap, sizeof(g_kernel_heap));
    if (!initialize_physical_memory(context.boot_info)) {
        boot_failure("MEMORY", "physical memory manager initialization failed");
    }
    log::write_u64(
        log::Level::Info,
        "MEMORY",
        "kernel heap bytes=",
        memory::total_bytes());

    log::write(
        log::Level::Info,
        "MEMORY",
        "cloning active UEFI four-level page-table root");
    const auto paging_initialize_status =
        memory::kernel_virtual_memory::initialize();
    if (paging_initialize_status !=
        memory::kernel_virtual_memory::Status::Ok) {
        terminal::println("[TEST] paging: FAIL");
        boot_failure(
            "MEMORY",
            memory::kernel_virtual_memory::status_message(
                paging_initialize_status));
    }
    log::write(
        log::Level::Info,
        "MEMORY",
        "page-table backend initialized; running CPU mapping test");
    const auto paging_test_status =
        memory::kernel_virtual_memory::self_test();
    if (paging_test_status !=
        memory::kernel_virtual_memory::Status::Ok) {
        terminal::println("[TEST] paging: FAIL");
        boot_failure(
            "MEMORY",
            memory::kernel_virtual_memory::status_message(
                paging_test_status));
    }
    log::write_hex(
        log::Level::Info,
        "MEMORY",
        "virtual memory manager ready, CR3=",
        memory::kernel_virtual_memory::root_table_physical());
    terminal::println("[TEST] paging: PASS");

    initialize_platform_discovery(context.boot_info);

    if (context.installer) {
        pci::scan();
        initialize_device_framework(false);
        const storage::ahci::Status ahci_status = storage::ahci::initialize();
        if (ahci_status != storage::ahci::Status::Ok &&
            ahci_status != storage::ahci::Status::AlreadyInitialized) {
            boot_failure("INSTALL", storage::ahci::status_message(ahci_status));
        }
        if (!drivers::keyboard::initialize()) {
            boot_failure("INSTALL", "PS/2 keyboard initialization failed");
        }
        install::installer::run_interactive(
            context.boot_info->installation_package,
            static_cast<size_t>(
                context.boot_info->installation_package_size));
    }

    const user::runtime::Status user_runtime_status =
        user::runtime::initialize();
    if (user_runtime_status != user::runtime::Status::Ok &&
        user_runtime_status != user::runtime::Status::AlreadyInitialized) {
        terminal::println("[TEST] user_runtime: FAIL");
        boot_failure(
            "USER",
            user::runtime::status_message(user_runtime_status));
    }
    log::write(
        log::Level::Info,
        "USER",
        "ring-3 runtime and int 0x80 syscall gate initialized");
    terminal::println("[TEST] user_runtime: PASS");

    if (!memory_self_test()) {
        terminal::println("[TEST] memory_allocator: FAIL");
        boot_failure("MEMORY", "allocator self-test failed");
    }
    terminal::println("[TEST] memory_allocator: PASS");
    if (!seed_filesystem()) {
        terminal::println("[TEST] ramfs_bootstrap: FAIL");
        boot_failure("FS", "root RAMFS initialization failed");
    }
    terminal::println("[TEST] ramfs_bootstrap: PASS");
    log::write(log::Level::Info, "FS", "root RAMFS mounted");
    if (!context.safe_mode) {
        pci::scan();
        initialize_device_framework(false);
        initialize_storage_probe();
        run_userland_probe();
    } else {
        initialize_device_framework(true);
    }
    const auto network_status = context.safe_mode
        ? net::Status::NotInitialized
        : net::service::initialize();
    if (scheduler::initialize(0) != scheduler::Status::Ok) {
        boot_failure("SCHED", "scheduler initialization failed");
    }
    log::write(log::Level::Info, "SCHED", "scheduler initialized");
    if (!run_kernel_thread_probe()) {
        terminal::println("[TEST] kernel_context_switch: FAIL");
        boot_failure("THREAD", "separate-stack context switch failed");
    }
    log::write(
        log::Level::Info,
        "THREAD",
        "two kernel threads switched on separate stacks");
    terminal::println("[TEST] kernel_context_switch: PASS");
    if (fs::root_volume::mounted()) {
        if (!run_process_lifecycle_probe()) {
            terminal::println("[TEST] process_spawn_wait: FAIL");
            boot_failure("PROCESS", "spawn/run/wait lifecycle proof failed");
        }
        log::write(
            log::Level::Info,
            "PROCESS",
            "two ELF processes reached zombie and were reaped");
        terminal::println("[TEST] process_spawn_wait: PASS");
    }
    applications::initialize();
    const bool experimental_gui_enabled =
        context.force_desktop && !context.safe_mode;
    if (experimental_gui_enabled && !builtin_apps::register_all()) {
        boot_failure("APPS", "built-in application registration failed");
    }

    const bool hardware_ready = initialize_hardware_interrupts();
    log::write(
        hardware_ready ? log::Level::Info : log::Level::Warn,
        "INTERRUPTS",
        hardware_ready ? "PIC, PIT, keyboard, mouse and input queue ready"
                       : "hardware input degraded; polling fallback active");
    terminal::write("interrupts/timer/input: ");
    terminal::println(hardware_ready ? "READY" : "DEGRADED (polling enabled)");
    terminal::write("PS/2 controller: ");
    terminal::println(
        drivers::keyboard::controller_configured() ? "configured" : "fallback");
    terminal::write("PS/2 mouse: ");
    terminal::println(
        drivers::mouse::controller_configured()
            ? (drivers::mouse::wheel_enabled()
                ? "configured (wheel)"
                : "configured (3-button)")
            : "unavailable");
    terminal::println(
        drivers::mouse::initialized()
            ? "[TEST] ps2_mouse: PASS"
            : "[TEST] ps2_mouse: FAIL");
    if (!hardware_ready) {
        boot_failure("INTERRUPTS", "required timer or PS/2 input unavailable");
    }
    if (!run_kernel_preemption_probe()) {
        terminal::println("[TEST] kernel_preemption: FAIL");
        boot_failure("THREAD", "timer preemption proof failed");
    }
    log::write(
        log::Level::Info,
        "THREAD",
        "IRQ0 preempted two non-yielding kernel threads");
    terminal::println("[TEST] kernel_preemption: PASS");
    if (fs::root_volume::mounted()) {
        if (!run_user_process_preemption_probe()) {
            terminal::println("[TEST] ring3_preemption: FAIL");
            boot_failure(
                "PROCESS",
                "independent Ring-3 process preemption proof failed");
        }
        log::write(
            log::Level::Info,
            "PROCESS",
            "IRQ0 preempted a non-cooperating Ring-3 process");
        terminal::println("[TEST] ring3_preemption: PASS");
        terminal::println("[TEST] user_multitasking: PASS");
        terminal::println("[TEST] syscall_process_abi: PASS");
    }
    terminal::write("PCI devices: ");
    terminal::write_u64(pci::device_count());
    terminal::println();
    terminal::write("network: ");
    if (context.safe_mode) {
        terminal::println("SKIPPED (safe mode)");
        terminal::println("[TEST] network_loopback: SKIP");
    } else if (network_status == net::Status::Ok &&
               net::service::physical_interface()) {
        terminal::println("E1000 link READY");
        terminal::println("[TEST] e1000_link: PASS");
        if (!net::service::dhcp_configured()) {
            terminal::println("[TEST] dhcp_lease: FAIL");
            boot_failure("NET", "DHCP did not configure the physical link");
        } else {
            terminal::println("[TEST] dhcp_lease: PASS");
            terminal::println("[TEST] udp_transport: PASS");
        }
        if (net::service::ping_gateway(1) != net::Status::Ok) {
            terminal::println("[TEST] network_gateway_icmp: FAIL");
            boot_failure("NET", "gateway ICMP self-test failed");
        } else {
            terminal::println("gateway ICMP: PASS");
            terminal::println("[TEST] network_gateway_icmp: PASS");
        }
        net::IPv4Address resolved{};
        const net::Status dns_status =
            net::service::resolve_a("example.com", &resolved);
        if (dns_status == net::Status::Ok) {
            terminal::println("DNS A example.com: PASS");
            terminal::println("[TEST] dns_resolver: PASS");
            const net::Status tcp_status =
                net::service::tcp_connect_probe(resolved, 80U, "example.com");
            terminal::println(
                tcp_status == net::Status::Ok
                    ? "[TEST] tcp_http_optional: PASS"
                    : "[TEST] tcp_http_optional: SKIP");
        } else {
            terminal::println("DNS A example.com: optional online test unavailable");
            terminal::println("[TEST] dns_resolver_online: SKIP");
            terminal::println("[TEST] tcp_http_optional: SKIP");
        }
        const net::IPv4Address public_probe = {{1U, 1U, 1U, 1U}};
        terminal::println(
            net::service::ping_address(public_probe, 2U) == net::Status::Ok
                ? "[TEST] network_online_icmp: PASS"
                : "[TEST] network_online_icmp: SKIP");
    } else if (network_status == net::Status::Ok &&
               net::service::ping_loopback(1) == net::Status::Ok) {
        terminal::println("PASS (loopback fallback 127.0.0.1)");
        terminal::println("[TEST] network_loopback: PASS");
    } else {
        terminal::write("FAIL (");
        terminal::write(net::status_message(network_status));
        terminal::println(")");
        terminal::println("[TEST] network_gateway_icmp: FAIL");
        boot_failure("NET", "physical network self-test failed");
    }
    terminal::println(
        g_required_runtime_test_failed
            ? "[TEST] ALL_REQUIRED_TESTS_PASSED: FAIL"
            : "[TEST] ALL_REQUIRED_TESTS_PASSED");
    terminal::println(
        context.safe_mode
            ? "Type 'help' for emergency kernel commands."
            : "Starting /system/init and the userspace console...");
    terminal::println();

    if (context.safe_mode) {
        shell::initialize(false);
    } else {
        if (context.force_desktop) {
            if (!kurogane_start_desktop_session()) {
                boot_failure("GUI", "cannot establish Flux desktop session owner");
            }
            terminal::println("[TEST] flux_session_owner: PASS");
        }
        user::console::initialize();
        process::ProcessId init_pid = process::INVALID_PROCESS_ID;
        const process::Status init_status =
            process::spawn_init("/system/init", &init_pid);
        if (init_status != process::Status::Ok || init_pid != 1U) {
            terminal::println("[TEST] userspace_init_spawn: FAIL");
            boot_failure("INIT", "cannot create /system/init as PID 1");
        }
        terminal::println("[TEST] userspace_init_spawn: PASS");
        log::write(log::Level::Info, "INIT", "spawned /system/init as PID 1");
    }


    kernel_loop();
}
