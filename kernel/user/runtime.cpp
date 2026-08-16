#include "runtime.hpp"

#include <kurogane/status.h>
#include <kurogane/syscall.h>
#include <kurogane/ui.h>

#include "elf_loader.hpp"
#include "console.hpp"
#include "../arch/x86_64/gdt.hpp"
#include "../core/log.hpp"
#include "../fs/root_volume.hpp"
#include "../memory/allocator.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/physical_memory.hpp"
#include "../task/process.hpp"
#include "../task/thread.hpp"
#include "../terminal.hpp"
#include "../drivers/framebuffer.hpp"
#include "../ui/ui.hpp"
#include "../ui/window_manager.hpp"

struct UserReturnState {
    uint64_t kernel_rsp;
    uint64_t kernel_rflags;
};

extern "C" void x86_64_enter_user(
    uint64_t entry,
    uint64_t stack_pointer,
    UserReturnState* return_state);
extern "C" void x86_64_interrupt_return_to_kernel();

namespace user::runtime {
namespace {

constexpr uint8_t kSyscallVector = 0x80U;
constexpr size_t kMaximumExecutableSize = 512U * 1024U;
constexpr size_t kStackPages = 8U;
constexpr uint64_t kStackTop =
    elf::USER_REGION_BASE + UINT64_C(0x02000000);
constexpr uint64_t kFaultTrampoline =
    elf::USER_REGION_BASE + UINT64_C(0x03FFF000);
constexpr size_t kMaximumWrite = 16U * 1024U;
constexpr size_t kMaximumContexts = threading::MAX_THREADS;
constexpr size_t kMaximumHandles = 16U;
constexpr size_t kMaximumAllocations = 16U;
constexpr uint64_t kHeapBase =
    elf::USER_REGION_BASE + UINT64_C(0x02800000);
constexpr uint64_t kHeapEnd =
    elf::USER_REGION_BASE + UINT64_C(0x03800000);

struct HandleSlot {
    fs::vfs::OpenFileHandle file;
    uint32_t generation;
    bool active;
};

struct Allocation {
    uint64_t address;
    size_t page_count;
    bool active;
};

constexpr size_t kMaximumUiEvents = 16U;

struct UiState {
    ku_ui_frame frame;
    ku_ui_event events[kMaximumUiEvents];
    windowing::WindowId window;
    uint8_t head;
    uint8_t tail;
    bool active;
};

struct Context {
    memory::kernel_virtual_memory::OwnedAddressSpace address_space;
    elf::Image image;
    Result result;
    UserReturnState return_state;
    uint64_t pid;
    threading::ThreadId tid;
    HandleSlot handles[kMaximumHandles];
    Allocation allocations[kMaximumAllocations];
    UiState ui;
    uint64_t next_heap;
    bool active;
};

void draw_user_window(
    windowing::WindowId,
    const ui::Rect& content,
    bool focused,
    void* opaque) {
    auto* context = static_cast<Context*>(opaque);
    if (context == nullptr || !context->active || !context->ui.active) return;
    const ku_ui_frame& frame = context->ui.frame;
    const graphics::Color background = frame.background_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color foreground = frame.foreground_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color accent = frame.accent_rgb & UINT32_C(0xFFFFFF);
    graphics::fill_rect(
        content.x, content.y, content.width, content.height, background);
    int32_t y = content.y + 12;
    for (uint32_t index = 0U;
         index < frame.line_count && index < KU_UI_MAX_LINES;
         ++index) {
        graphics::draw_text(
            content.x + 12, y, frame.lines[index],
            index == 0U && focused ? accent : foreground,
            background, 2U, true);
        y += 22;
        if (y + 20 >= content.y + content.height) break;
    }
    if (frame.progress_maximum != 0U && content.height >= 70) {
        ui::progress(
            {content.x + 12, content.y + content.height - 32,
             content.width - 24, 16},
            frame.progress_value, frame.progress_maximum);
    }
}

void queue_user_event(Context& context, const ku_ui_event& event) {
    const uint8_t next = static_cast<uint8_t>(
        (context.ui.head + 1U) % kMaximumUiEvents);
    if (next == context.ui.tail) {
        context.ui.tail = static_cast<uint8_t>(
            (context.ui.tail + 1U) % kMaximumUiEvents);
    }
    context.ui.events[context.ui.head] = event;
    context.ui.head = next;
}

void input_user_window(
    windowing::WindowId,
    const input::Event& input_event,
    void* opaque) {
    auto* context = static_cast<Context*>(opaque);
    if (context == nullptr || !context->active || !context->ui.active) return;
    ku_ui_event event{};
    event.structure_size = sizeof(event);
    if (input_event.type == input::EventType::KeyDown) {
        event.type = KU_UI_EVENT_KEY;
        event.key = static_cast<uint32_t>(input_event.key);
        event.character = static_cast<uint8_t>(input_event.character);
    } else if (input_event.type == input::EventType::MouseMove ||
               input_event.type == input::EventType::MouseButtonDown ||
               input_event.type == input::EventType::MouseButtonUp) {
        event.type = KU_UI_EVENT_POINTER;
        event.x = input_event.x;
        event.y = input_event.y;
        event.buttons = input_event.buttons;
    } else {
        return;
    }
    queue_user_event(*context, event);
}

bool g_initialized = false;
Context* g_contexts[kMaximumContexts]{};
uint64_t g_process_identity = 0U;

uint64_t save_and_disable_interrupts() {
    uint64_t flags = 0U;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void restore_interrupts(uint64_t flags) {
    __asm__ volatile(
        "pushq %0; popfq"
        :
        : "r"(flags)
        : "memory", "cc");
}

Context* current_context() {
    const uint64_t root =
        memory::kernel_virtual_memory::active_root_table_physical();
    for (Context* context : g_contexts) {
        if (context != nullptr && context->active &&
            context->address_space.address_space.root_table_physical == root) {
            return context;
        }
    }
    return nullptr;
}

void log_missing_context() {
    log::write_u64(
        log::Level::Error,
        "SYSCALL",
        "missing context TID=",
        threading::current());
    log::write_hex(
        log::Level::Error,
        "SYSCALL",
        "missing context active CR3=",
        memory::kernel_virtual_memory::active_root_table_physical());
    for (Context* context : g_contexts) {
        if (context == nullptr || !context->active) continue;
        log::write_u64(
            log::Level::Error,
            "SYSCALL",
            "registered context TID=",
            context->tid);
        log::write_hex(
            log::Level::Error,
            "SYSCALL",
            "registered context CR3=",
            context->address_space.address_space.root_table_physical);
    }
}

bool register_context(Context* context) {
    if (context == nullptr) return false;
    for (Context*& candidate : g_contexts) {
        if (candidate == nullptr) {
            candidate = context;
            return true;
        }
    }
    return false;
}

void unregister_context(Context* context) {
    for (Context*& candidate : g_contexts) {
        if (candidate == context) {
            candidate = nullptr;
            return;
        }
    }
}

void cpuid(
    uint32_t leaf,
    uint32_t& eax,
    uint32_t& ebx,
    uint32_t& ecx,
    uint32_t& edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(0U)
        : "memory");
}

uint64_t read_msr(uint32_t index) {
    uint32_t low = 0U;
    uint32_t high = 0U;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(index));
    return (static_cast<uint64_t>(high) << 32U) | low;
}

void write_msr(uint32_t index, uint64_t value) {
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(index),
          "a"(static_cast<uint32_t>(value)),
          "d"(static_cast<uint32_t>(value >> 32U))
        : "memory");
}

