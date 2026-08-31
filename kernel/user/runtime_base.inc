#include "runtime.hpp"

#include <kurogane/audio.h>
#include <kurogane/desktop.h>
#include <kurogane/filesystem.h>
#include <kurogane/network.h>
#include <kurogane/status.h>
#include <kurogane/syscall.h>
#include <kurogane/system.h>
#include <kurogane/ui.h>

#include "elf_loader.hpp"
#include "console.hpp"
#include "../arch/x86_64/gdt.hpp"
#include "../core/log.hpp"
#include "../core/system_metrics.hpp"
#include "../drivers/audio/ac97.hpp"
#include "../fs/root_volume.hpp"
#include "../memory/allocator.hpp"
#include "../memory/kernel_virtual_memory.hpp"
#include "../memory/physical_memory.hpp"
#include "../net/service.hpp"
#include "../net/socket.hpp"
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

bool native_text_terminated(const char text[KU_UI_NATIVE_TEXT_CAPACITY]) {
    for (size_t index = 0U; index < KU_UI_NATIVE_TEXT_CAPACITY; ++index) {
        if (text[index] == '\0') return true;
    }
    return false;
}

bool native_command_valid(
    const ku_ui_native_command& command, uint32_t version) {
    const uint32_t maximum_type = version >= KU_UI_NATIVE_VERSION_3
        ? KU_UI_NATIVE_NOTICE
        : (version >= KU_UI_NATIVE_VERSION_2
            ? KU_UI_NATIVE_TILE : KU_UI_NATIVE_SEPARATOR);
    uint32_t allowed_flags = KU_UI_NATIVE_SELECTED | KU_UI_NATIVE_DISABLED;
    if (version >= KU_UI_NATIVE_VERSION_2) {
        allowed_flags |= KU_UI_NATIVE_PINNED | KU_UI_NATIVE_RUNNING;
    }
    if (command.type < KU_UI_NATIVE_PANEL || command.type > maximum_type ||
        (command.flags & ~allowed_flags) != 0U || command.reserved != 0U ||
        !native_text_terminated(command.text)) {
        return false;
    }
    if (command.x < 0 || command.y < 0 || command.height <= 0 || command.width < 0 ||
        command.x > KU_UI_NATIVE_COORD_LIMIT || command.y > KU_UI_NATIVE_COORD_LIMIT ||
        command.width > KU_UI_NATIVE_COORD_LIMIT || command.height > KU_UI_NATIVE_COORD_LIMIT) {
        return false;
    }
    if ((command.type == KU_UI_NATIVE_PROGRESS ||
         command.type == KU_UI_NATIVE_METRIC ||
         command.type == KU_UI_NATIVE_NOTICE) && command.maximum == 0U) return false;
    if (command.type == KU_UI_NATIVE_NOTICE &&
        (command.value < 1U || command.value > 4U || command.maximum != 4U)) return false;
    if (command.type == KU_UI_NATIVE_TILE) {
        const uint32_t icon_limit = version >= KU_UI_NATIVE_VERSION_3
            ? KU_UI_NATIVE_ICON_COUNT : UINT32_C(8);
        if (command.value >= icon_limit) return false;
    }
    return true;
}

bool native_frame_valid(const ku_ui_native_frame& frame) {
    if (frame.structure_size != sizeof(ku_ui_native_frame) ||
        frame.magic != KU_UI_NATIVE_MAGIC ||
        (frame.version != KU_UI_NATIVE_VERSION_1 &&
         frame.version != KU_UI_NATIVE_VERSION_2 &&
         frame.version != KU_UI_NATIVE_VERSION_3) ||
        frame.command_count > KU_UI_NATIVE_MAX_COMMANDS || frame.reserved != 0U) {
        return false;
    }
    for (uint32_t index = 0U; index < frame.command_count; ++index) {
        if (!native_command_valid(frame.commands[index], frame.version)) return false;
    }
    return true;
}

ui::Rect native_bounds(
    const ui::Rect& content,
    const ku_ui_native_command& command) {
    int32_t width = command.width;
    if (width == 0) width = content.width - command.x - 16;
    if (width < 1) width = 1;
    const int32_t available_width = content.width - command.x;
    if (available_width > 0 && width > available_width) width = available_width;
    int32_t height = command.height;
    const int32_t available_height = content.height - command.y;
    if (available_height > 0 && height > available_height) height = available_height;
    if (height < 1) height = 1;
    return {content.x + command.x, content.y + command.y, width, height};
}

void split_native_tile_text(
    const char* text, char title[32], char subtitle[32]) {
    size_t source = 0U;
    size_t title_size = 0U;
    size_t subtitle_size = 0U;
    bool second = false;
    title[0] = '\0';
    subtitle[0] = '\0';
    if (text == nullptr) return;
    while (source < KU_UI_NATIVE_TEXT_CAPACITY && text[source] != '\0') {
        const char value = text[source++];
        if (!second && value == '\n') {
            second = true;
            continue;
        }
        if (!second) {
            if (title_size + 1U < 32U) title[title_size++] = value;
        } else if (subtitle_size + 1U < 32U) {
            subtitle[subtitle_size++] = value;
        }
    }
    title[title_size] = '\0';
    subtitle[subtitle_size] = '\0';
}

void draw_native_frame(
    windowing::WindowId window,
    const ku_ui_native_frame& frame,
    const ui::Rect& content,
    bool focused) {
    windowing::InteractionSnapshot interaction{};
    const bool pointer_owner =
        windowing::interaction_snapshot(&interaction) == windowing::Status::Ok &&
        interaction.hovered == window;
    const int32_t pointer_x = input::pointer_x();
    const int32_t pointer_y = input::pointer_y();
    const bool primary_down =
        (input::pointer_buttons() & drivers::mouse::Left) != 0U;
    const graphics::Color background = frame.background_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color foreground = frame.foreground_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color accent = frame.accent_rgb & UINT32_C(0xFFFFFF);
    graphics::fill_rect(content.x, content.y, content.width, content.height, background);
    if (content.width > 48) {
        graphics::fill_rect(content.x + 16, content.y + 7,
                            content.width - 32, 1,
                            focused ? accent : graphics::rgb(45, 47, 52));
    }

    for (uint32_t index = 0U; index < frame.command_count; ++index) {
        const ku_ui_native_command& command = frame.commands[index];
        const ui::Rect bounds = native_bounds(content, command);
        const bool selected = (command.flags & KU_UI_NATIVE_SELECTED) != 0U;
        const bool disabled = (command.flags & KU_UI_NATIVE_DISABLED) != 0U;
        const bool interactive = !disabled &&
            (command.type == KU_UI_NATIVE_BUTTON ||
             command.type == KU_UI_NATIVE_INPUT ||
             command.type == KU_UI_NATIVE_LIST_ITEM ||
             command.type == KU_UI_NATIVE_TILE);
        const bool hovered = pointer_owner && interactive &&
            ui::contains(bounds, pointer_x, pointer_y);
        const bool pressed = hovered && primary_down;
        switch (command.type) {
            case KU_UI_NATIVE_PANEL:
                ui::panel(bounds, true);
                graphics::draw_text(bounds.x + 12, bounds.y + 11, command.text,
                                    foreground, background, 1U, true);
                break;
            case KU_UI_NATIVE_LABEL:
                graphics::draw_text(bounds.x, bounds.y + 6, command.text,
                                    disabled ? ui::default_theme().text_muted : foreground,
                                    background, 1U, true);
                break;
            case KU_UI_NATIVE_BUTTON:
                ui::button(bounds, command.text, selected, hovered, pressed);
                break;
            case KU_UI_NATIVE_INPUT:
                ui::input_field(bounds, command.text, selected, hovered, pressed);
                break;
            case KU_UI_NATIVE_LIST_ITEM:
                ui::list_row(bounds, command.text, selected, disabled, hovered, pressed);
                break;
            case KU_UI_NATIVE_PROGRESS: {
                graphics::draw_text(bounds.x, bounds.y, command.text,
                                    foreground, background, 1U, true);
                const int32_t bar_y = bounds.y + 18;
                const int32_t bar_height = bounds.height > 24 ? bounds.height - 20 : 8;
                ui::progress({bounds.x, bar_y, bounds.width, bar_height},
                             command.value, command.maximum);
                break;
            }
            case KU_UI_NATIVE_SEPARATOR:
                ui::separator(bounds.x, bounds.y + bounds.height / 2, bounds.width);
                break;
            case KU_UI_NATIVE_METRIC: {
                char title[32];
                char detail[32];
                split_native_tile_text(command.text, title, detail);
                ui::metric_card(
                    bounds, title, detail, command.value, command.maximum);
                break;
            }
            case KU_UI_NATIVE_NOTICE: {
                char title[32];
                char detail[32];
                split_native_tile_text(command.text, title, detail);
                ui::notice_card(bounds, title, detail, command.value);
                break;
            }
            case KU_UI_NATIVE_TILE: {
                char title[32];
                char subtitle[32];
                split_native_tile_text(command.text, title, subtitle);
                ui::app_tile(
                    bounds, title, subtitle,
                    static_cast<ui::AppIcon>(command.value),
                    selected,
                    (command.flags & KU_UI_NATIVE_PINNED) != 0U,
                    (command.flags & KU_UI_NATIVE_RUNNING) != 0U,
                    hovered, pressed);
                break;
            }
            default:
                break;
        }
    }
}

