#include "window_manager.hpp"

#include <kurogane/desktop.h>

#ifndef KUROGANE_HOST_TEST
#include "../drivers/framebuffer.hpp"
#include "../task/process.hpp"
#include "icon_registry.hpp"
#endif

namespace windowing {
namespace {

constexpr int32_t HEADER_HEIGHT = 36;
constexpr int32_t MINIMUM_WIDTH = 260;
constexpr int32_t MINIMUM_HEIGHT = 160;
constexpr int32_t WORKSPACE_LEFT = 112;
constexpr int32_t WORKSPACE_TOP = 64;
constexpr int32_t WORKSPACE_RIGHT = 12;
constexpr int32_t DOCK_HEIGHT = 58;
constexpr int32_t DOCK_BOTTOM = 8;
constexpr int32_t WORKSPACE_BOTTOM = DOCK_HEIGHT + DOCK_BOTTOM + 8;
constexpr int32_t SPINE_X = 9;
constexpr int32_t SPINE_WIDTH = 90;
constexpr int32_t CONTROL_WIDTH = 22;
constexpr int32_t CONTROL_HEIGHT = 20;
constexpr int32_t CONTROL_GAP = 4;
constexpr int32_t CONTROL_RIGHT = 8;
constexpr int32_t DOCK_PADDING = 12;
constexpr int32_t DOCK_PIN_SIZE = 42;
constexpr int32_t DOCK_HOME_WIDTH = 78;
constexpr int32_t DOCK_PIN_GAP = 8;
constexpr int32_t DOCK_SEPARATOR = 16;
constexpr int32_t DESKTOP_SHORTCUT_WIDTH = 76;
constexpr int32_t DESKTOP_SHORTCUT_HEIGHT = 78;
constexpr int32_t DESKTOP_SHORTCUT_STEP_X = 88;
constexpr int32_t DESKTOP_SHORTCUT_STEP_Y = 84;
constexpr int32_t RIBBON_GAP = 6;
constexpr int32_t RIBBON_ITEM_MAX = 184;
constexpr int32_t RIBBON_ITEM_MIN = 72;
constexpr size_t DOCK_PIN_COUNT = KU_DESKTOP_APP_COUNT;
constexpr size_t BLADE_PIN_COUNT = 6U;
constexpr uint32_t DESKTOP_PIN_QUERY = 0U;
constexpr uint32_t DESKTOP_PIN_SET = 1U;
constexpr uint32_t DESKTOP_PIN_TOGGLE = 2U;

enum class DirtyMode : uint8_t {
    None = 0,
    Full,
};

struct Slot {
    WindowInfo info;
    DrawCallback draw;
    InputCallback input_callback;
    void* context;
    uint16_t generation;
    CursorHint cursor_hint;
    bool occupied;
};

struct DockPin {
    const char* title;
    char command;
    ui::DockIcon icon;
    const char* shortcut_label;
};

// Order is the public ku_desktop_app_id ABI.
constexpr DockPin kDockPins[DOCK_PIN_COUNT] = {
    {"BLADE LAUNCHER", 0, ui::DockIcon::Home, "BLADE"},
    {"KUROSH", 't', ui::DockIcon::Terminal, "KUROSH"},
    {"VAULT", 'f', ui::DockIcon::Files, "VAULT"},
    {"PERFORMANCE", 'v', ui::DockIcon::Performance, "PERF"},
    {"KUROGANE WEB", 'b', ui::DockIcon::Web, "WEB"},
    {"SYSTEM MONITOR", 'm', ui::DockIcon::SystemMonitor, "MON"},
    {"FORGE CONTROL", 's', ui::DockIcon::Settings, "FORGE"},
    {"ABOUT KUROGANEOS", 'a', ui::DockIcon::About, "ABOUT"},
    {"ANVIL", 'i', ui::DockIcon::Anvil, "ANVIL"},
    {"PULSE", 'u', ui::DockIcon::Pulse, "PULSE"},
};

// Permanent Blade rail order mirrors the reference desktop while the full
// catalogue remains available from the Home launcher.
constexpr size_t kBladePins[BLADE_PIN_COUNT] = {0U, 2U, 6U, 1U, 4U, 8U};
constexpr const char* kBladeLabels[BLADE_PIN_COUNT] = {
    "HOME", "VAULT", "SYSTEM", "TERMINAL", "DOCS", "ANVIL"};

Slot g_slots[MAX_WINDOWS]{};
uint8_t g_order[MAX_WINDOWS]{};
bool g_desktop_pinned[DOCK_PIN_COUNT]{};
size_t g_count = 0U;
int32_t g_screen_width = 0;
int32_t g_screen_height = 0;
WindowId g_focused = INVALID_WINDOW;
WindowId g_dragged = INVALID_WINDOW;
WindowId g_resized = INVALID_WINDOW;
int32_t g_drag_offset_x = 0;
int32_t g_drag_offset_y = 0;
DirtyMode g_dirty = DirtyMode::None;
bool g_initialized = false;
#ifndef KUROGANE_HOST_TEST
process::ProcessId g_session_root_pid = process::INVALID_PROCESS_ID;
#endif

#ifndef KUROGANE_HOST_TEST
bool g_cursor_visible = false;
int32_t g_cursor_x = 0;
int32_t g_cursor_y = 0;
ui::icons::Cursor g_cursor_shape = ui::icons::Cursor::Default;
constexpr int32_t CURSOR_SIZE = 24;
graphics::Color g_cursor_under[CURSOR_SIZE * CURSOR_SIZE]{};
#endif

void mark_full_dirty() {
    g_dirty = DirtyMode::Full;
}

size_t text_length(const char* text, size_t maximum) {
    if (text == nullptr) return 0U;
    size_t length = 0U;
    while (length < maximum && text[length] != '\0') ++length;
    return length;
}

bool text_equals(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    size_t index = 0U;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

void copy_title(char* destination, const char* title) {
    size_t index = 0U;
    while (index < 32U && title[index] != '\0') {
        destination[index] = title[index];
        ++index;
    }
    destination[index] = '\0';
}

WindowId make_id(size_t slot, uint16_t generation) {
    return (static_cast<uint32_t>(generation) << 8U) |
        static_cast<uint32_t>(slot + 1U);
}

Slot* find(WindowId id, size_t* out_slot = nullptr) {
    if (id == INVALID_WINDOW) return nullptr;
    const uint32_t encoded = id & UINT32_C(0xff);
    if (encoded == 0U) return nullptr;
    const size_t slot = encoded - 1U;
    if (slot >= MAX_WINDOWS) return nullptr;
    Slot& candidate = g_slots[slot];
    if (!candidate.occupied || candidate.info.id != id) return nullptr;
    if (out_slot != nullptr) *out_slot = slot;
    return &candidate;
}

bool exposed(const Slot& slot) {
    return slot.occupied && slot.info.owner_pid != 0U;
}

Slot* find_by_title(const char* title) {
    for (size_t index = 0U; index < MAX_WINDOWS; ++index) {
        Slot& slot = g_slots[index];
        if (exposed(slot) && text_equals(slot.info.title, title)) return &slot;
    }
    return nullptr;
}

bool is_login_surface(const Slot& slot) {
    return text_equals(slot.info.title, "KUROGANE LOGIN");
}

bool is_home_surface(const char* title) {
    return text_equals(title, "BLADE LAUNCHER") ||
        text_equals(title, "RED FLUX HOME");
}

bool is_performance_surface(const char* title) {
    return text_equals(title, "PERFORMANCE");
}

Slot* login_surface() {
    Slot* login = find_by_title("KUROGANE LOGIN");
    return login != nullptr && login->info.state != WindowState::Minimized
        ? login : nullptr;
}

size_t exposed_window_count() {
    size_t count = 0U;
    for (size_t position = 0U; position < g_count; ++position) {
        const Slot& slot = g_slots[g_order[position]];
        if (exposed(slot) && !is_home_surface(slot.info.title)) ++count;
    }
    return count;
}

Slot* exposed_at(size_t exposed_position) {
    size_t current = 0U;
    for (size_t position = 0U; position < g_count; ++position) {
        Slot& slot = g_slots[g_order[position]];
        if (!exposed(slot) || is_home_surface(slot.info.title)) continue;
        if (current++ == exposed_position) return &slot;
    }
    return nullptr;
}

#ifndef KUROGANE_HOST_TEST
bool belongs_to_session_tree(process::ProcessId pid) {
    if (g_session_root_pid == process::INVALID_PROCESS_ID ||
        pid == process::INVALID_PROCESS_ID) return false;
    process::ProcessId current = pid;
    for (size_t depth = 0U; depth < process::MAX_PROCESSES; ++depth) {
        if (current == g_session_root_pid) return true;
        process::Stat stat{};
        if (process::stat(current, &stat) != process::Status::Ok ||
            stat.parent_pid == process::INVALID_PROCESS_ID ||
            stat.parent_pid == current) return false;
        current = stat.parent_pid;
    }
    return false;
}
#endif

int32_t clamp_position(int32_t value, int32_t lower, int32_t upper) {
    if (upper < lower) return lower;
    if (value < lower) return lower;
    if (value > upper) return upper;
    return value;
}

int32_t clamp_size(int32_t value, int32_t lower, int32_t upper) {
    if (upper < lower) return lower;
    if (value < lower) return lower;
    if (value > upper) return upper;
    return value;
}

bool rect_contains(const ui::Rect& rectangle, int32_t x, int32_t y) {
    return x >= rectangle.x && y >= rectangle.y &&
        x < rectangle.x + rectangle.width &&
        y < rectangle.y + rectangle.height;
}

int32_t pinned_section_width() {
    return DOCK_HOME_WIDTH;
}

WorkspaceGeometry calculate_workspace() {
    WorkspaceGeometry geometry{};
    if (!g_initialized) return geometry;

    const int32_t work_width = g_screen_width - WORKSPACE_LEFT - WORKSPACE_RIGHT;
    const int32_t work_height = g_screen_height - WORKSPACE_TOP - WORKSPACE_BOTTOM;
    geometry.work_area = {
        WORKSPACE_LEFT,
        WORKSPACE_TOP,
        work_width > 1 ? work_width : 1,
        work_height > 1 ? work_height : 1,
    };
    geometry.signal_spine = {
        SPINE_X,
        WORKSPACE_TOP + 8,
        SPINE_WIDTH,
        work_height > 24 ? work_height - 16 : work_height,
    };

    const int32_t dock_width = g_screen_width > 16 ? g_screen_width - 16 : 1;
    geometry.pulse_ribbon = {
        8,
        g_screen_height - DOCK_HEIGHT - DOCK_BOTTOM,
        dock_width,
        DOCK_HEIGHT,
    };
    return geometry;
}

ChromeGeometry calculate_chrome(const ui::Rect& bounds) {
    ChromeGeometry geometry{};
    geometry.header = {bounds.x, bounds.y, bounds.width, HEADER_HEIGHT};
    const int32_t control_y = bounds.y + (HEADER_HEIGHT - CONTROL_HEIGHT) / 2;
    const int32_t dismiss_x =
        bounds.x + bounds.width - CONTROL_RIGHT - CONTROL_WIDTH;
    const int32_t expand_x = dismiss_x - CONTROL_GAP - CONTROL_WIDTH;
    const int32_t minimize_x = expand_x - CONTROL_GAP - CONTROL_WIDTH;
    geometry.minimize_control = {
        minimize_x, control_y, CONTROL_WIDTH, CONTROL_HEIGHT};
    geometry.expand_control = {
        expand_x, control_y, CONTROL_WIDTH, CONTROL_HEIGHT};
    geometry.dismiss_control = {
        dismiss_x, control_y, CONTROL_WIDTH, CONTROL_HEIGHT};
    geometry.resize_grip = {
        bounds.x + bounds.width - 18,
        bounds.y + bounds.height - 18,
        18,
        18,
    };
    return geometry;
}

ui::Rect dock_pin_rect(size_t position) {
    const WorkspaceGeometry workspace = calculate_workspace();
    if (position != 0U || position >= DOCK_PIN_COUNT) return {};
    const int32_t base_x = workspace.pulse_ribbon.x + DOCK_PADDING;
    if (position == 0U) {
        return {
            base_x,
            workspace.pulse_ribbon.y + (DOCK_HEIGHT - DOCK_PIN_SIZE) / 2,
            DOCK_HOME_WIDTH,
            DOCK_PIN_SIZE,
        };
    }
    return {};
}

ui::Rect blade_item_rect(size_t position) {
    if (position >= BLADE_PIN_COUNT) return {};
    const WorkspaceGeometry workspace = calculate_workspace();
    const int32_t top = workspace.signal_spine.y + 36;
    const int32_t available = workspace.signal_spine.height - 44;
    const int32_t gap = 5;
    int32_t height = (available - gap * static_cast<int32_t>(BLADE_PIN_COUNT - 1U)) /
        static_cast<int32_t>(BLADE_PIN_COUNT);
    height = clamp_size(height, 54, 74);
    return {
        workspace.signal_spine.x + 7,
        top + static_cast<int32_t>(position) * (height + gap),
        workspace.signal_spine.width - 14,
        height,
    };
}

ui::Rect desktop_shortcut_rect(size_t app_id) {
    if (app_id >= DOCK_PIN_COUNT || !g_desktop_pinned[app_id]) return {};
    const WorkspaceGeometry workspace = calculate_workspace();
    size_t ordinal = 0U;
    for (size_t index = 0U; index < app_id; ++index) {
        if (g_desktop_pinned[index]) ++ordinal;
    }
    int32_t rows = workspace.work_area.height / DESKTOP_SHORTCUT_STEP_Y;
    if (rows < 1) rows = 1;
    const int32_t row = static_cast<int32_t>(ordinal % static_cast<size_t>(rows));
    const int32_t column = static_cast<int32_t>(ordinal / static_cast<size_t>(rows));
    return {
        workspace.work_area.x + 20 + column * DESKTOP_SHORTCUT_STEP_X,
        workspace.work_area.y + 22 + row * DESKTOP_SHORTCUT_STEP_Y,
        DESKTOP_SHORTCUT_WIDTH,
        DESKTOP_SHORTCUT_HEIGHT,
    };
}

int32_t ribbon_item_width(const WorkspaceGeometry& workspace) {
    const size_t tasks = exposed_window_count();
    if (tasks == 0U) return 0;
    const int32_t task_start = DOCK_PADDING + pinned_section_width() +
        DOCK_SEPARATOR;
    const int32_t usable = workspace.pulse_ribbon.width - task_start -
        DOCK_PADDING - static_cast<int32_t>(tasks - 1U) * RIBBON_GAP;
    return clamp_size(
        usable / static_cast<int32_t>(tasks),
        RIBBON_ITEM_MIN,
        RIBBON_ITEM_MAX);
}

ui::Rect ribbon_item_rect(size_t position) {
    const WorkspaceGeometry workspace = calculate_workspace();
    const size_t tasks = exposed_window_count();
    if (position >= tasks) return {};
    const int32_t width = ribbon_item_width(workspace);
    const int32_t task_start = workspace.pulse_ribbon.x + DOCK_PADDING +
        pinned_section_width() + DOCK_SEPARATOR;
    return {
        task_start + static_cast<int32_t>(position) * (width + RIBBON_GAP),
        workspace.pulse_ribbon.y + 12,
        width,
        workspace.pulse_ribbon.height - 24,
    };
}

bool valid_bounds(const ui::Rect& bounds) {
    if (!g_initialized) return false;
    const WorkspaceGeometry workspace = calculate_workspace();
    if (bounds.width < MINIMUM_WIDTH || bounds.height < MINIMUM_HEIGHT ||
        bounds.width > workspace.work_area.width ||
        bounds.height > workspace.work_area.height) return false;
    return bounds.x >= workspace.work_area.x &&
        bounds.y >= workspace.work_area.y &&
        bounds.x <= workspace.work_area.x + workspace.work_area.width - bounds.width &&
        bounds.y <= workspace.work_area.y + workspace.work_area.height - bounds.height;
}

ui::Rect normalize_new_window_bounds(const char* title, const ui::Rect& requested) {
    const WorkspaceGeometry workspace = calculate_workspace();
    ui::Rect normalized = requested;
    if (is_performance_surface(title) &&
        workspace.work_area.width >= 300 && workspace.work_area.height >= 240) {
        const int32_t width = workspace.work_area.width >= 390 ? 360 : 300;
        const int32_t height = workspace.work_area.height >= 350 ? 310 : 240;
        normalized = {
            workspace.work_area.x + workspace.work_area.width - width - 18,
            workspace.work_area.y + (workspace.work_area.height - height) / 2,
            width,
            height,
        };
    }
    normalized.x = clamp_position(
        normalized.x,
        workspace.work_area.x,
        workspace.work_area.x + workspace.work_area.width - normalized.width);
    normalized.y = clamp_position(
        normalized.y,
        workspace.work_area.y,
        workspace.work_area.y + workspace.work_area.height - normalized.height);
    return normalized;
}

void update_z_order() {
    for (size_t position = 0U; position < g_count; ++position) {
        g_slots[g_order[position]].info.z_order = static_cast<uint8_t>(position);
        g_slots[g_order[position]].info.focused =
            g_slots[g_order[position]].info.id == g_focused;
    }
}

#ifndef KUROGANE_HOST_TEST
void purge_exposed_session() {
    size_t write_position = 0U;
    for (size_t position = 0U; position < g_count; ++position) {
        const uint8_t slot_index = g_order[position];
        Slot& slot = g_slots[slot_index];
        if (!slot.occupied) continue;
        if (slot.info.owner_pid == 0U) {
            g_order[write_position++] = slot_index;
            continue;
        }
        static_cast<void>(process::terminate(slot.info.owner_pid, 0));
        slot.occupied = false;
        slot.draw = nullptr;
        slot.input_callback = nullptr;
        slot.context = nullptr;
    }
    g_count = write_position;
    g_focused = INVALID_WINDOW;
    g_dragged = INVALID_WINDOW;
    g_resized = INVALID_WINDOW;
    update_z_order();
    mark_full_dirty();
}
#endif

void choose_top_focus() {
    g_focused = INVALID_WINDOW;
    for (size_t position = g_count; position > 0U; --position) {
        Slot& slot = g_slots[g_order[position - 1U]];
        if (exposed(slot) && slot.info.state != WindowState::Minimized) {
            g_focused = slot.info.id;
            break;
        }
    }
    update_z_order();
}

bool title_hit(const Slot& slot, int32_t x, int32_t y) {
    if (is_login_surface(slot)) return false;
    const ChromeGeometry chrome = calculate_chrome(slot.info.bounds);
    return rect_contains(chrome.header, x, y) &&
        !rect_contains(chrome.minimize_control, x, y) &&
        !rect_contains(chrome.expand_control, x, y) &&
        !rect_contains(chrome.dismiss_control, x, y);
}

WindowId hit_test(int32_t x, int32_t y) {
    for (size_t position = g_count; position > 0U; --position) {
        Slot& slot = g_slots[g_order[position - 1U]];
        if (exposed(slot) && slot.info.state != WindowState::Minimized &&
            rect_contains(slot.info.bounds, x, y)) return slot.info.id;
    }
    return INVALID_WINDOW;
}

Status cycle_focus() {
    const size_t tasks = exposed_window_count();
    if (tasks == 0U) return Status::NotFound;
    size_t current = tasks;
    for (size_t position = 0U; position < tasks; ++position) {
        Slot* slot = exposed_at(position);
        if (slot != nullptr && slot->info.id == g_focused) {
            current = position;
            break;
        }
    }
    for (size_t offset = 1U; offset <= tasks; ++offset) {
        const size_t position = (current + offset) % tasks;
        Slot* slot = exposed_at(position);
        if (slot != nullptr && slot->info.state != WindowState::Minimized) {
            return focus(slot->info.id);
        }
    }
    return Status::NotFound;
}

Status activate_ribbon_item(size_t position) {
    Slot* slot = exposed_at(position);
    if (slot == nullptr) return Status::NotFound;
    if (slot->info.state == WindowState::Minimized) return restore(slot->info.id);
    return focus(slot->info.id);
}

Status activate_dock_pin(size_t position) {
    if (position >= DOCK_PIN_COUNT) return Status::NotFound;
    const DockPin& pin = kDockPins[position];
    Slot* existing = find_by_title(pin.title);
    if (existing != nullptr) {
        if (existing->info.state == WindowState::Minimized) {
            return restore(existing->info.id);
        }
        return focus(existing->info.id);
    }
    if (pin.command == 0) return Status::NotFound;

    Slot* launcher = find_by_title("BLADE LAUNCHER");
    if (launcher == nullptr) launcher = find_by_title("RED FLUX HOME");
    if (launcher == nullptr || launcher->input_callback == nullptr) {
        return Status::NotFound;
    }
    input::Event synthetic{};
    synthetic.type = input::EventType::KeyDown;
    synthetic.key = drivers::keyboard::KeyCode::Unknown;
    synthetic.character = pin.command;
    launcher->input_callback(launcher->info.id, synthetic, launcher->context);
    return Status::Ok;
}

void resize_window(Slot& slot, int32_t pointer_x, int32_t pointer_y) {
    if (slot.info.state != WindowState::Normal || is_login_surface(slot)) return;
    const WorkspaceGeometry workspace = calculate_workspace();
    const int32_t maximum_width =
        workspace.work_area.x + workspace.work_area.width - slot.info.bounds.x;
    const int32_t maximum_height =
        workspace.work_area.y + workspace.work_area.height - slot.info.bounds.y;
    slot.info.bounds.width = clamp_size(
        pointer_x - slot.info.bounds.x + 1,
        MINIMUM_WIDTH,
        maximum_width);
    slot.info.bounds.height = clamp_size(
        pointer_y - slot.info.bounds.y + 1,
        MINIMUM_HEIGHT,
        maximum_height);
    slot.info.restore_bounds = slot.info.bounds;
    mark_full_dirty();
}

#ifndef KUROGANE_HOST_TEST
ui::icons::Cursor icon_cursor(CursorHint hint) {
    switch (hint) {
        case CursorHint::Default: return ui::icons::Cursor::Default;
        case CursorHint::Pointer: return ui::icons::Cursor::Pointer;
        case CursorHint::Hand: return ui::icons::Cursor::Hand;
        case CursorHint::Text: return ui::icons::Cursor::Text;
        case CursorHint::Working: return ui::icons::Cursor::Working;
        case CursorHint::Busy: return ui::icons::Cursor::Busy;
        case CursorHint::Move: return ui::icons::Cursor::Move;
        case CursorHint::Resize: return ui::icons::Cursor::Resize;
        case CursorHint::Help: return ui::icons::Cursor::Help;
        case CursorHint::NotAllowed: return ui::icons::Cursor::NotAllowed;
        case CursorHint::Auto: return ui::icons::Cursor::Pointer;
    }
    return ui::icons::Cursor::Default;
}

ui::icons::Cursor cursor_for_position(int32_t x, int32_t y) {
    if (g_resized != INVALID_WINDOW) return ui::icons::Cursor::Resize;
    if (g_dragged != INVALID_WINDOW) return ui::icons::Cursor::Move;
    if (login_surface() != nullptr) return ui::icons::Cursor::Default;

    const WorkspaceGeometry workspace = calculate_workspace();
    for (size_t position = 0U; position < BLADE_PIN_COUNT; ++position) {
        if (rect_contains(blade_item_rect(position), x, y)) {
            return ui::icons::Cursor::Hand;
        }
    }
    if (rect_contains(workspace.pulse_ribbon, x, y)) {
        for (size_t index = 0U; index < DOCK_PIN_COUNT; ++index) {
            if (rect_contains(dock_pin_rect(index), x, y)) {
                return ui::icons::Cursor::Hand;
            }
        }
        for (size_t position = 0U; position < exposed_window_count(); ++position) {
            if (rect_contains(ribbon_item_rect(position), x, y)) {
                return ui::icons::Cursor::Hand;
            }
        }
    }
    for (size_t app = 0U; app < DOCK_PIN_COUNT; ++app) {
        if (g_desktop_pinned[app] &&
            rect_contains(desktop_shortcut_rect(app), x, y)) {
            return ui::icons::Cursor::Hand;
        }
    }

    const WindowId target = hit_test(x, y);
    Slot* slot = find(target);
    if (slot == nullptr || is_login_surface(*slot)) {
        return ui::icons::Cursor::Default;
    }
    const ChromeGeometry chrome = calculate_chrome(slot->info.bounds);
    if (rect_contains(chrome.minimize_control, x, y) ||
        rect_contains(chrome.expand_control, x, y) ||
        rect_contains(chrome.dismiss_control, x, y)) {
        return ui::icons::Cursor::Hand;
    }
    if (slot->info.state == WindowState::Normal &&
        rect_contains(chrome.resize_grip, x, y)) {
        return ui::icons::Cursor::Resize;
    }
    if (title_hit(*slot, x, y)) return ui::icons::Cursor::Move;
    return icon_cursor(slot->cursor_hint);
}

void capture_cursor_under(int32_t x, int32_t y) {
    for (int32_t row = 0; row < CURSOR_SIZE; ++row) {
        for (int32_t column = 0; column < CURSOR_SIZE; ++column) {
            g_cursor_under[row * CURSOR_SIZE + column] =
                graphics::get_pixel(x + column, y + row);
        }
    }
}

void hide_cursor() {
    if (!g_cursor_visible) return;
    for (int32_t row = 0; row < CURSOR_SIZE; ++row) {
        for (int32_t column = 0; column < CURSOR_SIZE; ++column) {
            graphics::put_pixel(
                g_cursor_x + column, g_cursor_y + row,
                g_cursor_under[row * CURSOR_SIZE + column]);
        }
    }
    g_cursor_visible = false;
}

void show_cursor(int32_t x, int32_t y, ui::icons::Cursor shape) {
    if (g_cursor_visible) hide_cursor();
    g_cursor_x = x;
    g_cursor_y = y;
    g_cursor_shape = shape;
    capture_cursor_under(g_cursor_x, g_cursor_y);
    ui::icons::draw(
        ui::icons::cursor(g_cursor_shape),
        g_cursor_x, g_cursor_y, CURSOR_SIZE, CURSOR_SIZE);
    g_cursor_visible = true;
}

void move_cursor(int32_t x, int32_t y) {
    const ui::icons::Cursor shape = cursor_for_position(x, y);
    if (g_cursor_visible && x == g_cursor_x && y == g_cursor_y &&
        shape == g_cursor_shape) return;
    hide_cursor();
    show_cursor(x, y, shape);
}

void draw_window_slot(Slot& slot) {
    if (!exposed(slot) || slot.info.state == WindowState::Minimized) return;
    const ui::Rect& bounds = slot.info.bounds;

    if (is_login_surface(slot)) {
        if (slot.draw != nullptr) {
            graphics::set_clip(bounds.x, bounds.y, bounds.width, bounds.height);
            graphics::set_text_scale_limit(bounds.width >= 620 ? 2U : 1U);
            slot.draw(slot.info.id, bounds, true, slot.context);
            graphics::reset_text_scale_limit();
            graphics::reset_clip();
        }
        return;
    }

    const ChromeGeometry chrome = calculate_chrome(bounds);
    ui::flux_window(bounds, slot.info.title, slot.info.focused);
    ui::flux_control(chrome.minimize_control, ui::FluxControl::Minimize, slot.info.focused);
    ui::flux_control(
        chrome.expand_control,
        ui::FluxControl::Expand,
        slot.info.state == WindowState::Maximized);
    ui::flux_control(chrome.dismiss_control, ui::FluxControl::Dismiss, slot.info.focused);
    if (slot.info.state == WindowState::Normal) {
        graphics::draw_rect(
            chrome.resize_grip.x + 6,
            chrome.resize_grip.y + 6,
            8,
            8,
            graphics::rgb(224, 26, 48),
            1U);
    }
    if (slot.draw != nullptr) {
        const ui::Rect content = {
            bounds.x + 4,
            bounds.y + HEADER_HEIGHT,
            bounds.width - 8,
            bounds.height - HEADER_HEIGHT - 4,
        };
        graphics::set_clip(
            content.x, content.y, content.width, content.height);
        graphics::set_text_scale_limit(content.width >= 800 ? 2U : 1U);
        slot.draw(slot.info.id, content, slot.info.focused, slot.context);
        graphics::reset_text_scale_limit();
        graphics::reset_clip();
    }
}

void render_layers() {
    graphics::reset_clip();
    graphics::reset_text_scale_limit();

    Slot* login = login_surface();
    if (login != nullptr) {
        ui::login_backdrop("LOCAL SESSION / ENTER TO CONTINUE");
        draw_window_slot(*login);
        return;
    }

    ui::desktop("KUROGANEOS 5 / FORGED STEEL");
    const WorkspaceGeometry workspace = calculate_workspace();
    const size_t tasks = exposed_window_count();
    ui::blade_bar(workspace.signal_spine);
    for (size_t position = 0U; position < BLADE_PIN_COUNT; ++position) {
        const size_t app = kBladePins[position];
        const Slot* running = find_by_title(kDockPins[app].title);
        ui::blade_item(
            blade_item_rect(position),
            kDockPins[app].icon,
            kBladeLabels[position],
            running != nullptr,
            running != nullptr && running->info.focused);
    }

    const ui::Theme& theme = ui::default_theme();
    for (size_t app = 0U; app < DOCK_PIN_COUNT; ++app) {
        if (!g_desktop_pinned[app]) continue;
        const ui::Rect shortcut = desktop_shortcut_rect(app);
        const ui::Rect shortcut_icon = {
            shortcut.x + (shortcut.width - DOCK_PIN_SIZE) / 2,
            shortcut.y,
            DOCK_PIN_SIZE,
            DOCK_PIN_SIZE,
        };
        const Slot* running = find_by_title(kDockPins[app].title);
        ui::dock_item(
            shortcut_icon,
            kDockPins[app].icon,
            running != nullptr,
            running != nullptr && running->info.focused);
        graphics::draw_text(
            shortcut.x + 14, shortcut.y + 53, kDockPins[app].shortcut_label,
            theme.text, theme.desktop, 1U, true);
    }

    for (size_t position = 0U; position < g_count; ++position) {
        draw_window_slot(g_slots[g_order[position]]);
    }

    ui::dock_bar(workspace.pulse_ribbon, tasks);
    for (size_t index = 0U; index < 1U; ++index) {
        const Slot* running = find_by_title(kDockPins[index].title);
        const bool active = running != nullptr && running->info.focused;
        ui::dock_item(
            dock_pin_rect(index), kDockPins[index].icon,
            running != nullptr, active);
    }
    for (size_t position = 0U; position < tasks; ++position) {
        const Slot* slot = exposed_at(position);
        if (slot == nullptr) continue;
        ui::dock_task(
            ribbon_item_rect(position),
            slot->info.title,
            slot->info.focused,
            slot->info.state == WindowState::Minimized);
    }
}
#endif

} // namespace

Status initialize(uint32_t screen_width, uint32_t screen_height) {
    if (screen_width < 320U || screen_height < 200U ||
        screen_width > static_cast<uint32_t>(INT32_MAX) ||
        screen_height > static_cast<uint32_t>(INT32_MAX)) {
        return Status::InvalidArgument;
    }
    for (size_t index = 0U; index < MAX_WINDOWS; ++index) {
        const uint16_t previous_generation = g_slots[index].generation;
        g_slots[index] = {};
        g_slots[index].generation = previous_generation;
    }
    for (size_t index = 0U; index < DOCK_PIN_COUNT; ++index) {
        g_desktop_pinned[index] = false;
    }
    g_desktop_pinned[0U] = true;
    g_desktop_pinned[3U] = true;
    g_count = 0U;
    g_screen_width = static_cast<int32_t>(screen_width);
    g_screen_height = static_cast<int32_t>(screen_height);
    g_focused = INVALID_WINDOW;
    g_dragged = INVALID_WINDOW;
    g_resized = INVALID_WINDOW;
#ifndef KUROGANE_HOST_TEST
    g_session_root_pid = process::INVALID_PROCESS_ID;
    g_cursor_visible = false;
    g_cursor_x = input::pointer_x();
    g_cursor_y = input::pointer_y();
#endif
    g_initialized = true;
    mark_full_dirty();
    return Status::Ok;
}

Status create_window(
    const char* title,
    uint64_t owner_pid,
    const ui::Rect& bounds,
    DrawCallback draw,
    InputCallback input_callback,
    void* context,
    WindowId* out_id) {
    if (!g_initialized) return Status::NotInitialized;
    if (title == nullptr || out_id == nullptr || text_length(title, 33U) == 0U ||
        text_length(title, 33U) > 32U) {
        return Status::InvalidArgument;
    }
    const ui::Rect requested_bounds = normalize_new_window_bounds(title, bounds);
    if (!valid_bounds(requested_bounds)) return Status::InvalidArgument;

    const bool home = is_home_surface(title);
#ifndef KUROGANE_HOST_TEST
    const bool login = text_equals(title, "KUROGANE LOGIN");
    if (login) {
        purge_exposed_session();
        g_session_root_pid = process::INVALID_PROCESS_ID;
    } else if (home) {
        g_session_root_pid = owner_pid;
    } else if (owner_pid != 0U && !belongs_to_session_tree(owner_pid)) {
        return Status::InvalidState;
    }
#endif

    if (g_count == MAX_WINDOWS) return Status::CapacityReached;
    size_t selected = MAX_WINDOWS;
    for (size_t index = 0U; index < MAX_WINDOWS; ++index) {
        if (!g_slots[index].occupied) {
            selected = index;
            break;
        }
    }
    if (selected == MAX_WINDOWS) return Status::CapacityReached;
    Slot& slot = g_slots[selected];
    ++slot.generation;
    if (slot.generation == 0U) ++slot.generation;
    slot.occupied = true;
    slot.info = {};
    slot.info.id = make_id(selected, slot.generation);
    slot.info.bounds = requested_bounds;
    slot.info.restore_bounds = requested_bounds;
    slot.info.owner_pid = owner_pid;
    slot.info.state = (owner_pid == 0U || home)
        ? WindowState::Minimized : WindowState::Normal;
    copy_title(slot.info.title, title);
    slot.draw = draw;
    slot.input_callback = input_callback;
    slot.context = context;
    slot.cursor_hint = CursorHint::Auto;
    g_order[g_count++] = static_cast<uint8_t>(selected);
    if (owner_pid != 0U && !home) g_focused = slot.info.id;
    update_z_order();
    mark_full_dirty();
    *out_id = slot.info.id;
    return Status::Ok;
}

Status close(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    size_t slot_index = 0U;
    Slot* slot = find(id, &slot_index);
    if (slot == nullptr) return Status::NotFound;

    if (is_home_surface(slot->info.title)) {
        if (slot->info.state == WindowState::Minimized) return Status::Ok;
        return minimize(id);
    }

    size_t position = 0U;
    while (position < g_count && g_order[position] != slot_index) ++position;
    for (size_t index = position + 1U; index < g_count; ++index) {
        g_order[index - 1U] = g_order[index];
    }
    --g_count;
    slot->occupied = false;
    if (g_dragged == id) g_dragged = INVALID_WINDOW;
    if (g_resized == id) g_resized = INVALID_WINDOW;
    choose_top_focus();
    mark_full_dirty();
    return Status::Ok;
}

Status focus(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    if (g_count == 0U) return Status::NotFound;
    size_t slot_index = 0U;
    Slot* slot = find(id, &slot_index);
    if (slot == nullptr || !exposed(*slot)) return Status::NotFound;
    if (slot->info.state == WindowState::Minimized) return Status::InvalidState;
    size_t position = 0U;
    while (position < g_count && g_order[position] != slot_index) ++position;
    if (position == g_count) return Status::NotFound;
    for (size_t index = position + 1U; index < g_count; ++index) {
        g_order[index - 1U] = g_order[index];
    }
    g_order[g_count - 1U] = static_cast<uint8_t>(slot_index);
    g_focused = id;
    update_z_order();
    mark_full_dirty();
    return Status::Ok;
}

Status move(WindowId id, int32_t x, int32_t y) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr || !exposed(*slot)) return Status::NotFound;
    if (slot->info.state != WindowState::Normal || is_login_surface(*slot)) {
        return Status::InvalidState;
    }
    const WorkspaceGeometry workspace = calculate_workspace();
    slot->info.bounds.x = clamp_position(
        x,
        workspace.work_area.x,
        workspace.work_area.x + workspace.work_area.width - slot->info.bounds.width);
    slot->info.bounds.y = clamp_position(
        y,
        workspace.work_area.y,
        workspace.work_area.y + workspace.work_area.height - slot->info.bounds.height);
    slot->info.restore_bounds = slot->info.bounds;
    mark_full_dirty();
    return Status::Ok;
}