bool enable_memory_protection() {
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    uint32_t ecx = 0U;
    uint32_t edx = 0U;
    cpuid(UINT32_C(0x80000000), eax, ebx, ecx, edx);
    if (eax < UINT32_C(0x80000001)) {
        return false;
    }
    cpuid(UINT32_C(0x80000001), eax, ebx, ecx, edx);
    constexpr uint32_t nx_capability = UINT32_C(1) << 20U;
    if ((edx & nx_capability) == 0U) {
        return false;
    }

    constexpr uint32_t efer_msr = UINT32_C(0xC0000080);
    constexpr uint64_t nxe = UINT64_C(1) << 11U;
    write_msr(efer_msr, read_msr(efer_msr) | nxe);

    uint64_t cr0 = 0U;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    constexpr uint64_t write_protect = UINT64_C(1) << 16U;
    cr0 |= write_protect;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    return true;
}

bool validate_user_buffer(
    const Context& context,
    uint64_t address,
    size_t size,
    bool require_writable = false) {
    if (!context.active) {
        return false;
    }
    if (size == 0U) {
        return true;
    }
    if (address < elf::USER_REGION_BASE ||
        address >= elf::USER_REGION_END ||
        size > elf::USER_REGION_END - address) {
        return false;
    }

    const uint64_t last = address + size - 1U;
    uint64_t page =
        address & ~(memory::virtual_memory::PAGE_SIZE - 1U);
    const uint64_t last_page =
        last & ~(memory::virtual_memory::PAGE_SIZE - 1U);
    for (;;) {
        memory::virtual_memory::Mapping mapping{};
        if (memory::virtual_memory::query_page(
                const_cast<memory::virtual_memory::AddressSpace*>(
                    &context.address_space.address_space),
                page,
                &mapping) != memory::virtual_memory::Status::Ok ||
            !memory::virtual_memory::has_flag(
                mapping.flags,
                memory::virtual_memory::MapFlags::User) ||
            (require_writable && !memory::virtual_memory::has_flag(
                mapping.flags,
                memory::virtual_memory::MapFlags::Writable))) {
            return false;
        }
        if (page == last_page) {
            return true;
        }
        page += memory::virtual_memory::PAGE_SIZE;
    }
}