void draw_user_window(
    windowing::WindowId,
    const ui::Rect& content,
    bool focused,
    void* opaque) {
    auto* context = static_cast<Context*>(opaque);
    if (context == nullptr || !context->active || !context->ui.active) return;

    windowing::SurfaceView retained{};
    if (windowing::read_surface(context->ui.window, &retained) == windowing::Status::Ok &&
        retained.data != nullptr && retained.size == sizeof(ku_ui_native_frame)) {
        ku_ui_native_frame native{};
        auto* destination = reinterpret_cast<uint8_t*>(&native);
        for (size_t index = 0U; index < sizeof(native); ++index) {
            destination[index] = retained.data[index];
        }
        if (native_frame_valid(native)) {
            draw_native_frame(context->ui.window, native, content, focused);
            return;
        }
    }

    ku_ui_frame frame = context->ui.frame;
    if (retained.data != nullptr && retained.size == sizeof(frame)) {
        auto* destination = reinterpret_cast<uint8_t*>(&frame);
        for (size_t index = 0U; index < sizeof(frame); ++index) {
            destination[index] = retained.data[index];
        }
    }
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
    windowing::WindowId window,
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
        ui::Rect content{};
        if (windowing::content_geometry(window, &content) != windowing::Status::Ok) {
            return;
        }
        event.type = KU_UI_EVENT_POINTER;
        event.x = input_event.x - content.x;
        event.y = input_event.y - content.y;
        event.buttons = input_event.buttons;
    } else {
        return;
    }
    queue_user_event(*context, event);
}

bool g_initialized = false;
Context* g_contexts[kMaximumContexts]{};
Context g_context_storage[kMaximumContexts]{};
bool g_context_storage_used[kMaximumContexts]{};
uint64_t g_process_identity = 0U;
uint64_t g_audio_owner_pid = 0U;

struct SocketWaiter {
    uint64_t pid;
    threading::ThreadId tid;
    net::socket::Handle socket;
    uint32_t requested;
    uint64_t deadline_tick;
    bool finite_timeout;
    bool active;
};

SocketWaiter g_socket_waiters[kMaximumContexts]{};

ku_status_t socket_wait_status(net::socket::Status status) {
    using SocketStatus = net::socket::Status;
    switch (status) {
        case SocketStatus::Ok:
        case SocketStatus::AlreadyInitialized: return KU_STATUS_OK;
        case SocketStatus::InvalidArgument:
        case SocketStatus::StaleHandle: return KU_STATUS_INVALID_ARGUMENT;
        case SocketStatus::NotSupported: return KU_STATUS_NOT_SUPPORTED;
        case SocketStatus::CapacityReached: return KU_STATUS_OUT_OF_MEMORY;
        case SocketStatus::AccessDenied: return KU_STATUS_ACCESS_DENIED;
        case SocketStatus::AddressInUse: return KU_STATUS_ALREADY_EXISTS;
        case SocketStatus::WouldBlock: return KU_STATUS_WOULD_BLOCK;
        case SocketStatus::BufferTooSmall:
        case SocketStatus::PayloadTooLarge: return KU_STATUS_OUT_OF_RANGE;
        case SocketStatus::ConnectionRefused: return KU_STATUS_CONNECTION_REFUSED;
        case SocketStatus::ConnectionReset: return KU_STATUS_CONNECTION_RESET;
        case SocketStatus::TimedOut: return KU_STATUS_TIMED_OUT;
        case SocketStatus::NotInitialized:
        case SocketStatus::NotBound:
        case SocketStatus::NotConnected: return KU_STATUS_BAD_STATE;
        case SocketStatus::TransportError: return KU_STATUS_IO_ERROR;
    }
    return KU_STATUS_IO_ERROR;
}

void clear_socket_waiter(threading::ThreadId tid) {
    for (SocketWaiter& waiter : g_socket_waiters) {
        if (waiter.active && waiter.tid == tid) waiter = {};
    }
}

SocketWaiter* reserve_socket_waiter(threading::ThreadId tid) {
    clear_socket_waiter(tid);
    for (SocketWaiter& waiter : g_socket_waiters) {
        if (!waiter.active) return &waiter;
    }
    return nullptr;
}

void socket_wait_pre_dispatch() {
    const uint64_t now = threading::timer_ticks();
    for (SocketWaiter& waiter : g_socket_waiters) {
        if (!waiter.active) continue;
        threading::Stat stat{};
        if (threading::stat(waiter.tid, &stat) != threading::Status::Ok ||
            stat.process_id != waiter.pid || stat.state != threading::State::Blocked) {
            waiter = {};
            continue;
        }

        uint32_t ready = net::socket::ReadyNone;
        const net::socket::Status status = net::socket::readiness(
            waiter.pid, waiter.socket, waiter.requested, &ready);
        uint64_t completion = 0U;
        bool complete = false;
        if (status != net::socket::Status::Ok) {
            completion = static_cast<uint64_t>(socket_wait_status(status));
            complete = true;
        } else if ((ready & waiter.requested) != 0U) {
            completion = ready;
            complete = true;
        } else if (waiter.finite_timeout && now >= waiter.deadline_tick) {
            completion = static_cast<uint64_t>(KU_STATUS_TIMED_OUT);
            complete = true;
        }
        if (!complete) continue;
        const threading::ThreadId tid = waiter.tid;
        waiter = {};
        static_cast<void>(threading::wake_user(tid, completion));
    }
}

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

Context* acquire_context_storage() {
    const uint64_t flags = save_and_disable_interrupts();
    for (size_t index = 0U; index < kMaximumContexts; ++index) {
        if (g_context_storage_used[index]) continue;
        g_context_storage_used[index] = true;
        g_context_storage[index] = {};
        restore_interrupts(flags);
        return &g_context_storage[index];
    }
    restore_interrupts(flags);
    return nullptr;
}