Status minimize(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr || !exposed(*slot)) return Status::NotFound;
    if (slot->info.state == WindowState::Minimized || is_login_surface(*slot)) {
        return Status::InvalidState;
    }
    slot->info.state = WindowState::Minimized;
    if (g_focused == id) choose_top_focus();
    mark_full_dirty();
    return Status::Ok;
}

Status maximize(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr || !exposed(*slot)) return Status::NotFound;
    if (slot->info.state == WindowState::Maximized || is_login_surface(*slot)) {
        return Status::InvalidState;
    }
    if (slot->info.state == WindowState::Normal) slot->info.restore_bounds = slot->info.bounds;
    const WorkspaceGeometry workspace = calculate_workspace();
    slot->info.state = WindowState::Maximized;
    slot->info.bounds = workspace.work_area;
    static_cast<void>(focus(id));
    mark_full_dirty();
    return Status::Ok;
}

Status restore(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr || !exposed(*slot)) return Status::NotFound;
    if (slot->info.state == WindowState::Normal || is_login_surface(*slot)) {
        return Status::InvalidState;
    }
    slot->info.state = WindowState::Normal;
    slot->info.bounds = slot->info.restore_bounds;
    static_cast<void>(focus(id));
    mark_full_dirty();
    return Status::Ok;
}