uint64_t encode_handle(size_t index, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32U) |
        static_cast<uint64_t>(index + 3U);
}

HandleSlot* decode_handle(Context& context, uint64_t handle) {
    const uint64_t encoded = handle & UINT64_C(0xFFFFFFFF);
    const uint32_t generation = static_cast<uint32_t>(handle >> 32U);
    if (encoded < 3U || encoded >= 3U + kMaximumHandles) return nullptr;
    HandleSlot& slot = context.handles[static_cast<size_t>(encoded - 3U)];
    return slot.active && slot.generation == generation ? &slot : nullptr;
}

ku_status_t vfs_status(fs::vfs::Status status) {
    switch (status) {
        case fs::vfs::Status::Ok: return KU_STATUS_OK;
        case fs::vfs::Status::NotFound: return KU_STATUS_NOT_FOUND;
        case fs::vfs::Status::PermissionDenied:
        case fs::vfs::Status::ReadOnly: return KU_STATUS_ACCESS_DENIED;
        case fs::vfs::Status::OutOfMemory:
        case fs::vfs::Status::OpenFileTableFull:
            return KU_STATUS_OUT_OF_MEMORY;
        case fs::vfs::Status::InvalidArgument:
        case fs::vfs::Status::InvalidFlags:
        case fs::vfs::Status::InvalidPath:
        case fs::vfs::Status::InvalidHandle:
        case fs::vfs::Status::StaleHandle:
            return KU_STATUS_INVALID_ARGUMENT;
        case fs::vfs::Status::OutOfRange:
        case fs::vfs::Status::ArithmeticOverflow:
            return KU_STATUS_OUT_OF_RANGE;
        case fs::vfs::Status::Unsupported:
            return KU_STATUS_NOT_SUPPORTED;
        default: return KU_STATUS_IO_ERROR;
    }
}

bool copy_user_path(
    Context& context,
    uint64_t address,
    uint64_t length,
    char* output) {
    if (output == nullptr || length == 0U ||
        length > fs::vfs::MAX_PATH_LENGTH || length > SIZE_MAX ||
        !validate_user_buffer(context, address, static_cast<size_t>(length))) {
        return false;
    }
    const auto* source = reinterpret_cast<const char*>(
        static_cast<uintptr_t>(address));
    for (size_t index = 0U; index < static_cast<size_t>(length); ++index) {
        if (source[index] == '\0') return false;
        output[index] = source[index];
    }
    output[static_cast<size_t>(length)] = '\0';
    return output[0] == '/';
}

uint64_t allocate_user(Context& context, uint64_t requested) {
    if (requested == 0U || requested > UINT64_C(1024) * 1024U) {
        return static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
    }
    const uint64_t page_size = memory::virtual_memory::PAGE_SIZE;
    if (requested > UINT64_MAX - (page_size - 1U)) {
        return static_cast<uint64_t>(KU_STATUS_OUT_OF_RANGE);
    }
    const size_t page_count = static_cast<size_t>(
        (requested + page_size - 1U) / page_size);
    Allocation* allocation = nullptr;
    for (Allocation& candidate : context.allocations) {
        if (!candidate.active) {
            allocation = &candidate;
            break;
        }
    }
    if (allocation == nullptr ||
        page_count > elf::MAX_IMAGE_PAGES - context.image.page_count ||
        context.next_heap > kHeapEnd ||
        page_count > (kHeapEnd - context.next_heap) / page_size) {
        return static_cast<uint64_t>(KU_STATUS_OUT_OF_MEMORY);
    }
    const uint64_t base = context.next_heap;
    size_t mapped = 0U;
    for (; mapped < page_count; ++mapped) {
        if (elf::map_anonymous_page(
                &context.image,
                base + mapped * page_size,
                memory::virtual_memory::MapFlags::Writable |
                    memory::virtual_memory::MapFlags::NoExecute) !=
            elf::Status::Ok) {
            while (mapped != 0U) {
                --mapped;
                static_cast<void>(elf::unmap_owned_page(
                    &context.image, base + mapped * page_size));
            }
            return static_cast<uint64_t>(KU_STATUS_OUT_OF_MEMORY);
        }
    }
    allocation->address = base;
    allocation->page_count = page_count;
    allocation->active = true;
    context.next_heap += page_count * page_size;
    return base;
}

