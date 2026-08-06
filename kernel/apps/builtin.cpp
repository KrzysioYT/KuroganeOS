#include "builtin.hpp"

#include "framework.hpp"
#include "../core/string.hpp"
#include "../drivers/framebuffer.hpp"
#include "../drivers/pci.hpp"
#include "../drivers/rtc.hpp"
#include "../fs/ramfs.hpp"
#include "../memory/allocator.hpp"
#include "../memory/physical_memory.hpp"
#include "../terminal.hpp"
#include "../ui/ui.hpp"
#include "../../common/version.h"

namespace builtin_apps {

namespace {
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

void switch_to(const char* name) {
    applications::stop();
    applications::launch(name);
}

void common_key(char character) {
    if (character == 'q' || character == 'Q' || character == 27) {
        leave_application();
    }
}

void desktop_key(char character) {
    switch (character) {
    case '1':
        switch_to("monitor");
        break;
    case '2':
        switch_to("files");
        break;
    case '3':
        switch_to("about");
        break;
    case 'm':
    case 'M':
        switch_to("monitor");
        break;
    case 'f':
    case 'F':
        switch_to("files");
        break;
    case 'a':
    case 'A':
        switch_to("about");
        break;
    default:
        common_key(character);
        break;
    }
}

void draw_desktop() {
    ui::desktop("KUROGANE OS");

    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t button_width = 180;
    const int32_t button_height = 60;
    const int32_t spacing = 24;
    const int32_t total_width = button_width * 3 + spacing * 2;
    const int32_t start_x = (width - total_width) / 2;
    const int32_t button_y = 90;

    ui::button({start_x, button_y, button_width, button_height},
               "1: Monitor", false);
    ui::button({start_x + button_width + spacing, button_y,
                button_width, button_height},
               "2: Files", false);
    ui::button({start_x + 2 * (button_width + spacing), button_y,
                button_width, button_height},
               "3: About", false);

    const int32_t label_x = 32;
    int32_t label_y = button_y + button_height + 36;
    graphics::draw_text(label_x, label_y,
                        "Press 1/2/3 to open an app",
                        ui::default_theme().text,
                        ui::default_theme().desktop, 2, true);
    label_y += 28;
    graphics::draw_text(label_x, label_y,
                        "or Q to return to the shell",
                        ui::default_theme().text_muted,
                        ui::default_theme().desktop, 2, true);

    ui::taskbar("1: Monitor  2: Files  3: About  Q: Return to shell");
}

bool desktop_start(const char*) {
    if (!graphics::available()) {
        return false;
    }
    draw_desktop();
    return true;
}

void desktop_tick(uint64_t tick) {
    static uint64_t last_draw = 0;
    if (tick - last_draw >= 25) {
        last_draw = tick;
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
} // namespace

bool register_all() {
    bool ok = true;
    ok = applications::register_application({
        "desktop", "graphical application launcher",
        desktop_start, desktop_key, desktop_tick, no_stop
    }) == applications::Status::Ok && ok;
    ok = applications::register_application({
        "monitor", "live memory and hardware monitor",
        monitor_start, common_key, monitor_tick, no_stop
    }) == applications::Status::Ok && ok;
    ok = applications::register_application({
        "files", "RAMFS file browser",
        files_start, files_key, no_tick, no_stop
    }) == applications::Status::Ok && ok;
    ok = applications::register_application({
        "about", "KuroganeOS information",
        about_start, common_key, no_tick, no_stop
    }) == applications::Status::Ok && ok;
    return ok;
}

} // namespace builtin_apps