void release_context_storage(Context* context) {
    if (context == nullptr) return;
    const uint64_t flags = save_and_disable_interrupts();
    for (size_t index = 0U; index < kMaximumContexts; ++index) {
        if (&g_context_storage[index] != context) continue;
        g_context_storage[index] = {};
        g_context_storage_used[index] = false;
        break;
    }
    restore_interrupts(flags);
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
    if (eax < UINT32_C(0x80000001)) return false;
    cpuid(UINT32_C(0x80000001), eax, ebx, ecx, edx);
    constexpr uint32_t nx_capability = UINT32_C(1) << 20U;
    if ((edx & nx_capability) == 0U) return false;

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
    if (!context.active) return false;
    if (size == 0U) return true;
    if (address < elf::USER_REGION_BASE ||
        address >= elf::USER_REGION_END ||
        size > elf::USER_REGION_END - address) return false;

    const uint64_t last = address + size - 1U;
    uint64_t page = address & ~(memory::virtual_memory::PAGE_SIZE - 1U);
    const uint64_t last_page = last & ~(memory::virtual_memory::PAGE_SIZE - 1U);
    for (;;) {
        memory::virtual_memory::Mapping mapping{};
        if (memory::virtual_memory::query_page(
                const_cast<memory::virtual_memory::AddressSpace*>(
                    &context.address_space.address_space),
                page,
                &mapping) != memory::virtual_memory::Status::Ok ||
            !memory::virtual_memory::has_flag(
                mapping.flags, memory::virtual_memory::MapFlags::User) ||
            (require_writable && !memory::virtual_memory::has_flag(
                mapping.flags, memory::virtual_memory::MapFlags::Writable))) {
            return false;
        }
        if (page == last_page) return true;
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

void sync_process_handle_count(Context& context) {
    uint32_t count = 0U;
    for (const HandleSlot& handle : context.handles) {
        if (handle.active) ++count;
    }
    static_cast<void>(process::set_handle_count(context.pid, count));
}

ku_status_t vfs_status(fs::vfs::Status status) {
    switch (status) {
        case fs::vfs::Status::Ok: return KU_STATUS_OK;
        case fs::vfs::Status::NotFound: return KU_STATUS_NOT_FOUND;
        case fs::vfs::Status::AlreadyExists: return KU_STATUS_ALREADY_EXISTS;
        case fs::vfs::Status::PermissionDenied:
        case fs::vfs::Status::ReadOnly:
        case fs::vfs::Status::RootProtected: return KU_STATUS_ACCESS_DENIED;
        case fs::vfs::Status::OutOfMemory:
        case fs::vfs::Status::OpenFileTableFull:
        case fs::vfs::Status::MountTableFull: return KU_STATUS_OUT_OF_MEMORY;
        case fs::vfs::Status::InvalidArgument:
        case fs::vfs::Status::InvalidFlags:
        case fs::vfs::Status::InvalidPath:
        case fs::vfs::Status::InvalidHandle:
        case fs::vfs::Status::StaleHandle:
        case fs::vfs::Status::NameTooLong:
        case fs::vfs::Status::PathTooLong:
        case fs::vfs::Status::PathTooDeep: return KU_STATUS_INVALID_ARGUMENT;
        case fs::vfs::Status::OutOfRange:
        case fs::vfs::Status::ArithmeticOverflow:
        case fs::vfs::Status::BufferTooSmall: return KU_STATUS_OUT_OF_RANGE;
        case fs::vfs::Status::Unsupported:
        case fs::vfs::Status::NotSeekable:
        case fs::vfs::Status::CrossDevice: return KU_STATUS_NOT_SUPPORTED;
        case fs::vfs::Status::EndOfDirectory: return KU_STATUS_END_OF_STREAM;
        case fs::vfs::Status::NotInitialized:
        case fs::vfs::Status::NoRootFilesystem:
        case fs::vfs::Status::NotDirectory:
        case fs::vfs::Status::IsDirectory:
        case fs::vfs::Status::DirectoryNotEmpty:
        case fs::vfs::Status::Busy: return KU_STATUS_BAD_STATE;
        default: return KU_STATUS_IO_ERROR;
    }
}

ku_status_t network_status(net::Status status) {
    switch (status) {
        case net::Status::Ok: return KU_STATUS_OK;
        case net::Status::WouldBlock:
        case net::Status::NeighborResolutionPending: return KU_STATUS_WOULD_BLOCK;
        case net::Status::NameNotFound: return KU_STATUS_NOT_FOUND;
        case net::Status::InvalidArgument:
        case net::Status::InvalidConfiguration: return KU_STATUS_INVALID_ARGUMENT;
        case net::Status::BufferTooSmall:
        case net::Status::PayloadTooLarge: return KU_STATUS_OUT_OF_RANGE;
        case net::Status::NotInitialized:
        case net::Status::NotConfigured: return KU_STATUS_BAD_STATE;
        case net::Status::UnsupportedProtocol:
        case net::Status::UnsupportedFragment: return KU_STATUS_NOT_SUPPORTED;
        default: return KU_STATUS_IO_ERROR;
    }
}

ku_status_t audio_status(drivers::audio::ac97::Status status) {
    using AudioStatus = drivers::audio::ac97::Status;
    switch (status) {
        case AudioStatus::Ok: return KU_STATUS_OK;
        case AudioStatus::AlreadyInitialized: return KU_STATUS_OK;
        case AudioStatus::NotInitialized:
        case AudioStatus::NoDevice: return KU_STATUS_BAD_STATE;
        case AudioStatus::UnsupportedDevice: return KU_STATUS_NOT_SUPPORTED;
        case AudioStatus::InvalidArgument: return KU_STATUS_INVALID_ARGUMENT;
        case AudioStatus::BufferTooLarge: return KU_STATUS_OUT_OF_RANGE;
        case AudioStatus::DeviceBusy: return KU_STATUS_WOULD_BLOCK;
        case AudioStatus::InvalidBar:
        case AudioStatus::DmaAllocationFailed:
        case AudioStatus::CodecResetFailed:
        case AudioStatus::DeviceFault: return KU_STATUS_IO_ERROR;
    }
    return KU_STATUS_IO_ERROR;
}

bool fixed_string_terminated(const char* text, size_t capacity) {
    if (text == nullptr || capacity == 0U) return false;
    for (size_t index = 0U; index < capacity; ++index) {
        if (text[index] == '\0') return index != 0U;
    }
    return false;
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
    return output[0] != '\0';
}

bool decode_open_flags(uint64_t raw, fs::vfs::OpenFlags* output) {
    if (output == nullptr) return false;
    constexpr uint64_t known = KU_OPEN_READ | KU_OPEN_WRITE |
        KU_OPEN_APPEND | KU_OPEN_DIRECTORY;
    if ((raw & ~known) != 0U) return false;
    const bool readable = (raw & KU_OPEN_READ) != 0U;
    const bool writable = (raw & KU_OPEN_WRITE) != 0U;
    if ((!readable && !writable) ||
        ((raw & KU_OPEN_APPEND) != 0U && !writable)) {
        return false;
    }
    fs::vfs::OpenFlags flags = fs::vfs::OpenFlags::None;
    if (readable) flags = flags | fs::vfs::OpenFlags::Read;
    if (writable) flags = flags | fs::vfs::OpenFlags::Write;
    if ((raw & KU_OPEN_APPEND) != 0U) {
        flags = flags | fs::vfs::OpenFlags::Append;
    }
    if ((raw & KU_OPEN_DIRECTORY) != 0U) {
        flags = flags | fs::vfs::OpenFlags::Directory;
    }
    *output = flags;
    return true;
}

uint32_t public_file_type(fs::vfs::NodeType type) {
    switch (type) {
        case fs::vfs::NodeType::Regular: return KU_FILE_TYPE_REGULAR;
        case fs::vfs::NodeType::Directory: return KU_FILE_TYPE_DIRECTORY;
        case fs::vfs::NodeType::Device: return KU_FILE_TYPE_DEVICE;
        case fs::vfs::NodeType::Pipe: return KU_FILE_TYPE_PIPE;
        case fs::vfs::NodeType::MountPoint: return KU_FILE_TYPE_MOUNT_POINT;
    }
    return KU_FILE_TYPE_UNKNOWN;
}

uint32_t public_file_flags(fs::vfs::NodeFlags flags) {
    uint32_t result = KU_FILE_FLAG_NONE;
    if (fs::vfs::has_flag(flags, fs::vfs::NodeFlags::Seekable)) {
        result |= KU_FILE_FLAG_SEEKABLE;
    }
    return result;
}

void export_file_stat(const fs::vfs::FileStat& source, ku_file_stat* output) {
    *output = {};
    output->structure_size = sizeof(*output);
    output->type = public_file_type(source.type);
    output->size = source.size;
    output->flags = public_file_flags(source.flags);
}

void handle_path_operation(
    Context& context,
    arch::x86_64::interrupts::InterruptFrame& frame,
    fs::vfs::Status (*operation)(const char*)) {
    char path[fs::vfs::MAX_PATH_LENGTH + 1U]{};
    if (operation == nullptr ||
        !copy_user_path(context, frame.rdi, frame.rsi, path)) {
        frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
        return;
    }
    frame.rax = static_cast<uint64_t>(vfs_status(operation(path)));
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
                    memory::virtual_memory::MapFlags::NoExecute) != elf::Status::Ok) {
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
    const uint64_t interrupt_flags = save_and_disable_interrupts();
    const threading::Status retirement_status =
        threading::retire_current_user_frame();
    context.result.exit_code = exit_code;
    context.active = false;
    frame.rip = reinterpret_cast<uint64_t>(&x86_64_interrupt_return_to_kernel);
    frame.cs = arch::x86_64::gdt::KERNEL_CODE_SELECTOR;
    frame.rsp = context.return_state.kernel_rsp;
    frame.ss = arch::x86_64::gdt::KERNEL_DATA_SELECTOR;
    frame.rflags = UINT64_C(0x2);
    frame.rdi = context.return_state.kernel_rflags;
    restore_interrupts(interrupt_flags);
    if (retirement_status != threading::Status::Ok) {
        log::write(
            log::Level::Error,
            "RUNTIME",
            threading::status_message(retirement_status));
    }
}

void syscall_handler(
    arch::x86_64::interrupts::InterruptFrame& frame) {
    Context* context = current_context();
    if (context == nullptr || (frame.cs & 3U) != 3U) {
        if ((frame.cs & 3U) == 3U) log_missing_context();
        frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);
        return;
    }
    if ((frame.cs & UINT64_C(0xFFFF)) == arch::x86_64::gdt::USER_CODE_SELECTOR &&
        (frame.ss & UINT64_C(0xFFFF)) == arch::x86_64::gdt::USER_DATA_SELECTOR) {
        context->result.entered_ring3 = true;
    }

    switch (frame.rax) {
        case KU_SYS_EXIT:
            finish_from_interrupt(*context, frame, static_cast<int32_t>(frame.rdi));
            return;
        case KU_SYS_WRITE: {
            const uint64_t descriptor = frame.rdi;
            const uint64_t user_buffer = frame.rsi;
            const uint64_t requested = frame.rdx;
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
            if (descriptor == 1U || descriptor == 2U) {
                const auto* bytes = reinterpret_cast<const char*>(
                    static_cast<uintptr_t>(user_buffer));
                const uint64_t interrupt_flags = save_and_disable_interrupts();
                for (size_t index = 0U; index < count; ++index) terminal::put(bytes[index]);
                restore_interrupts(interrupt_flags);
                context->result.bytes_written += count;
                frame.rax = count;
                return;
            }
            HandleSlot* handle = decode_handle(*context, descriptor);
            if (handle == nullptr) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            size_t bytes_written = 0U;
            const fs::vfs::Status status = fs::root_volume::write(
                handle->file,
                reinterpret_cast<const void*>(static_cast<uintptr_t>(user_buffer)),
                count,
                &bytes_written);
            frame.rax = status == fs::vfs::Status::Ok
                ? bytes_written : static_cast<uint64_t>(vfs_status(status));
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
            if (!validate_user_buffer(*context, user_buffer, count, true)) {
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
                       user::console::try_read(&output[bytes_read])) ++bytes_read;
                frame.rax = bytes_read == 0U
                    ? static_cast<uint64_t>(KU_STATUS_WOULD_BLOCK) : bytes_read;
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
                ? bytes_read : static_cast<uint64_t>(vfs_status(status));
            return;
        }
        case KU_SYS_OPEN: {
            fs::vfs::OpenFlags flags = fs::vfs::OpenFlags::None;
            if (!decode_open_flags(frame.rdx, &flags)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
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
            const fs::vfs::Status status = fs::root_volume::open(path, flags, &file);
            if (status != fs::vfs::Status::Ok) {
                frame.rax = static_cast<uint64_t>(vfs_status(status));
                return;
            }
            ++slot.generation;
            if (slot.generation == 0U) ++slot.generation;
            slot.file = file;
            slot.active = true;
            sync_process_handle_count(*context);
            frame.rax = encode_handle(slot_index, slot.generation);
            return;
        }
        case KU_SYS_CLOSE: {
            HandleSlot* slot = decode_handle(*context, frame.rdi);
            if (slot == nullptr) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const fs::vfs::Status status = fs::root_volume::close(slot->file);
            if (status == fs::vfs::Status::Ok) {
                slot->active = false;
                slot->file = {};
                sync_process_handle_count(*context);
            }
            frame.rax = static_cast<uint64_t>(vfs_status(status));
            return;
        }
        case KU_SYS_FS_STAT: {
            if (!validate_user_buffer(
                    *context, frame.rdx, sizeof(ku_file_stat), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_file_stat*>(
                static_cast<uintptr_t>(frame.rdx));
            if (output->structure_size != sizeof(*output)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            char path[fs::vfs::MAX_PATH_LENGTH + 1U]{};
            if (!copy_user_path(*context, frame.rdi, frame.rsi, path)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            fs::vfs::FileStat info{};
            const fs::vfs::Status status = fs::root_volume::stat(path, &info);
            if (status == fs::vfs::Status::Ok) export_file_stat(info, output);
            frame.rax = static_cast<uint64_t>(vfs_status(status));
            return;
        }
        case KU_SYS_FS_READDIR: {
            if (frame.rdx != sizeof(ku_directory_entry) ||
                !validate_user_buffer(
                    *context, frame.rsi, sizeof(ku_directory_entry), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_directory_entry*>(
                static_cast<uintptr_t>(frame.rsi));
            if (output->structure_size != sizeof(*output)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            HandleSlot* handle = decode_handle(*context, frame.rdi);
            if (handle == nullptr) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            fs::vfs::DirectoryEntry entry{};
            const fs::vfs::Status status = fs::root_volume::readdir(
                handle->file, &entry);
            if (status == fs::vfs::Status::Ok) {
                *output = {};
                output->structure_size = sizeof(*output);
                output->type = public_file_type(entry.info.type);
                output->size = entry.info.size;
                output->flags = public_file_flags(entry.info.flags);
                output->name_length = static_cast<uint32_t>(entry.name_length);
                for (size_t index = 0U;
                     index <= entry.name_length && index < KU_FILE_NAME_CAPACITY;
                     ++index) {
                    output->name[index] = entry.name[index];
                }
            }
            frame.rax = static_cast<uint64_t>(vfs_status(status));
            return;
        }
        case KU_SYS_FS_CREATE:
            handle_path_operation(*context, frame, fs::root_volume::create);
            return;
        case KU_SYS_FS_UNLINK:
            handle_path_operation(*context, frame, fs::root_volume::unlink);
            return;
        case KU_SYS_FS_MKDIR:
            handle_path_operation(*context, frame, fs::root_volume::mkdir);
            return;
        case KU_SYS_FS_RMDIR:
            handle_path_operation(*context, frame, fs::root_volume::rmdir);
            return;
        case KU_SYS_FS_RENAME: {
            if (frame.rsi != sizeof(ku_file_rename_request) ||
                !validate_user_buffer(
                    *context, frame.rdi, sizeof(ku_file_rename_request))) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const auto* request = reinterpret_cast<const ku_file_rename_request*>(
                static_cast<uintptr_t>(frame.rdi));
            if (request->structure_size != sizeof(*request) ||
                request->reserved != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            char source[fs::vfs::MAX_PATH_LENGTH + 1U]{};
            char destination[fs::vfs::MAX_PATH_LENGTH + 1U]{};
            if (!copy_user_path(
                    *context,
                    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
                        request->source.data)),
                    request->source.size,
                    source) ||
                !copy_user_path(
                    *context,
                    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
                        request->destination.data)),
                    request->destination.size,
                    destination)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            frame.rax = static_cast<uint64_t>(vfs_status(
                fs::root_volume::rename(source, destination)));
            return;
        }
        case KU_SYS_FS_SYNC:
            frame.rax = static_cast<uint64_t>(vfs_status(fs::root_volume::sync()));
            return;
        case KU_SYS_FS_SEEK: {
            if (frame.rsi != sizeof(ku_file_seek_request) || frame.rdx != 0U ||
                !validate_user_buffer(
                    *context, frame.rdi, sizeof(ku_file_seek_request), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* request = reinterpret_cast<ku_file_seek_request*>(
                static_cast<uintptr_t>(frame.rdi));
            if (request->structure_size != sizeof(*request)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            if (request->origin > KU_FILE_SEEK_END || request->reserved != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            HandleSlot* handle = decode_handle(*context, request->file);
            if (handle == nullptr) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            fs::vfs::SeekOrigin origin = fs::vfs::SeekOrigin::Begin;
            switch (request->origin) {
                case KU_FILE_SEEK_BEGIN:
                    origin = fs::vfs::SeekOrigin::Begin;
                    break;
                case KU_FILE_SEEK_CURRENT:
                    origin = fs::vfs::SeekOrigin::Current;
                    break;
                case KU_FILE_SEEK_END:
                    origin = fs::vfs::SeekOrigin::End;
                    break;
                default:
                    frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                    return;
            }
            uint64_t new_offset = 0U;
            const fs::vfs::Status status = fs::root_volume::seek(
                handle->file, request->offset, origin, &new_offset);
            if (status == fs::vfs::Status::Ok) request->new_offset = new_offset;
            frame.rax = static_cast<uint64_t>(vfs_status(status));
            return;
        }
        case KU_SYS_FS_CHDIR: {
            if (frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            char path[fs::vfs::MAX_PATH_LENGTH + 1U]{};
            if (!copy_user_path(*context, frame.rdi, frame.rsi, path)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            frame.rax = static_cast<uint64_t>(
                vfs_status(fs::root_volume::chdir(path)));
            return;
        }
        case KU_SYS_FS_GETCWD: {
            if (frame.rsi > SIZE_MAX) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_RANGE);
                return;
            }
            const size_t capacity = static_cast<size_t>(frame.rsi);
            if ((capacity != 0U &&
                 !validate_user_buffer(*context, frame.rdi, capacity, true)) ||
                (frame.rdx != 0U &&
                 !validate_user_buffer(*context, frame.rdx, sizeof(size_t), true))) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = capacity == 0U
                ? nullptr
                : reinterpret_cast<char*>(static_cast<uintptr_t>(frame.rdi));
            size_t required = 0U;
            const fs::vfs::Status status = fs::root_volume::getcwd(
                output, capacity, &required);
            if (frame.rdx != 0U) {
                *reinterpret_cast<size_t*>(static_cast<uintptr_t>(frame.rdx)) = required;
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
                ? KU_STATUS_OK : KU_STATUS_BAD_STATE;
            return;
        case KU_SYS_SLEEP:
            frame.rax = threading::sleep_current(frame.rdi) == threading::Status::Ok
                ? KU_STATUS_OK : KU_STATUS_INVALID_ARGUMENT;
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
                        ? KU_STATUS_OUT_OF_MEMORY : KU_STATUS_BAD_STATE);
            return;
        }
        case KU_SYS_SOCKET_WAIT: {
            if (frame.rdi == KU_SOCKET_INVALID || frame.rsi == 0U ||
                (frame.rsi & ~static_cast<uint64_t>(KU_SOCKET_READY_ALL)) != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            uint32_t ready = net::socket::ReadyNone;
            const net::socket::Status readiness_status = net::socket::readiness(
                context->pid, static_cast<net::socket::Handle>(frame.rdi),
                static_cast<uint32_t>(frame.rsi), &ready);
            if (readiness_status != net::socket::Status::Ok) {
                frame.rax = static_cast<uint64_t>(socket_wait_status(readiness_status));
                return;
            }
            if ((ready & static_cast<uint32_t>(frame.rsi)) != 0U) {
                frame.rax = ready;
                return;
            }
            if (frame.rdx == 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_TIMED_OUT);
                return;
            }
            SocketWaiter* waiter = reserve_socket_waiter(context->tid);
            if (waiter == nullptr) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_MEMORY);
                return;
            }
            const uint64_t now = threading::timer_ticks();
            const bool finite = frame.rdx != UINT64_MAX;
            const uint64_t deadline = !finite || frame.rdx > UINT64_MAX - now
                ? UINT64_MAX : now + frame.rdx;
            *waiter = {
                context->pid, context->tid,
                static_cast<net::socket::Handle>(frame.rdi),
                static_cast<uint32_t>(frame.rsi), deadline, finite, true};
            if (threading::block_current() != threading::Status::Ok) {
                *waiter = {};
                frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);
                return;
            }
            // The saved Ring-3 frame cannot be selected while Blocked.
            // wake_user() overwrites RAX with readiness or a terminal status.
            frame.rax = static_cast<uint64_t>(KU_STATUS_WOULD_BLOCK);
            return;
        }
        case KU_SYS_WAIT: {
            if (!validate_user_buffer(*context, frame.rsi, sizeof(int32_t), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            int32_t exit_code = 0;
            const process::Status status = process::wait(
                static_cast<process::ProcessId>(frame.rdi), &exit_code);
            if (status == process::Status::Ok) {
                *reinterpret_cast<int32_t*>(static_cast<uintptr_t>(frame.rsi)) = exit_code;
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
                !validate_user_buffer(*context, frame.rdx, sizeof(ku_ui_window_options))) {
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
            for (size_t index = 0U; index < static_cast<size_t>(frame.rsi); ++index) {
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
                        ? KU_STATUS_OUT_OF_MEMORY : KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            context->ui.active = true;
            frame.rax = context->ui.window;
            return;
        }
        case KU_SYS_UI_PRESENT: {
            if (!context->ui.active || frame.rdi != context->ui.window ||
                (frame.rdx != sizeof(ku_ui_frame) &&
                 frame.rdx != sizeof(ku_ui_native_frame)) ||
                frame.rdx > SIZE_MAX ||
                !validate_user_buffer(
                    *context, frame.rsi, static_cast<size_t>(frame.rdx))) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }

            if (frame.rdx == sizeof(ku_ui_native_frame)) {
                const auto* user_native = reinterpret_cast<const ku_ui_native_frame*>(
                    static_cast<uintptr_t>(frame.rsi));
                if (!native_frame_valid(*user_native)) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_CORRUPT_DATA);
                    return;
                }
                const windowing::Status surface_status = windowing::present_surface(
                    context->ui.window, sizeof(ku_ui_native_frame), 1U,
                    sizeof(ku_ui_native_frame), user_native,
                    sizeof(ku_ui_native_frame));
                if (surface_status != windowing::Status::Ok) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                    return;
                }
                system_metrics::record_graphics_work(
                    UINT64_C(1) + static_cast<uint64_t>(user_native->command_count));
                frame.rax = KU_STATUS_OK;
                return;
            }

            const auto* user_frame = reinterpret_cast<const ku_ui_frame*>(
                static_cast<uintptr_t>(frame.rsi));
            if (user_frame->structure_size != sizeof(ku_ui_frame) ||
                user_frame->line_count > KU_UI_MAX_LINES || user_frame->reserved != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_CORRUPT_DATA);
                return;
            }
            for (uint32_t line = 0U; line < user_frame->line_count; ++line) {
                bool terminated = false;
                for (size_t character = 0U; character < KU_UI_LINE_CAPACITY; ++character) {
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
            const windowing::Status surface_status = windowing::present_surface(
                context->ui.window, sizeof(ku_ui_frame), 1U, sizeof(ku_ui_frame),
                user_frame, sizeof(ku_ui_frame));
            if (surface_status != windowing::Status::Ok) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            context->ui.frame = *user_frame;
            system_metrics::record_graphics_work(
                UINT64_C(1) + static_cast<uint64_t>(user_frame->line_count));
            frame.rax = KU_STATUS_OK;
            return;
        }
        case KU_SYS_UI_POLL: {
            if (!context->ui.active || frame.rdi != context->ui.window ||
                frame.rdx != sizeof(ku_ui_event) ||
                !validate_user_buffer(*context, frame.rsi, sizeof(ku_ui_event), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* user_event = reinterpret_cast<ku_ui_event*>(
                static_cast<uintptr_t>(frame.rsi));
            windowing::WindowInfo info{};
            if (windowing::query(context->ui.window, &info) != windowing::Status::Ok) {
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
            const windowing::Status status = windowing::close(context->ui.window);
            context->ui.active = false;
            context->ui.window = windowing::INVALID_WINDOW;
            frame.rax = status == windowing::Status::Ok
                ? static_cast<uint64_t>(KU_STATUS_OK)
                : static_cast<uint64_t>(KU_STATUS_NOT_FOUND);
            return;
        }
        case KU_SYS_SYSTEM_SNAPSHOT: {
            if (frame.rsi != sizeof(ku_system_snapshot) ||
                !validate_user_buffer(
                    *context, frame.rdi, sizeof(ku_system_snapshot), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_system_snapshot*>(
                static_cast<uintptr_t>(frame.rdi));
            if (output->structure_size != sizeof(ku_system_snapshot)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            const system_metrics::ActivitySnapshot activity = system_metrics::sample();
            const uint64_t frame_size = static_cast<uint64_t>(memory::physical_frame_size());
            const uint64_t total_frames = static_cast<uint64_t>(memory::total_frames());
            const uint64_t free_frames = static_cast<uint64_t>(memory::free_frames());
            const uint64_t total_bytes = frame_size != 0U &&
                total_frames <= UINT64_MAX / frame_size
                    ? total_frames * frame_size : 0U;
            const uint64_t free_bytes = frame_size != 0U &&
                free_frames <= UINT64_MAX / frame_size
                    ? free_frames * frame_size : 0U;
            const uint64_t used_bytes = total_bytes >= free_bytes
                ? total_bytes - free_bytes : 0U;
            const uint32_t ram_percent = total_bytes == 0U
                ? 0U : static_cast<uint32_t>(
                    used_bytes >= total_bytes ? 100U :
                    (used_bytes * UINT64_C(100)) / total_bytes);
            *output = {};
            output->structure_size = sizeof(*output);
            output->version = KU_SYSTEM_SNAPSHOT_VERSION;
            output->cpu_percent = activity.cpu_percent;
            output->gpu_percent = activity.gpu_percent;
            output->ram_percent = ram_percent;
            output->disk_percent = activity.disk_percent;
            output->memory_total_bytes = total_bytes;
            output->memory_free_bytes = free_bytes;
            output->uptime_ticks = threading::timer_ticks();
            frame.rax = KU_STATUS_OK;
            return;
        }
        case KU_SYS_DESKTOP_PIN: {
            if (frame.rsi != sizeof(ku_desktop_pin_request) ||
                !validate_user_buffer(
                    *context, frame.rdi, sizeof(ku_desktop_pin_request), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* request = reinterpret_cast<ku_desktop_pin_request*>(
                static_cast<uintptr_t>(frame.rdi));
            if (request->structure_size != sizeof(*request) || request->reserved != 0U ||
                request->app_id >= KU_DESKTOP_APP_COUNT ||
                request->action > KU_DESKTOP_PIN_TOGGLE) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            bool pinned = false;
            const windowing::Status status = windowing::desktop_pin(
                request->app_id,
                request->action,
                request->value != 0U,
                &pinned);
            request->pinned = pinned ? 1U : 0U;
            frame.rax = status == windowing::Status::Ok
                ? static_cast<uint64_t>(KU_STATUS_OK)
                : static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
            return;
        }
        case KU_SYS_NET_STATUS: {
            if (frame.rsi != sizeof(ku_network_status) ||
                !validate_user_buffer(
                    *context, frame.rdi, sizeof(ku_network_status), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_network_status*>(
                static_cast<uintptr_t>(frame.rdi));
            if (output->structure_size != sizeof(*output)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            net::NetworkStats stats{};
            const net::Status stats_status = net::service::stats(&stats);
            const net::IPv4Config* config = net::service::configuration();
            const net::IPv4Address* dns = net::service::dns_server();
            *output = {};
            output->structure_size = sizeof(*output);
            output->ready = net::service::ready() ? 1U : 0U;
            output->physical = net::service::physical_interface() ? 1U : 0U;
            output->dhcp = net::service::dhcp_configured() ? 1U : 0U;
            if (config != nullptr) {
                for (size_t index = 0U; index < 4U; ++index) {
                    output->address[index] = config->address.bytes[index];
                    output->gateway[index] = config->gateway.bytes[index];
                }
            }
            if (dns != nullptr) {
                for (size_t index = 0U; index < 4U; ++index) {
                    output->dns[index] = dns->bytes[index];
                }
            }
            if (stats_status == net::Status::Ok) {
                output->bytes_received = stats.bytes_received;
                output->bytes_transmitted = stats.bytes_transmitted;
            }
            frame.rax = KU_STATUS_OK;
            return;
        }
        case KU_SYS_HTTP_GET: {
            if (frame.rsi != sizeof(ku_http_request) ||
                !validate_user_buffer(
                    *context, frame.rdi, sizeof(ku_http_request), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* request = reinterpret_cast<ku_http_request*>(
                static_cast<uintptr_t>(frame.rdi));
            if (request->structure_size != sizeof(*request) || request->flags != 0U ||
                request->reserved != 0U ||
                !fixed_string_terminated(request->host, sizeof(request->host)) ||
                !fixed_string_terminated(request->path, sizeof(request->path)) ||
                request->path[0] != '/' || request->output == nullptr ||
                request->output_capacity == 0U ||
                request->output_capacity > KU_HTTP_RESPONSE_CAPACITY_LIMIT ||
                request->output_capacity > SIZE_MAX ||
                !validate_user_buffer(
                    *context,
                    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(request->output)),
                    static_cast<size_t>(request->output_capacity),
                    true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            char host[KU_NET_HOST_CAPACITY]{};
            char path[KU_NET_PATH_CAPACITY]{};
            for (size_t index = 0U; index < sizeof(host); ++index) {
                host[index] = request->host[index];
                if (host[index] == '\0') break;
            }
            for (size_t index = 0U; index < sizeof(path); ++index) {
                path[index] = request->path[index];
                if (path[index] == '\0') break;
            }
            auto* output = reinterpret_cast<uint8_t*>(request->output);
            size_t received = 0U;
            uint16_t http_status = 0U;
            const net::Status status = net::service::http_get(
                host,
                path,
                output,
                static_cast<size_t>(request->output_capacity),
                &received,
                &http_status);
            request->bytes_received = received;
            request->http_status = http_status;
            frame.rax = static_cast<uint64_t>(network_status(status));
            return;
        }
        case KU_SYS_DNS_RESOLVE_A: {
    if (frame.rsi != sizeof(ku_dns_a_request) || frame.rdx != 0U ||
        !validate_user_buffer(
            *context, frame.rdi, sizeof(ku_dns_a_request), true)) {
        frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
        return;
    }
    auto* request = reinterpret_cast<ku_dns_a_request*>(
        static_cast<uintptr_t>(frame.rdi));
    if (request->structure_size != sizeof(*request)) {
        frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
        return;
    }
    if (request->flags != KU_DNS_FLAG_NONE || request->reserved != 0U ||
        !fixed_string_terminated(request->host, sizeof(request->host))) {
        frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
        return;
    }
    net::IPv4Address address{};
    const net::Status status = net::service::resolve_a(request->host, &address);
    if (status == net::Status::Ok) {
        for (size_t index = 0U; index < 4U; ++index) {
            request->address[index] = address.bytes[index];
        }
    }
    frame.rax = static_cast<uint64_t>(network_status(status));
    return;
}
        case KU_SYS_AUDIO_STATUS: {
            if (frame.rsi != sizeof(ku_audio_state) ||
                !validate_user_buffer(
                    *context, frame.rdi, sizeof(ku_audio_state), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_audio_state*>(
                static_cast<uintptr_t>(frame.rdi));
            if (output->structure_size != sizeof(*output)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            const bool available = drivers::audio::ac97::initialized();
            const drivers::audio::ac97::Capabilities caps =
                drivers::audio::ac97::capabilities();
            *output = {};
            output->structure_size = sizeof(*output);
            output->version = KU_AUDIO_STATE_VERSION;
            output->available = available ? 1U : 0U;
            output->muted = available && drivers::audio::ac97::muted() ? 1U : 0U;
            output->volume_percent = available
                ? drivers::audio::ac97::master_volume_percent() : 0U;
            output->sample_rate = available ? caps.sample_rate : 0U;
            output->channels = available ? caps.channels : 0U;
            output->bits_per_sample = available ? caps.bits_per_sample : 0U;
            frame.rax = KU_STATUS_OK;
            return;
        }
        case KU_SYS_AUDIO_SET: {
            if (frame.rsi != sizeof(ku_audio_set_request) ||
                !validate_user_buffer(
                    *context, frame.rdi, sizeof(ku_audio_set_request))) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            const auto* request = reinterpret_cast<const ku_audio_set_request*>(
                static_cast<uintptr_t>(frame.rdi));
            if (request->structure_size != sizeof(*request) ||
                request->reserved != 0U || request->volume_percent > 100U ||
                request->muted > 1U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            if (!drivers::audio::ac97::initialized()) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);
                return;
            }
            const drivers::audio::ac97::Status status =
                drivers::audio::ac97::set_master_volume(
                    request->volume_percent, request->muted != 0U);
            frame.rax = status == drivers::audio::ac97::Status::Ok
                ? static_cast<uint64_t>(KU_STATUS_OK)
                : static_cast<uint64_t>(
                    status == drivers::audio::ac97::Status::InvalidArgument
                        ? KU_STATUS_INVALID_ARGUMENT : KU_STATUS_IO_ERROR);
            return;
        }
        case KU_SYS_AUDIO_PLAY_PCM16: {
            if (frame.rdx != 0U || frame.rsi == 0U || frame.rsi > SIZE_MAX) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            if (!drivers::audio::ac97::initialized()) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);
                return;
            }
            const drivers::audio::ac97::Capabilities caps =
                drivers::audio::ac97::capabilities();
            if (frame.rsi > caps.maximum_frames_per_buffer ||
                frame.rsi > SIZE_MAX / (sizeof(int16_t) * 2U)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_RANGE);
                return;
            }
            const size_t frame_count = static_cast<size_t>(frame.rsi);
            const size_t bytes = frame_count * sizeof(int16_t) * 2U;
            if (!validate_user_buffer(*context, frame.rdi, bytes)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            if (g_audio_owner_pid != 0U &&
                g_audio_owner_pid != context->pid) {
                const drivers::audio::ac97::Status poll_status =
                    drivers::audio::ac97::poll();
                if (poll_status != drivers::audio::ac97::Status::Ok) {
                    g_audio_owner_pid = 0U;
                    frame.rax = static_cast<uint64_t>(audio_status(poll_status));
                    return;
                }
                if (drivers::audio::ac97::busy()) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_WOULD_BLOCK);
                    return;
                }
                g_audio_owner_pid = 0U;
            }
            const auto* samples = reinterpret_cast<const int16_t*>(
                static_cast<uintptr_t>(frame.rdi));
            const drivers::audio::ac97::Status status =
                drivers::audio::ac97::play_pcm16_stereo(samples, frame_count);
            if (status == drivers::audio::ac97::Status::Ok) {
                g_audio_owner_pid = context->pid;
            }
            frame.rax = static_cast<uint64_t>(audio_status(status));
            return;
        }
        case KU_SYS_AUDIO_POLL: {
            if (frame.rdi != 0U || frame.rsi != 0U || frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            if (!drivers::audio::ac97::initialized()) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);
                return;
            }
            if (g_audio_owner_pid != 0U &&
                g_audio_owner_pid != context->pid) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_ACCESS_DENIED);
                return;
            }
            const drivers::audio::ac97::Status status =
                drivers::audio::ac97::poll();
            if (status != drivers::audio::ac97::Status::Ok) {
                g_audio_owner_pid = 0U;
                frame.rax = static_cast<uint64_t>(audio_status(status));
                return;
            }
            if (drivers::audio::ac97::busy()) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_WOULD_BLOCK);
                return;
            }
            g_audio_owner_pid = 0U;
            frame.rax = static_cast<uint64_t>(KU_STATUS_OK);
            return;
        }
        case KU_SYS_AUDIO_STOP: {
            if (frame.rdi != 0U || frame.rsi != 0U || frame.rdx != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            if (!drivers::audio::ac97::initialized()) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);
                return;
            }
            if (g_audio_owner_pid == 0U) {
                frame.rax = static_cast<uint64_t>(
                    drivers::audio::ac97::busy()
                        ? KU_STATUS_ACCESS_DENIED : KU_STATUS_OK);
                return;
            }
            if (g_audio_owner_pid != context->pid) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_ACCESS_DENIED);
                return;
            }
            const drivers::audio::ac97::Status status =
                drivers::audio::ac97::stop();
            if (status == drivers::audio::ac97::Status::Ok ||
                status == drivers::audio::ac97::Status::DeviceFault) {
                g_audio_owner_pid = 0U;
            }
            frame.rax = static_cast<uint64_t>(audio_status(status));
            return;
        }
        default:
            frame.rax = static_cast<uint64_t>(KU_STATUS_NOT_SUPPORTED);
            return;
    }
}

Status cleanup(Context& context) {
    Status result = Status::Ok;
    clear_socket_waiter(context.tid);
    if (g_audio_owner_pid == context.pid) {
        if (drivers::audio::ac97::initialized() &&
            drivers::audio::ac97::busy() &&
            drivers::audio::ac97::stop() != drivers::audio::ac97::Status::Ok) {
            result = Status::CleanupFailed;
        }
        g_audio_owner_pid = 0U;
    }
    if (context.ui.active) {
        const windowing::Status ui_status = windowing::close(context.ui.window);
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
        handle.file = {};
    }
    sync_process_handle_count(context);
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
            &context.address_space) != memory::kernel_virtual_memory::Status::Ok) {
        result = Status::CleanupFailed;
    }
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
    if (g_initialized) return Status::AlreadyInitialized;
    if (!enable_memory_protection()) return Status::CpuUnsupported;
    for (Context*& context : g_contexts) context = nullptr;
    for (size_t index = 0U; index < kMaximumContexts; ++index) {
        g_context_storage[index] = {};
        g_context_storage_used[index] = false;
        g_socket_waiters[index] = {};
    }
    if (threading::set_pre_dispatch_hook(socket_wait_pre_dispatch) !=
        threading::Status::Ok) {
        return Status::SchedulerRegistrationFailed;
    }
    if (!arch::x86_64::interrupts::register_handler(kSyscallVector, syscall_handler) ||
        !arch::x86_64::interrupts::set_gate_privilege(
            kSyscallVector, arch::x86_64::interrupts::GatePrivilege::User) ||
        !arch::x86_64::interrupts::set_gate_type(
            kSyscallVector, arch::x86_64::interrupts::GateType::Trap)) {
        arch::x86_64::interrupts::unregister_handler(kSyscallVector);
        return Status::InterruptRegistrationFailed;
    }
    g_initialized = true;
    return Status::Ok;
}

bool initialized() { return g_initialized; }

void set_process_identity(uint64_t pid) {
    if (current_context() == nullptr) g_process_identity = pid;
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
    if (!g_initialized) return Status::NotInitialized;
    if (path == nullptr || result == nullptr) return Status::FileReadFailed;
    if (!fs::root_volume::mounted()) return Status::RootUnavailable;

    fs::vfs::FileStat file_info{};
    fs::vfs::Status file_status = fs::root_volume::stat(path, &file_info);
    if (file_status == fs::vfs::Status::NotFound) return Status::FileNotFound;
    if (file_status != fs::vfs::Status::Ok ||
        file_info.type != fs::vfs::NodeType::Regular) return Status::FileReadFailed;
    if (file_info.size == 0U || file_info.size > kMaximumExecutableSize ||
        file_info.size > SIZE_MAX) return Status::FileTooLarge;

    const size_t file_size = static_cast<size_t>(file_info.size);
    void* file_bytes = memory::kmalloc(file_size, 16U);
    if (file_bytes == nullptr) return Status::OutOfMemory;
    size_t bytes_read = 0U;
    uint64_t measured_size = 0U;
    file_status = fs::root_volume::read_file(
        path, file_bytes, file_size, &bytes_read, &measured_size);
    if (file_status != fs::vfs::Status::Ok || bytes_read != file_size ||
        measured_size != file_info.size) {
        memory::kfree(file_bytes);
        return Status::FileReadFailed;
    }

    Context* context_storage = acquire_context_storage();
    if (context_storage == nullptr) {
        memory::kfree(file_bytes);
        return Status::Busy;
    }
    Context& context = *context_storage;
    context.pid = pid;
    context.tid = threading::current();
    context.next_heap = kHeapBase;
    context.result.fault_vector = 0xFFU;
    if (memory::kernel_virtual_memory::create_address_space(
            &context.address_space) != memory::kernel_virtual_memory::Status::Ok) {
        memory::kfree(file_bytes);
        release_context_storage(context_storage);
        return Status::AddressSpaceFailed;
    }
    const elf::Status load_status = elf::load(
        file_bytes, file_size, &context.address_space.address_space, &context.image);
    memory::kfree(file_bytes);
    if (load_status != elf::Status::Ok) {
        static_cast<void>(cleanup(context));
        log::write(log::Level::Error, "ELF", elf::status_message(load_status));
        release_context_storage(context_storage);
        return Status::ElfLoadFailed;
    }

    for (size_t index = 0U; index < kStackPages; ++index) {
        const uint64_t page =
            kStackTop - (index + 1U) * memory::virtual_memory::PAGE_SIZE;
        if (elf::map_anonymous_page(
                &context.image,
                page,
                memory::virtual_memory::MapFlags::Writable |
                    memory::virtual_memory::MapFlags::NoExecute) != elf::Status::Ok) {
            static_cast<void>(cleanup(context));
            release_context_storage(context_storage);
            return Status::StackMappingFailed;
        }
    }

    const uint8_t fault_exit_code[] = {
        0xB8, 0x01, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        0x0F, 0x0B
    };
    if (elf::map_anonymous_page(
            &context.image,
            kFaultTrampoline,
            memory::virtual_memory::MapFlags::None,
            fault_exit_code,
            sizeof(fault_exit_code)) != elf::Status::Ok) {
        static_cast<void>(cleanup(context));
        release_context_storage(context_storage);
        return Status::TrampolineMappingFailed;
    }

    context.return_state.kernel_rflags = save_and_disable_interrupts();
    context.active = true;
    if (!register_context(&context)) {
        context.active = false;
        const uint64_t saved_flags = context.return_state.kernel_rflags;
        static_cast<void>(cleanup(context));
        __asm__ volatile("pushq %0; popfq" : : "r"(saved_flags) : "memory", "cc");
        release_context_storage(context_storage);
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
        __asm__ volatile("pushq %0; popfq" : : "r"(saved_flags) : "memory", "cc");
        release_context_storage(context_storage);
        return Status::AddressSpaceActivationFailed;
    }
    if (memory::kernel_virtual_memory::activate(&context.address_space) !=
        memory::kernel_virtual_memory::Status::Ok) {
        context.active = false;
        const uint64_t saved_flags = context.return_state.kernel_rflags;
        static_cast<void>(cleanup(context));
        __asm__ volatile("pushq %0; popfq" : : "r"(saved_flags) : "memory", "cc");
        release_context_storage(context_storage);
        return Status::AddressSpaceActivationFailed;
    }
    x86_64_enter_user(context.image.entry, kStackTop - 16U, &context.return_state);

    const Status cleanup_status = cleanup(context);
    *result = context.result;
    release_context_storage(context_storage);
    return cleanup_status;
}

bool handle_exception(
    arch::x86_64::interrupts::InterruptFrame& frame) {
    Context* context = current_context();
    if (context == nullptr || (frame.cs & 3U) != 3U || frame.vector >= 32U) {
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
            context->tid == threading::INVALID_THREAD_ID) continue;
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
        case Status::SchedulerRegistrationFailed:
            return "cannot install ring-3 readiness scheduler hook";
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
        case Status::TrampolineMappingFailed: return "fault trampoline mapping failed";
        case Status::AddressSpaceActivationFailed:
            return "address-space activation failed";
        case Status::CleanupFailed: return "user image cleanup failed";
        case Status::ResourceLeak: return "user image leaked PMM frames";
    }
    return "unknown user runtime status";
}

} // namespace user::runtime