ku_status_t free_user(Context& context, uint64_t address) {
    Allocation* allocation = nullptr;
    for (Allocation& candidate : context.allocations) {
        if (candidate.active && candidate.address == address) {
            allocation = &candidate;
            break;
        }
    }
    if (allocation == nullptr) return KU_STATUS_INVALID_ARGUMENT;
    const uint64_t page_size = memory::virtual_memory::PAGE_SIZE;
    for (size_t index = 0U; index < allocation->page_count; ++index) {
        if (elf::unmap_owned_page(
                &context.image,
                allocation->address + index * page_size) != elf::Status::Ok) {
            return KU_STATUS_IO_ERROR;
        }
    }
    *allocation = {};
    return KU_STATUS_OK;
}

void finish_from_interrupt(
    Context& context,
    arch::x86_64::interrupts::InterruptFrame& frame,
    int32_t exit_code) {
    context.result.exit_code = exit_code;
    context.active = false;
    frame.rip = reinterpret_cast<uint64_t>(
        &x86_64_interrupt_return_to_kernel);
    frame.cs = arch::x86_64::gdt::KERNEL_CODE_SELECTOR;
    // In 64-bit mode IRET validates the complete five-word frame left by the
    // outer-privilege interrupt even when the target CS is ring 0. Replace the
    // user SS:RSP pair as well, otherwise the user selector (0x1b) causes #GP.
    frame.rsp = context.return_state.kernel_rsp;
    frame.ss = arch::x86_64::gdt::KERNEL_DATA_SELECTOR;
    // IF remains clear until the assembly return path is back on the original
    // kernel stack and restores the launcher's complete RFLAGS value.
    frame.rflags = UINT64_C(0x2);
    // The trampoline receives the launcher's flags in a caller-saved register
    // after the interrupt stub has restored the rewritten frame.
    frame.rdi = context.return_state.kernel_rflags;
}

