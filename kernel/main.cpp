#include "include/kernel.hpp"

#include "apps/builtin.hpp"
#include "apps/framework.hpp"
#include "arch/x86_64/gdt.hpp"
#include "arch/x86_64/interrupts.hpp"
#include "arch/x86_64/io.hpp"
#include "core/string.hpp"
#include "core/log.hpp"
#include "drivers/keyboard.hpp"
#include "drivers/pci.hpp"
#include "drivers/pic.hpp"
#include "drivers/pit.hpp"
#include "drivers/serial.hpp"
#include "fs/ramfs.hpp"
#include "memory/allocator.hpp"
#include "memory/kernel_virtual_memory.hpp"
#include "memory/physical_memory.hpp"
#include "net/service.hpp"
#include "shell/shell.hpp"
#include "task/scheduler.hpp"
#include "terminal.hpp"
#include "../common/version.h"

extern "C" unsigned char kernel_stack_bottom[];
extern "C" unsigned char kernel_stack_top[];

namespace {

constexpr size_t kKernelHeapSize = 2 * 1024 * 1024;
alignas(64) uint8_t g_kernel_heap[kKernelHeapSize];

struct BootContext {
    KuroganeFramebuffer framebuffer;
    const KuroganeBootInfo* boot_info;
    bool safe_mode;
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

    context.framebuffer = boot_info->framebuffer;
    context.boot_info = boot_info;
    context.safe_mode =
        (boot_info->flags & KUROGANE_BOOT_FLAG_SAFE_MODE) != 0;
    return true;
}

void print_banner(bool safe_mode) {
    terminal::set_colors(0x00F97316, 0x000C1018);
    terminal::println("KUROGANE OS");
    terminal::reset_colors();
    terminal::println(
        "x86-64 UEFI kernel " KUROGANE_VERSION_STRING);
    terminal::println("boot protocol: v2 (memory map + boot flags)");
    if (safe_mode) {
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
        "Type 'help' to list shell commands or 'gui' for the desktop.\n";
    return fs::write_file_data(
               "/home/readme.txt", readme, sizeof(readme) - 1, true) ==
           fs::Status::Ok;
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
    const uintptr_t interrupted_rsp =
        (frame.cs & 3u) != 0 || frame.vector == 2 ||
                frame.vector == 8 || frame.vector == 18
            ? static_cast<uintptr_t>(frame.rsp)
            : reinterpret_cast<uintptr_t>(&frame.rflags) + sizeof(uint64_t);

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
    const bool keyboard_ready = drivers::keyboard::initialize();
    arch::x86_64::interrupts::enable();
    return timer_ready && keyboard_ready;
}

void restore_shell_after_application() {
    terminal::clear();
    terminal::println("KuroganeOS application closed.");
    shell::show_prompt();
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

        char character = 0;
        while (drivers::keyboard::try_read_char(character)) {
            const bool application_was_running = applications::running();
            if (application_was_running) {
                applications::dispatch_key(character);
                if (!applications::running()) {
                    restore_shell_after_application();
                }
            } else {
                shell::feed(character);
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

    print_banner(context.safe_mode);
    if (context.safe_mode) {
        log::write(log::Level::Warn, "BOOT", "safe mode enabled");
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
    }
    const auto network_status = context.safe_mode
        ? net::Status::NotInitialized
        : net::service::initialize();
    if (scheduler::initialize(0) != scheduler::Status::Ok) {
        boot_failure("SCHED", "scheduler initialization failed");
    }
    log::write(log::Level::Info, "SCHED", "scheduler initialized");
    applications::initialize();
    if (!context.safe_mode && !builtin_apps::register_all()) {
        boot_failure("APPS", "built-in application registration failed");
    }

    const bool hardware_ready = initialize_hardware_interrupts();
    log::write(
        hardware_ready ? log::Level::Info : log::Level::Warn,
        "INTERRUPTS",
        hardware_ready ? "PIC, PIT and keyboard ready"
                       : "hardware input degraded; polling fallback active");
    terminal::write("interrupts/timer/keyboard: ");
    terminal::println(hardware_ready ? "READY" : "DEGRADED (polling enabled)");
    terminal::write("PS/2 controller: ");
    terminal::println(
        drivers::keyboard::controller_configured() ? "configured" : "fallback");
    if (!hardware_ready) {
        boot_failure("INTERRUPTS", "required timer or keyboard unavailable");
    }
    terminal::write("PCI devices: ");
    terminal::write_u64(pci::device_count());
    terminal::println();
    terminal::write("network loopback: ");
    if (context.safe_mode) {
        terminal::println("SKIPPED (safe mode)");
        terminal::println("[TEST] network_loopback: SKIP");
    } else if (network_status == net::Status::Ok &&
        net::service::ping_loopback(1) == net::Status::Ok) {
        terminal::println("PASS (127.0.0.1)");
        terminal::println("[TEST] network_loopback: PASS");
    } else {
        terminal::println("FAIL");
        terminal::println("[TEST] network_loopback: FAIL");
        boot_failure("NET", "loopback self-test failed");
    }
    terminal::println("[TEST] ALL_REQUIRED_TESTS_PASSED");
    terminal::println("Type 'help' for commands.");
    terminal::println();
    shell::initialize();

    kernel_loop();
}