Status query(WindowId id, WindowInfo* out_info) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_info == nullptr) return Status::InvalidArgument;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    *out_info = slot->info;
    return Status::Ok;
}

Status set_content_cursor(WindowId id, CursorHint cursor) {
    if (!g_initialized) return Status::NotInitialized;
    if (cursor > CursorHint::NotAllowed) return Status::InvalidArgument;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    slot->cursor_hint = cursor;
#ifndef KUROGANE_HOST_TEST
    if (slot->info.focused) {
        move_cursor(input::pointer_x(), input::pointer_y());
    }
#endif
    return Status::Ok;
}

Status list(ListCallback callback, void* context) {
    if (!g_initialized) return Status::NotInitialized;
    if (callback == nullptr) return Status::InvalidArgument;
    for (size_t position = 0U; position < g_count; ++position) {
        if (!callback(g_slots[g_order[position]].info, context)) return Status::IterationStopped;
    }
    return Status::Ok;
}

WorkspaceGeometry workspace_geometry() { return calculate_workspace(); }

Status chrome_geometry(WindowId id, ChromeGeometry* out_geometry) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_geometry == nullptr) return Status::InvalidArgument;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    *out_geometry = calculate_chrome(slot->info.bounds);
    return Status::Ok;
}

Status blade_item_geometry(size_t position, ui::Rect* out_bounds) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_bounds == nullptr) return Status::InvalidArgument;
    if (position >= BLADE_PIN_COUNT) return Status::NotFound;
    *out_bounds = blade_item_rect(position);
    return Status::Ok;
}