void syscall_handler(
    arch::x86_64::interrupts::InterruptFrame& frame) {
    Context* context = current_context();
    if (context == nullptr ||
        (frame.cs & 3U) != 3U) {
        if ((frame.cs & 3U) == 3U) log_missing_context();
        frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);
        return;
    }
    if ((frame.cs & UINT64_C(0xFFFF)) ==
            arch::x86_64::gdt::USER_CODE_SELECTOR &&
        (frame.ss & UINT64_C(0xFFFF)) ==
            arch::x86_64::gdt::USER_DATA_SELECTOR) {
        context->result.entered_ring3 = true;
    }

    switch (frame.rax) {
        case KU_SYS_EXIT:
            finish_from_interrupt(
                *context,
                frame,
                static_cast<int32_t>(frame.rdi));
            return;
        case KU_SYS_WRITE: {
            const uint64_t descriptor = frame.rdi;
            const uint64_t user_buffer = frame.rsi;
            const uint64_t requested = frame.rdx;
            if (descriptor != 1U && descriptor != 2U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_NOT_FOUND);
                return;
            }
            if (requested > kMaximumWrite || requested > SIZE_MAX) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_RANGE);
                return;
            }
            const size_t count = static_cast<size_t>(requested);
            if (!validate_user_buffer(*context, user_buffer, count)) {
                context->result.invalid_pointer_rejected = true;
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const auto* bytes = reinterpret_cast<const char*>(
                static_cast<uintptr_t>(user_buffer));
            // A trap gate leaves IF set. Keep each write contiguous so two
            // preempted processes cannot splice their serial/terminal lines.
            const uint64_t interrupt_flags = save_and_disable_interrupts();
            for (size_t index = 0U; index < count; ++index) {
                terminal::put(bytes[index]);
            }
            restore_interrupts(interrupt_flags);
            context->result.bytes_written += count;
            frame.rax = count;
            return;
        }
        case KU_SYS_READ: {
            const uint64_t descriptor = frame.rdi;
            const uint64_t user_buffer = frame.rsi;
            const uint64_t requested = frame.rdx;
            if (requested > kMaximumWrite || requested > SIZE_MAX) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_RANGE);
                return;
            }
            const size_t count = static_cast<size_t>(requested);
            if (!validate_user_buffer(
                    *context, user_buffer, count, true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            if (descriptor == 0U) {
                if (count == 0U) {
                    frame.rax = 0U;
                    return;
                }
                auto* output = reinterpret_cast<char*>(
                    static_cast<uintptr_t>(user_buffer));
                size_t bytes_read = 0U;
                while (bytes_read < count &&
                       user::console::try_read(&output[bytes_read])) {
                    ++bytes_read;
                }
                frame.rax = bytes_read == 0U
                    ? static_cast<uint64_t>(KU_STATUS_WOULD_BLOCK)
                    : bytes_read;
                return;
            }
            HandleSlot* handle = decode_handle(*context, descriptor);
            if (handle == nullptr) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            size_t bytes_read = 0U;
            const fs::vfs::Status status = fs::root_volume::read(
                handle->file,
                reinterpret_cast<void*>(static_cast<uintptr_t>(user_buffer)),
                count,
                &bytes_read);
            frame.rax = status == fs::vfs::Status::Ok
                ? bytes_read
                : static_cast<uint64_t>(vfs_status(status));
            return;
        }
        case KU_SYS_OPEN: {
            if (frame.rdx != KU_OPEN_READ) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_ACCESS_DENIED);
                return;
            }
            char path[fs::vfs::MAX_PATH_LENGTH + 1U]{};
            if (!copy_user_path(*context, frame.rdi, frame.rsi, path)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            size_t slot_index = kMaximumHandles;
            for (size_t index = 0U; index < kMaximumHandles; ++index) {
                if (!context->handles[index].active) {
                    slot_index = index;
                    break;
                }
            }
            if (slot_index == kMaximumHandles) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_MEMORY);
                return;
            }
            HandleSlot& slot = context->handles[slot_index];
            fs::vfs::OpenFileHandle file{};
            const fs::vfs::Status status = fs::root_volume::open(
                path, fs::vfs::OpenFlags::Read, &file);
            if (status != fs::vfs::Status::Ok) {
                frame.rax = static_cast<uint64_t>(vfs_status(status));
                return;
            }
            ++slot.generation;
            if (slot.generation == 0U) ++slot.generation;
            slot.file = file;
            slot.active = true;
            frame.rax = encode_handle(slot_index, slot.generation);
            return;
        }
        case KU_SYS_CLOSE: {
            HandleSlot* slot = decode_handle(*context, frame.rdi);
            if (slot == nullptr) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const fs::vfs::Status status =
                fs::root_volume::close(slot->file);
            if (status == fs::vfs::Status::Ok) {
                slot->active = false;
                slot->file = {};
            }
            frame.rax = static_cast<uint64_t>(vfs_status(status));
            return;
        }
        case KU_SYS_ALLOC:
            frame.rax = allocate_user(*context, frame.rdi);
            return;
        case KU_SYS_FREE:
            frame.rax = static_cast<uint64_t>(free_user(*context, frame.rdi));
            return;
        case KU_SYS_GETPID:
            context->result.observed_pid = context->pid;
            frame.rax = context->pid;
            return;
        case KU_SYS_GETTID:
            context->result.observed_tid = context->tid;
            frame.rax = context->tid;
            return;
        case KU_SYS_YIELD:
            frame.rax = threading::request_yield() == threading::Status::Ok
                ? KU_STATUS_OK
                : KU_STATUS_BAD_STATE;
            return;
        case KU_SYS_SLEEP:
            frame.rax = threading::sleep_current(frame.rdi) ==
                    threading::Status::Ok
                ? KU_STATUS_OK
                : KU_STATUS_INVALID_ARGUMENT;
            return;
        case KU_SYS_SPAWN: {
            char path[fs::vfs::MAX_PATH_LENGTH + 1U]{};
            if (!copy_user_path(*context, frame.rdi, frame.rsi, path)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            process::ProcessId pid = process::INVALID_PROCESS_ID;
            const process::Status status = process::spawn(path, &pid);
            frame.rax = status == process::Status::Ok
                ? pid
                : static_cast<uint64_t>(
                    status == process::Status::CapacityReached
                        ? KU_STATUS_OUT_OF_MEMORY
                        : KU_STATUS_BAD_STATE);
            return;
        }
        case KU_SYS_WAIT: {
            if (!validate_user_buffer(
                    *context, frame.rsi, sizeof(int32_t), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            int32_t exit_code = 0;
            const process::Status status = process::wait(
                static_cast<process::ProcessId>(frame.rdi), &exit_code);
            if (status == process::Status::Ok) {
                *reinterpret_cast<int32_t*>(
                    static_cast<uintptr_t>(frame.rsi)) = exit_code;
                frame.rax = KU_STATUS_OK;
            } else if (status == process::Status::WouldBlock) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_WOULD_BLOCK);
            } else if (status == process::Status::NotFound) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_NOT_FOUND);
            } else {
                frame.rax = static_cast<uint64_t>(KU_STATUS_ACCESS_DENIED);
            }
            return;
        }
        case KU_SYS_UI_CREATE: {
            if (context->ui.active || !windowing::initialized() ||
                frame.rsi == 0U || frame.rsi > 32U || frame.rsi > SIZE_MAX ||
                !validate_user_buffer(
                    *context, frame.rdi, static_cast<size_t>(frame.rsi)) ||
                !validate_user_buffer(
                    *context, frame.rdx, sizeof(ku_ui_window_options))) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);
                return;
            }
            const auto* options = reinterpret_cast<const ku_ui_window_options*>(
                static_cast<uintptr_t>(frame.rdx));
            if (options->structure_size != sizeof(ku_ui_window_options)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            char title[33]{};
            const auto* user_title = reinterpret_cast<const char*>(
                static_cast<uintptr_t>(frame.rdi));
            for (size_t index = 0U;
                 index < static_cast<size_t>(frame.rsi); ++index) {
                if (user_title[index] == '\0' ||
                    static_cast<uint8_t>(user_title[index]) < 0x20U ||
                    static_cast<uint8_t>(user_title[index]) > 0x7EU) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                    return;
                }
                title[index] = user_title[index];
            }
            context->ui = {};
            context->ui.frame.structure_size = sizeof(ku_ui_frame);
            context->ui.frame.background_rgb = UINT32_C(0x111827);
            context->ui.frame.foreground_rgb = UINT32_C(0xE5E7EB);
            context->ui.frame.accent_rgb = UINT32_C(0xF97316);
            const windowing::Status status = windowing::create_window(
                title, context->pid,
                {options->x, options->y, options->width, options->height},
                draw_user_window, input_user_window, context,
                &context->ui.window);
            if (status != windowing::Status::Ok) {
                frame.rax = static_cast<uint64_t>(
                    status == windowing::Status::CapacityReached
                        ? KU_STATUS_OUT_OF_MEMORY
                        : KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            context->ui.active = true;
            frame.rax = context->ui.window;
            return;
        }
        case KU_SYS_UI_PRESENT: {
            if (!context->ui.active || frame.rdi != context->ui.window ||
                frame.rdx != sizeof(ku_ui_frame) ||
                !validate_user_buffer(
                    *context, frame.rsi, sizeof(ku_ui_frame))) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const auto* user_frame = reinterpret_cast<const ku_ui_frame*>(
                static_cast<uintptr_t>(frame.rsi));
            if (user_frame->structure_size != sizeof(ku_ui_frame) ||
                user_frame->line_count > KU_UI_MAX_LINES ||
                user_frame->reserved != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_CORRUPT_DATA);
                return;
            }
            for (uint32_t line = 0U; line < user_frame->line_count; ++line) {
                bool terminated = false;
                for (size_t character = 0U;
                     character < KU_UI_LINE_CAPACITY; ++character) {
                    if (user_frame->lines[line][character] == '\0') {
                        terminated = true;
                        break;
                    }
                }
                if (!terminated) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_CORRUPT_DATA);
                    return;
                }
            }
            context->ui.frame = *user_frame;
            windowing::invalidate();
            frame.rax = KU_STATUS_OK;
            return;
        }
        case KU_SYS_UI_POLL: {
            if (!context->ui.active || frame.rdi != context->ui.window ||
                frame.rdx != sizeof(ku_ui_event) ||
                !validate_user_buffer(
                    *context, frame.rsi, sizeof(ku_ui_event), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* user_event = reinterpret_cast<ku_ui_event*>(
                static_cast<uintptr_t>(frame.rsi));
            windowing::WindowInfo info{};
            if (windowing::query(context->ui.window, &info) !=
                windowing::Status::Ok) {
                *user_event = {};
                user_event->structure_size = sizeof(ku_ui_event);
                user_event->type = KU_UI_EVENT_CLOSE;
                context->ui.active = false;
                context->ui.window = windowing::INVALID_WINDOW;
                frame.rax = KU_STATUS_OK;
                return;
            }
            if (context->ui.tail == context->ui.head) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_WOULD_BLOCK);
                return;
            }
            *user_event = context->ui.events[context->ui.tail];
            context->ui.tail = static_cast<uint8_t>(
                (context->ui.tail + 1U) % kMaximumUiEvents);
            frame.rax = KU_STATUS_OK;
            return;
        }
        case KU_SYS_UI_CLOSE: {
            if (!context->ui.active || frame.rdi != context->ui.window) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const windowing::Status status =
                windowing::close(context->ui.window);
            context->ui.active = false;
            context->ui.window = windowing::INVALID_WINDOW;
            frame.rax = status == windowing::Status::Ok
                ? static_cast<uint64_t>(KU_STATUS_OK)
                : static_cast<uint64_t>(KU_STATUS_NOT_FOUND);
            return;
        }
        default:
            frame.rax = static_cast<uint64_t>(KU_STATUS_NOT_SUPPORTED);
            return;
    }
}

