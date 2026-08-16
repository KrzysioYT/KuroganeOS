#include "terminal.hpp"

#include "arch/x86_64/interrupts.hpp"
#include "drivers/framebuffer.hpp"
#include "drivers/serial.hpp"
#include "core/string.hpp"
#include "ui/ui.hpp"

namespace terminal {

namespace {
constexpr uint32_t kDefaultForeground = graphics::rgb(226, 232, 240);
constexpr uint32_t kDefaultBackground = graphics::rgb(7, 8, 10);

bool g_initialized = false;
bool g_framebuffer_output = true;
bool g_required_success_deferred = false;
uint32_t g_foreground = kDefaultForeground;
uint32_t g_background = kDefaultBackground;
uint32_t g_scale = 1;
size_t g_columns = 0;
size_t g_rows = 0;
size_t g_column = 0;
size_t g_row = 0;

constexpr char kRequiredSuccess[] =
    "[TEST] ALL_REQUIRED_TESTS_PASSED";
constexpr char kRequiredFailure[] =
    "[TEST] ALL_REQUIRED_TESTS_PASSED: FAIL";
constexpr char kUserspaceInitSuccess[] =
    "[TEST] userspace_init_spawn: PASS";
constexpr char kSafeModeReady[] =
    "Type 'help' for emergency kernel commands.";

uint32_t cell_width() {
    return 6 * g_scale;
}

uint32_t cell_height() {
    return 8 * g_scale;
}

bool starts_with(const char* text, const char* prefix) {
    if (text == nullptr || prefix == nullptr) return false;
    size_t index = 0U;
    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) return false;
        ++index;
    }
    return true;
}

void draw_cell(size_t column, size_t row, char character) {
    if (!graphics::available() || !g_framebuffer_output) {
        return;
    }
    graphics::draw_char(
        static_cast<int32_t>(column * cell_width()),
        static_cast<int32_t>(row * cell_height()), character,
        g_foreground, g_background, g_scale);
}

void ensure_visible() {
    if (!g_framebuffer_output || g_rows == 0 || g_row < g_rows) {
        return;
    }
    graphics::scroll_up(cell_height(), g_background);
    g_row = g_rows - 1;
}

void write_line_unfiltered(const char* text) {
    if (text) {
        while (*text) {
            put(*text++);
        }
    }
    put('\n');
}

void flush_required_success() {
    if (!g_required_success_deferred) {
        return;
    }
    g_required_success_deferred = false;
    write_line_unfiltered(kRequiredSuccess);
}

void enable_service_console(const char* heading) {
    if (!g_initialized || !graphics::available()) return;
    g_framebuffer_output = true;
    g_column = 0U;
    g_row = 4U;
    graphics::reset_clip();
    graphics::reset_text_scale_limit();
    graphics::clear(g_background);
    graphics::fill_rect(
        0, 0, static_cast<int32_t>(graphics::width()), 3,
        graphics::rgb(204, 22, 40));
    graphics::draw_text(
        16, 14, "KUROGANEOS / SERVICE CONSOLE",
        graphics::rgb(238, 239, 242), g_background, 2U, true);
    graphics::draw_text(
        16, 38, heading ? heading : "SERVICE MODE",
        graphics::rgb(150, 153, 160), g_background, 1U, true);
}

void update_boot_splash(const char* text) {
    if (g_framebuffer_output || !graphics::available() || text == nullptr) return;
    if (kstd::streq(text, "[TEST] paging: PASS")) {
        ui::boot_splash("MEMORY / VIRTUAL ADDRESSING", 34U);
    } else if (kstd::streq(text, "[TEST] user_runtime: PASS")) {
        ui::boot_splash("RING 3 / PROCESS RUNTIME", 48U);
    } else if (kstd::streq(text, "[TEST] ramfs_bootstrap: PASS")) {
        ui::boot_splash("FILESYSTEM / ROOT SERVICES", 58U);
    } else if (kstd::streq(text, "[TEST] fat32_vfs_read: PASS")) {
        ui::boot_splash("PERSISTENT STORAGE", 68U);
    } else if (kstd::streq(text, "[TEST] kernel_preemption: PASS")) {
        ui::boot_splash("SCHEDULER / INPUT SERVICES", 80U);
    } else if (kstd::streq(text, "[TEST] userspace_init_spawn: PASS")) {
        ui::boot_splash("STARTING RED FLUX SESSION", 96U);
    }
}
} // namespace

