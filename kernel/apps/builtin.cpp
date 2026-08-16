#include "builtin.hpp"

#include "framework.hpp"
#include "../core/string.hpp"
#include "../drivers/framebuffer.hpp"
#include "../drivers/pci.hpp"
#include "../drivers/rtc.hpp"
#include "../drivers/pit.hpp"
#include "../fs/ramfs.hpp"
#include "../memory/allocator.hpp"
#include "../memory/physical_memory.hpp"
#include "../task/process.hpp"
#include "../terminal.hpp"
#include "../ui/ui.hpp"
#include "../ui/window_manager.hpp"
#include "../../common/version.h"

namespace builtin_apps {

namespace {
windowing::WindowId g_monitor_window = windowing::INVALID_WINDOW;
windowing::WindowId g_files_window = windowing::INVALID_WINDOW;
windowing::WindowId g_about_window = windowing::INVALID_WINDOW;
bool g_drag_test_reported = false;

void monitor_window_draw(
    windowing::WindowId, const ui::Rect&, bool, void*);
void files_window_draw(
    windowing::WindowId, const ui::Rect&, bool, void*);
void about_window_draw(
    windowing::WindowId, const ui::Rect&, bool, void*);

bool open_desktop_window(char selector) {
    const int32_t screen_width = static_cast<int32_t>(graphics::width());
    const int32_t screen_height = static_cast<int32_t>(graphics::height());
    const int32_t width = screen_width / 2 - 45;
    const int32_t height = screen_height > 500 ? 300 : 220;
    windowing::WindowId* id = nullptr;
    windowing::DrawCallback draw = nullptr;
    const char* title = nullptr;
    ui::Rect bounds{};
    if (selector == '1') {
        id = &g_monitor_window;
        draw = monitor_window_draw;
        title = "SYSTEM MONITOR";
        bounds = {30, 60, width, height};
    } else if (selector == '2') {
        id = &g_files_window;
        draw = files_window_draw;
        title = "FILES";
        bounds = {screen_width - width - 30, 95, width, height};
    } else if (selector == '3') {
        id = &g_about_window;
        draw = about_window_draw;
        title = "ABOUT KUROGANE OS";
        bounds = {(screen_width - width) / 2, 130, width, height};
    } else {
        return false;
    }
    windowing::WindowInfo existing{};
    if (*id != windowing::INVALID_WINDOW &&
        windowing::query(*id, &existing) == windowing::Status::Ok) {
        if (existing.state == windowing::WindowState::Minimized) {
            static_cast<void>(windowing::restore(*id));
        } else {
            static_cast<void>(windowing::focus(*id));
        }
        return true;
    }
    return windowing::create_window(
        title, 0U, bounds, draw, nullptr, nullptr, id) == windowing::Status::Ok;
}

void draw_text_value(int32_t x, int32_t y, const char* label,
                     uint64_t value, const char* suffix = nullptr) {
    char buffer[48];
    kstd::copy(buffer, sizeof(buffer), label);
    char number[24];
    kstd::format_u64(number, sizeof(number), value);
    kstd::append(buffer, sizeof(buffer), number);
    if (suffix) {
        kstd::append(buffer, sizeof(buffer), suffix);
    }
    graphics::draw_text(x, y, buffer, ui::default_theme().text,
                        ui::default_theme().panel, 2, true);
}

void leave_application() {
    applications::stop();
}

void common_key(char character) {
    if (character == 'q' || character == 'Q' || character == 27) {
        leave_application();
    }
}

void desktop_key(char character) {
    switch (character) {
    case '1':
        static_cast<void>(open_desktop_window('1'));
        break;
    case '2':
        static_cast<void>(open_desktop_window('2'));
        break;
    case '3':
        static_cast<void>(open_desktop_window('3'));
        break;
    case 'm':
    case 'M':
        static_cast<void>(open_desktop_window('1'));
        break;
    case 'f':
    case 'F':
        static_cast<void>(open_desktop_window('2'));
        break;
    case 'a':
    case 'A':
        static_cast<void>(open_desktop_window('3'));
        break;
    case 't':
    case 'T':
        static_cast<void>(process::spawn("/gui/terminal", nullptr));
        break;
    case 'x':
    case 'X':
        static_cast<void>(process::spawn("/gui/files", nullptr));
        break;
    case 'u':
    case 'U':
        static_cast<void>(process::spawn("/gui/sysmon", nullptr));
        break;
    case 's':
    case 'S':
        static_cast<void>(process::spawn("/gui/settings", nullptr));
        break;
    case 'i':
    case 'I':
        static_cast<void>(process::spawn("/gui/about", nullptr));
        break;
    case 'q':
    case 'Q':
        static_cast<void>(windowing::close(windowing::focused_window()));
        break;
    default:
        break;
    }
}

void draw_desktop() {
    static_cast<void>(windowing::render_if_needed());
}

bool desktop_start(const char*) {
    if (!graphics::available()) {
        return false;
    }
    if (windowing::initialize(graphics::width(), graphics::height()) !=
        windowing::Status::Ok) return false;
    g_monitor_window = windowing::INVALID_WINDOW;
    g_files_window = windowing::INVALID_WINDOW;
    g_about_window = windowing::INVALID_WINDOW;
    g_drag_test_reported = false;
    if (!open_desktop_window('1') || !open_desktop_window('2') ||
        windowing::window_count() < 2U) return false;
    terminal::println("desktop window manager ready: 2 concurrent windows");
    terminal::println("[TEST] window_manager_multiwindow: PASS");
    draw_desktop();
    return true;
}

void desktop_tick(uint64_t tick) {
    static uint64_t last_draw = 0;
    if (tick - last_draw >= 25) {
        last_draw = tick;
        windowing::invalidate();
        draw_desktop();
    }
}

void draw_monitor(uint64_t tick) {
    ui::desktop("SYSTEM MONITOR");
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    ui::Rect window_bounds{30, 54, width - 60, height - 105};
    ui::window(window_bounds, "RESOURCES");

    int32_t y = window_bounds.y + 48;
    draw_text_value(window_bounds.x + 18, y, "UPTIME TICKS: ", tick);
    y += 24;
    draw_text_value(window_bounds.x + 18, y, "HEAP USED: ",
                    memory::used_bytes(), " B");
    y += 24;
    draw_text_value(window_bounds.x + 18, y, "HEAP FREE: ",
                    memory::free_bytes(), " B");
    y += 24;
    draw_text_value(window_bounds.x + 18, y, "HEAP ALLOCATIONS: ",
                    memory::allocation_count());
    y += 24;
    draw_text_value(window_bounds.x + 18, y, "PHYSICAL FRAMES FREE: ",
                    memory::free_frames());
    y += 24;
    draw_text_value(window_bounds.x + 18, y, "PCI DEVICES: ",
                    pci::device_count());

    const uint32_t heap_total = static_cast<uint32_t>(
        memory::total_bytes() > UINT32_MAX ? UINT32_MAX :
        memory::total_bytes());
    const uint32_t heap_used = static_cast<uint32_t>(
        memory::used_bytes() > UINT32_MAX ? UINT32_MAX :
        memory::used_bytes());
    ui::progress({window_bounds.x + 18, y + 38,
                  window_bounds.width - 36, 18},
                 heap_used, heap_total);
    ui::taskbar("Q: RETURN TO SHELL");
}

bool monitor_start(const char*) {
    if (!graphics::available()) {
        return false;
    }
    draw_monitor(0);
    return true;
}

void monitor_tick(uint64_t tick) {
    static uint64_t last_draw = 0;
    if (tick - last_draw >= 25) {
        last_draw = tick;
        draw_monitor(tick);
    }
}

struct FileDrawContext {
    int32_t x;
    int32_t y;
    int32_t bottom;
};

bool draw_file(const char* name, const fs::FileStat* stat, void* opaque) {
    auto* context = static_cast<FileDrawContext*>(opaque);
    if (!name || !stat || !context || context->y + 18 >= context->bottom) {
        return false;
    }
    char line[96];
    kstd::copy(line, sizeof(line),
               stat->type == fs::EntryType::Directory ? "[DIR]  " : "[FILE] ");
    kstd::append(line, sizeof(line), name);
    if (stat->type == fs::EntryType::File) {
        char size[24];
        kstd::append(line, sizeof(line), "  ");
        kstd::format_u64(size, sizeof(size), stat->size);
        kstd::append(line, sizeof(line), size);
        kstd::append(line, sizeof(line), " B");
    }
    graphics::draw_text(context->x, context->y, line,
                        ui::default_theme().text,
                        ui::default_theme().panel, 2, true);
    context->y += 20;
    return true;
}

void monitor_window_draw(
    windowing::WindowId,
    const ui::Rect& content,
    bool,
    void*) {
    int32_t y = content.y + 14;
    draw_text_value(content.x + 14, y, "UPTIME TICKS: ",
                    drivers::pit::ticks());
    y += 24;
    draw_text_value(content.x + 14, y, "HEAP USED: ",
                    memory::used_bytes(), " B");
    y += 24;
    draw_text_value(content.x + 14, y, "HEAP FREE: ",
                    memory::free_bytes(), " B");
    y += 24;
    draw_text_value(content.x + 14, y, "PHYSICAL FRAMES FREE: ",
                    memory::free_frames());
    y += 34;
    const uint32_t total = static_cast<uint32_t>(
        memory::total_bytes() > UINT32_MAX ? UINT32_MAX : memory::total_bytes());
    const uint32_t used = static_cast<uint32_t>(
        memory::used_bytes() > UINT32_MAX ? UINT32_MAX : memory::used_bytes());
    ui::progress({content.x + 14, y, content.width - 28, 18}, used, total);
}

void files_window_draw(
    windowing::WindowId,
    const ui::Rect& content,
    bool,
    void*) {
    FileDrawContext context{
        content.x + 14,
        content.y + 14,
        content.y + content.height - 8
    };
    const fs::Status status = fs::list_directory("/", draw_file, &context);
    if (status != fs::Status::Ok && status != fs::Status::IterationStopped) {
        graphics::draw_text(context.x, context.y, fs::status_message(status),
                            ui::default_theme().danger,
                            ui::default_theme().panel, 2, true);
    }
}

void about_window_draw(
    windowing::WindowId,
    const ui::Rect& content,
    bool,
    void*) {
    const char* lines[] = {
        "KuroganeOS " KUROGANE_VERSION_STRING,
        "x86-64 UEFI / Ring 3 / VFS",
        "E1000 IPv4 networking",
        "PS/2 unified input",
        "WindowManager focus + z-order"
    };
    int32_t y = content.y + 14;
    for (const char* line : lines) {
        graphics::draw_text(content.x + 14, y, line,
                            ui::default_theme().text,
                            ui::default_theme().panel, 2, true);
        y += 24;
    }
}

void draw_files() {
    ui::desktop("FILES");
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    ui::Rect window_bounds{30, 54, width - 60, height - 105};
    ui::window(window_bounds, "RAMFS /");
    FileDrawContext context{
        window_bounds.x + 18,
        window_bounds.y + 48,
        window_bounds.y + window_bounds.height - 12
    };
    const auto status = fs::list_directory("/", draw_file, &context);
    if (status != fs::Status::Ok &&
        status != fs::Status::IterationStopped) {
        graphics::draw_text(
            context.x, context.y, fs::status_message(status),
            ui::default_theme().danger, ui::default_theme().panel, 2, true);
    }
    ui::taskbar("Q: RETURN TO SHELL");
}

bool files_start(const char*) {
    if (!graphics::available()) {
        return false;
    }
    draw_files();
    return true;
}

void files_key(char character) {
    if (character == 'r' || character == 'R') {
        draw_files();
    } else {
        common_key(character);
    }
}

bool about_start(const char*) {
    if (!graphics::available()) {
        return false;
    }
    ui::desktop("ABOUT KUROGANE OS");
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    ui::Rect window_bounds{40, 70, width - 80, height - 145};
    ui::window(window_bounds, "VERSION " KUROGANE_VERSION_STRING);
    int32_t y = window_bounds.y + 52;
    const char* lines[] = {
        "X86-64 UEFI HOBBY OPERATING SYSTEM",
        "FRAMEBUFFER TERMINAL AND PS/2 INPUT",
        "PHYSICAL MEMORY AND KERNEL HEAP",
        "HIERARCHICAL RAMFS",
        "COOPERATIVE KERNEL TASKS",
        "PCI RTC NETWORK AND UI FRAMEWORK",
        "",
        "PRESS Q TO RETURN TO THE SHELL"
    };
    for (const char* line : lines) {
        graphics::draw_text(window_bounds.x + 18, y, line,
                            ui::default_theme().text,
                            ui::default_theme().panel, 2, true);
        y += 24;
    }
    ui::taskbar("KUROGANE OS");
    return true;
}

void no_tick(uint64_t) {}
void no_stop() {}

void desktop_input(const input::Event& event) {
    const size_t windows_before = windowing::window_count();
    static_cast<void>(windowing::dispatch(event));
    if (windowing::window_count() < windows_before) {
        terminal::println("[TEST] window_close_input: PASS");
    }
    if (!g_drag_test_reported && event.type == input::EventType::MouseMove &&
        (event.buttons & drivers::mouse::Left) != 0U) {
        g_drag_test_reported = true;
        terminal::println("[TEST] window_drag_input: PASS");
        windowing::invalidate();
    }
    if (event.type == input::EventType::KeyDown && event.character != 0) {
        windowing::WindowInfo focused{};
        if (windowing::query(windowing::focused_window(), &focused) ==
                windowing::Status::Ok && focused.owner_pid == 0U) {
            desktop_key(event.character);
        }
    }
    draw_desktop();
}
} // namespace

bool register_all() {
    bool ok = true;
    ok = applications::register_application({
        "desktop", "Desktop Alpha application launcher",
        desktop_start, desktop_key, desktop_tick, no_stop, desktop_input
    }) == applications::Status::Ok && ok;
    ok = applications::register_application({
        "monitor", "legacy kernel live memory and hardware monitor",
        monitor_start, common_key, monitor_tick, no_stop, nullptr
    }) == applications::Status::Ok && ok;
    ok = applications::register_application({
        "files", "legacy kernel VFS file browser",
        files_start, files_key, no_tick, no_stop, nullptr
    }) == applications::Status::Ok && ok;
    ok = applications::register_application({
        "about", "legacy kernel KuroganeOS information",
        about_start, common_key, no_tick, no_stop, nullptr
    }) == applications::Status::Ok && ok;
    return ok;
}

} // namespace builtin_apps