Status cleanup(Context& context) {
    Status result = Status::Ok;
    if (context.ui.active) {
        const windowing::Status ui_status =
            windowing::close(context.ui.window);
        if (ui_status != windowing::Status::Ok &&
            ui_status != windowing::Status::NotFound) {
            result = Status::CleanupFailed;
        }
        context.ui.active = false;
        context.ui.window = windowing::INVALID_WINDOW;
    }
    for (HandleSlot& handle : context.handles) {
        if (!handle.active) continue;
        if (fs::root_volume::close(handle.file) != fs::vfs::Status::Ok) {
            result = Status::CleanupFailed;
        }
        handle.active = false;
    }
    static_cast<void>(threading::bind_address_space(nullptr));
    if (memory::kernel_virtual_memory::activate_kernel() !=
        memory::kernel_virtual_memory::Status::Ok) {
        result = Status::CleanupFailed;
    }
    unregister_context(&context);
    if (context.image.address_space != nullptr &&
        elf::unload(&context.image) != elf::Status::Ok) {
        result = Status::CleanupFailed;
    }
    if (context.address_space.initialized &&
        memory::kernel_virtual_memory::destroy_address_space(
            &context.address_space) !=
            memory::kernel_virtual_memory::Status::Ok) {
        result = Status::CleanupFailed;
    }
    // A global PMM delta is not a valid ownership proof while other Ring-3
    // processes allocate concurrently. The ELF Image is the authoritative
    // owner of executable, stack and heap pages; successful unload plus a
    // cleared private address-space object proves this context released them.
    context.result.resources_reclaimed = result == Status::Ok &&
        context.image.page_count == 0U && !context.image.loaded &&
        context.image.address_space == nullptr &&
        !context.address_space.initialized &&
        context.address_space.root_frame == nullptr;
    if (!context.result.resources_reclaimed && result == Status::Ok) {
        result = Status::ResourceLeak;
    }
    return result;
}

} // namespace