bool configure(const KuroganeFramebuffer& framebuffer) {
    if (!graphics::init(framebuffer)) {
        g_initialized = false;
        return false;
    }
    g_scale = framebuffer.width >= 640 && framebuffer.height >= 400 ? 2 : 1;
    g_columns = framebuffer.width / cell_width();
    g_rows = framebuffer.height / cell_height();
    if (g_columns == 0 || g_rows == 0) {
        g_initialized = false;
        return false;
    }
    g_initialized = true;
    g_framebuffer_output = false;
    g_column = 0U;
    g_row = 0U;
    ui::boot_splash("INITIALIZING KERNEL", 18U);
    return true;
}

void init() {
    if (!serial::ready()) {
        serial::init();
    }
}

bool ready() {
    return g_initialized;
}

void set_framebuffer_output(bool enabled) {
    if (g_framebuffer_output == enabled) {
        return;
    }
    g_framebuffer_output = enabled;
    g_column = 0;
    g_row = 0;
    if (enabled && g_initialized && graphics::available()) {
        graphics::clear(g_background);
    }
}

bool framebuffer_output_enabled() {
    return g_framebuffer_output;
}

void put(char character) {
    init();
    if (character == '\n') {
        serial::put('\r');
    }
    serial::put(character);

    // During normal Red Flux boot and desktop ownership stdout/stderr and
    // kernel diagnostics stay available over serial, but they must never
    // overwrite the graphical boot/session framebuffer.
    if (!g_initialized || !g_framebuffer_output) {
        return;
    }

    switch (character) {
    case '\n':
        g_column = 0;
        ++g_row;
        ensure_visible();
        break;
    case '\r':
        g_column = 0;
        break;
    case '\t': {
        const size_t spaces = 4 - (g_column & static_cast<size_t>(3));
        for (size_t index = 0; index < spaces; ++index) {
            put(' ');
        }
        break;
    }
    case '\b':
        backspace();
        break;
    default:
        if (static_cast<unsigned char>(character) < 0x20) {
            break;
        }
        draw_cell(g_column, g_row, character);
        ++g_column;
        if (g_column >= g_columns) {
            g_column = 0;
            ++g_row;
            ensure_visible();
        }
        break;
    }
}

void write(const char* text) {
    if (!text) {
        return;
    }
    while (*text) {
        put(*text++);
    }
}

void println(const char* text) {
    if (!g_framebuffer_output && text != nullptr) {
        if (starts_with(text, "INSTALLER MODE:")) {
            enable_service_console("INSTALLER MODE");
        } else if (starts_with(text, "DIAGNOSTICS MODE:")) {
            enable_service_console("DIAGNOSTICS MODE");
        } else if (starts_with(text, "SAFE MODE:")) {
            enable_service_console("SAFE MODE");
        } else {
            update_boot_splash(text);
        }
    }

    // KuroganeOS 2.0 emitted the global success marker before the required
    // PID 1 spawn test. Keep the compatibility sequence, but gate its public
    // result so successful boot is only reported after userspace init exists.
    if (text && kstd::streq(text, kRequiredSuccess)) {
        g_required_success_deferred = true;
        return;
    }

    // A required-test failure is never hidden behind the graphical splash.
    if (text && kstd::streq(text, kRequiredFailure)) {
        g_required_success_deferred = false;
        if (!g_framebuffer_output) enable_service_console("BOOT FAILURE");
        write_line_unfiltered(text);
        arch::x86_64::interrupts::halt();
    }

    write_line_unfiltered(text);

    if (text && (kstd::streq(text, kUserspaceInitSuccess) ||
                 kstd::streq(text, kSafeModeReady))) {
        flush_required_success();
    }
}

void clear() {
    if (graphics::available() && g_framebuffer_output) {
        graphics::clear(g_background);
    }
    g_column = 0;
    g_row = 0;
    serial::write("\n[terminal cleared]\n");
}

void backspace() {
    if (!g_initialized || !g_framebuffer_output ||
        (g_column == 0 && g_row == 0)) {
        return;
    }
    if (g_column == 0) {
        --g_row;
        g_column = g_columns - 1;
    } else {
        --g_column;
    }
    draw_cell(g_column, g_row, ' ');
}

void set_colors(uint32_t foreground, uint32_t background) {
    g_foreground = foreground;
    g_background = background;
}

void reset_colors() {
    g_foreground = kDefaultForeground;
    g_background = kDefaultBackground;
}

void write_u64(uint64_t value) {
    char buffer[32];
    kstd::format_u64(buffer, sizeof(buffer), value);
    write(buffer);
}

void write_hex(uint64_t value) {
    char buffer[32] = "0x";
    kstd::format_u64(buffer + 2, sizeof(buffer) - 2, value, 16, true);
    write(buffer);
}

size_t columns() {
    return g_columns;
}

size_t rows() {
    return g_rows;
}

size_t cursor_column() {
    return g_column;
}

size_t cursor_row() {
    return g_row;
}

} // namespace terminal