Status pulse_item_geometry(size_t position, ui::Rect* out_bounds) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_bounds == nullptr) return Status::InvalidArgument;
    if (position >= exposed_window_count()) return Status::NotFound;
    *out_bounds = ribbon_item_rect(position);
    return Status::Ok;
}

Status desktop_pin(
    uint32_t app_id,
    uint32_t action,
    bool value,
    bool* out_pinned) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_pinned == nullptr || app_id >= DOCK_PIN_COUNT) {
        return Status::InvalidArgument;
    }
    if (app_id == 0U) {
        g_desktop_pinned[0U] = true;
        *out_pinned = true;
        return action <= DESKTOP_PIN_TOGGLE ? Status::Ok : Status::InvalidArgument;
    }
    switch (action) {
        case DESKTOP_PIN_QUERY:
            break;
        case DESKTOP_PIN_SET:
            g_desktop_pinned[app_id] = value;
            mark_full_dirty();
            break;
        case DESKTOP_PIN_TOGGLE:
            g_desktop_pinned[app_id] = !g_desktop_pinned[app_id];
            mark_full_dirty();
            break;
        default:
            return Status::InvalidArgument;
    }
    *out_pinned = g_desktop_pinned[app_id];
    return Status::Ok;
}

Status dispatch(const input::Event& event) {
    if (!g_initialized) return Status::NotInitialized;

    // Windows/Super key opens the persistent Red Flux application list.
    if (event.type == input::EventType::KeyDown &&
        (event.key == drivers::keyboard::KeyCode::LeftGui ||
         event.key == drivers::keyboard::KeyCode::RightGui)) {
        return login_surface() == nullptr
            ? activate_dock_pin(0U) : Status::InvalidState;
    }
    if (event.type == input::EventType::KeyDown && event.alt &&
        event.key == drivers::keyboard::KeyCode::F4) {
        return g_focused == INVALID_WINDOW ? Status::NotFound : close(g_focused);
    }
    if (event.type == input::EventType::KeyDown && event.alt &&
        event.key == drivers::keyboard::KeyCode::Tab) {
        return cycle_focus();
    }

    if (event.type == input::EventType::MouseButtonDown &&
        event.button == drivers::mouse::Left) {
        const WorkspaceGeometry workspace = calculate_workspace();
        if (login_surface() == nullptr &&
            rect_contains(workspace.signal_spine, event.x, event.y)) {
            for (size_t position = 0U; position < BLADE_PIN_COUNT; ++position) {
                if (rect_contains(blade_item_rect(position), event.x, event.y)) {
                    return activate_dock_pin(kBladePins[position]);
                }
            }
        }
        if (login_surface() == nullptr &&
            rect_contains(workspace.pulse_ribbon, event.x, event.y)) {
            for (size_t index = 0U; index < DOCK_PIN_COUNT; ++index) {
                if (rect_contains(dock_pin_rect(index), event.x, event.y)) {
                    return activate_dock_pin(index);
                }
            }
            const size_t tasks = exposed_window_count();
            for (size_t position = 0U; position < tasks; ++position) {
                if (rect_contains(ribbon_item_rect(position), event.x, event.y)) {
                    return activate_ribbon_item(position);
                }
            }
        }

        const WindowId target = hit_test(event.x, event.y);
        if (target == INVALID_WINDOW && login_surface() == nullptr) {
            for (size_t app = 0U; app < DOCK_PIN_COUNT; ++app) {
                if (g_desktop_pinned[app] &&
                    rect_contains(desktop_shortcut_rect(app), event.x, event.y)) {
                    return activate_dock_pin(app);
                }
            }
        }
        if (target != INVALID_WINDOW) {
            static_cast<void>(focus(target));
            Slot* slot = find(target);
            if (slot == nullptr) return Status::NotFound;
            if (!is_login_surface(*slot)) {
                const ChromeGeometry chrome = calculate_chrome(slot->info.bounds);
                if (rect_contains(chrome.dismiss_control, event.x, event.y)) return close(target);
                if (rect_contains(chrome.minimize_control, event.x, event.y)) return minimize(target);
                if (rect_contains(chrome.expand_control, event.x, event.y)) {
                    return slot->info.state == WindowState::Maximized ? restore(target) : maximize(target);
                }
                if (slot->info.state == WindowState::Normal &&
                    rect_contains(chrome.resize_grip, event.x, event.y)) {
                    g_resized = target;
                    g_dragged = INVALID_WINDOW;
                    return Status::Ok;
                }
                if (slot->info.state == WindowState::Normal && title_hit(*slot, event.x, event.y)) {
                    g_dragged = target;
                    g_resized = INVALID_WINDOW;
                    g_drag_offset_x = event.x - slot->info.bounds.x;
                    g_drag_offset_y = event.y - slot->info.bounds.y;
                }
            }
        }
    } else if (event.type == input::EventType::MouseMove &&
               (event.buttons & drivers::mouse::Left) != 0U) {
        if (g_resized != INVALID_WINDOW) {
            Slot* slot = find(g_resized);
            if (slot != nullptr) resize_window(*slot, event.x, event.y);
        } else if (g_dragged != INVALID_WINDOW) {
            static_cast<void>(move(
                g_dragged,
                event.x - g_drag_offset_x,
                event.y - g_drag_offset_y));
        }
    } else if (event.type == input::EventType::MouseButtonUp &&
               event.button == drivers::mouse::Left) {
        g_dragged = INVALID_WINDOW;
        g_resized = INVALID_WINDOW;
    }

#ifndef KUROGANE_HOST_TEST
    if (event.type == input::EventType::MouseMove) move_cursor(event.x, event.y);
#endif

    Slot* focused = find(g_focused);
    if (focused != nullptr && focused->input_callback != nullptr) {
        focused->input_callback(focused->info.id, event, focused->context);
    }
    return Status::Ok;
}

void invalidate() {
    mark_full_dirty();
}

bool render_if_needed() {
    if (!g_initialized || g_dirty == DirtyMode::None) return false;
#ifndef KUROGANE_HOST_TEST
    hide_cursor();
    const bool buffered = graphics::begin_frame();
    render_layers();
    if (buffered) graphics::end_frame();
    const int32_t cursor_x = input::pointer_x();
    const int32_t cursor_y = input::pointer_y();
    show_cursor(cursor_x, cursor_y, cursor_for_position(cursor_x, cursor_y));
#endif
    g_dirty = DirtyMode::None;
    return true;
}

size_t window_count() { return g_count; }
WindowId focused_window() { return g_focused; }
bool initialized() { return g_initialized; }

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "window manager not initialized";
        case Status::InvalidArgument: return "invalid window argument";
        case Status::NotFound: return "window not found";
        case Status::CapacityReached: return "window table full";
        case Status::InvalidState: return "invalid window state";
        case Status::IterationStopped: return "window iteration stopped";
    }
    return "unknown window status";
}

} // namespace windowing