Status initialize() {
    if (g_initialized) {
        return Status::AlreadyInitialized;
    }
    if (!enable_memory_protection()) {
        return Status::CpuUnsupported;
    }
    for (Context*& context : g_contexts) {
        context = nullptr;
    }
    if (!arch::x86_64::interrupts::register_handler(
            kSyscallVector, syscall_handler) ||
        !arch::x86_64::interrupts::set_gate_privilege(
            kSyscallVector,
            arch::x86_64::interrupts::GatePrivilege::User) ||
        !arch::x86_64::interrupts::set_gate_type(
            kSyscallVector,
            arch::x86_64::interrupts::GateType::Trap)) {
        arch::x86_64::interrupts::unregister_handler(kSyscallVector);
        return Status::InterruptRegistrationFailed;
    }
    g_initialized = true;
    return Status::Ok;
}

bool initialized() {
    return g_initialized;
}

void set_process_identity(uint64_t pid) {
    if (current_context() == nullptr) {
        g_process_identity = pid;
    }
}

uint64_t process_identity() {
    Context* context = current_context();
    return context == nullptr ? g_process_identity : context->pid;
}

Status run(const char* path, Result* result) {
    return run(path, g_process_identity, result);
}

Status run(const char* path, uint64_t pid, Result* result) {
    if (result != nullptr) {
        *result = {};
        result->fault_vector = 0xFFU;
    }
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (path == nullptr || result == nullptr) {
        return Status::FileReadFailed;
    }
    if (!fs::root_volume::mounted()) {
        return Status::RootUnavailable;
    }

    fs::vfs::FileStat file_info{};
    fs::vfs::Status file_status = fs::root_volume::stat(path, &file_info);
    if (file_status == fs::vfs::Status::NotFound) {
        return Status::FileNotFound;
    }
    if (file_status != fs::vfs::Status::Ok ||
        file_info.type != fs::vfs::NodeType::Regular) {
        return Status::FileReadFailed;
    }
    if (file_info.size == 0U || file_info.size > kMaximumExecutableSize ||
        file_info.size > SIZE_MAX) {
        return Status::FileTooLarge;
    }

    const size_t file_size = static_cast<size_t>(file_info.size);
    void* file_bytes = memory::kmalloc(file_size, 16U);
    if (file_bytes == nullptr) {
        return Status::OutOfMemory;
    }
    size_t bytes_read = 0U;
    uint64_t measured_size = 0U;
    file_status = fs::root_volume::read_file(
        path,
        file_bytes,
        file_size,
        &bytes_read,
        &measured_size);
    if (file_status != fs::vfs::Status::Ok || bytes_read != file_size ||
        measured_size != file_info.size) {
        memory::kfree(file_bytes);
        return Status::FileReadFailed;
    }

    Context context{};
    context.pid = pid;
    context.tid = threading::current();
    context.next_heap = kHeapBase;
    context.result.fault_vector = 0xFFU;
    if (memory::kernel_virtual_memory::create_address_space(
            &context.address_space) !=
        memory::kernel_virtual_memory::Status::Ok) {
        memory::kfree(file_bytes);
        return Status::AddressSpaceFailed;
    }
    const elf::Status load_status = elf::load(
        file_bytes,
        file_size,
        &context.address_space.address_space,
        &context.image);
    memory::kfree(file_bytes);
    if (load_status != elf::Status::Ok) {
        static_cast<void>(cleanup(context));
        log::write(log::Level::Error, "ELF", elf::status_message(load_status));
        return Status::ElfLoadFailed;
    }

    for (size_t index = 0U; index < kStackPages; ++index) {
        const uint64_t page =
            kStackTop - (index + 1U) * memory::virtual_memory::PAGE_SIZE;
        if (elf::map_anonymous_page(
                &context.image,
                page,
                memory::virtual_memory::MapFlags::Writable |
                    memory::virtual_memory::MapFlags::NoExecute) !=
            elf::Status::Ok) {
            static_cast<void>(cleanup(context));
            return Status::StackMappingFailed;
        }
    }

    const uint8_t fault_exit_code[] = {
        0xB8, 0x01, 0x00, 0x00, 0x00, // mov $KU_SYS_EXIT, %eax
        0xCD, 0x80,                   // int $0x80
        0x0F, 0x0B                    // ud2 (SYS_EXIT must not return)
    };
    if (elf::map_anonymous_page(
            &context.image,
            kFaultTrampoline,
            memory::virtual_memory::MapFlags::None,
            fault_exit_code,
            sizeof(fault_exit_code)) != elf::Status::Ok) {
        static_cast<void>(cleanup(context));
        return Status::TrampolineMappingFailed;
    }

    context.return_state.kernel_rflags = save_and_disable_interrupts();
    context.active = true;
    if (!register_context(&context)) {
        context.active = false;
        const uint64_t saved_flags = context.return_state.kernel_rflags;
        static_cast<void>(cleanup(context));
        __asm__ volatile(
            "pushq %0; popfq" : : "r"(saved_flags) : "memory", "cc");
        return Status::Busy;
    }
    const threading::Status bind_status = threading::bind_address_space(
        &context.address_space, kStackTop - 16U);
    if (bind_status != threading::Status::Ok &&
        bind_status != threading::Status::NotInitialized &&
        bind_status != threading::Status::NotRunning) {
        context.active = false;
        const uint64_t saved_flags = context.return_state.kernel_rflags;
        static_cast<void>(cleanup(context));
        __asm__ volatile(
            "pushq %0; popfq" : : "r"(saved_flags) : "memory", "cc");
        return Status::AddressSpaceActivationFailed;
    }
    if (memory::kernel_virtual_memory::activate(&context.address_space) !=
        memory::kernel_virtual_memory::Status::Ok) {
        context.active = false;
        const uint64_t saved_flags = context.return_state.kernel_rflags;
        static_cast<void>(cleanup(context));
        __asm__ volatile(
            "pushq %0; popfq" : : "r"(saved_flags) : "memory", "cc");
        return Status::AddressSpaceActivationFailed;
    }
    x86_64_enter_user(
        context.image.entry,
        kStackTop - 16U,
        &context.return_state);

    const Status cleanup_status = cleanup(context);
    *result = context.result;
    return cleanup_status;
}

bool handle_exception(
    arch::x86_64::interrupts::InterruptFrame& frame) {
    Context* context = current_context();
    if (context == nullptr ||
        (frame.cs & 3U) != 3U || frame.vector >= 32U) {
        return false;
    }
    context->result.fault_vector = static_cast<uint8_t>(frame.vector);
    context->result.fault_isolated = true;
    frame.rip = kFaultTrampoline;
    frame.rdi = UINT64_C(128) + frame.vector;
    frame.rax = KU_SYS_EXIT;
    return true;
}

bool request_termination(uint64_t pid, int32_t exit_code) {
    for (Context* context : g_contexts) {
        if (context == nullptr || !context->active || context->pid != pid ||
            context->tid == threading::INVALID_THREAD_ID) {
            continue;
        }
        return threading::redirect_user(
                   context->tid,
                   kFaultTrampoline,
                   KU_SYS_EXIT,
                   static_cast<uint64_t>(static_cast<int64_t>(exit_code))) ==
            threading::Status::Ok;
    }
    return false;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::CpuUnsupported: return "CPU lacks NX support";
        case Status::InterruptRegistrationFailed:
            return "cannot install ring-3 syscall gate";
        case Status::NotInitialized: return "user runtime not initialized";
        case Status::Busy: return "another user image is active";
        case Status::RootUnavailable: return "persistent root unavailable";
        case Status::FileNotFound: return "user ELF not found";
        case Status::FileTooLarge: return "user ELF is empty or too large";
        case Status::FileReadFailed: return "cannot read user ELF";
        case Status::OutOfMemory: return "out of kernel memory";
        case Status::AddressSpaceFailed: return "address-space creation failed";
        case Status::ElfLoadFailed: return "ELF validation or load failed";
        case Status::StackMappingFailed: return "user stack mapping failed";
        case Status::TrampolineMappingFailed:
            return "fault trampoline mapping failed";
        case Status::AddressSpaceActivationFailed:
            return "address-space activation failed";
        case Status::CleanupFailed: return "user image cleanup failed";
        case Status::ResourceLeak: return "user image leaked PMM frames";
    }
    return "unknown user runtime status";
}

} // namespace user::runtime
