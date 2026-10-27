#include "window_manager.hpp"
#include "../../sdk/include/kurogane/flux_app_catalog.h"

#ifndef KUROGANE_HOST_TEST
#include "../drivers/framebuffer.hpp"
#include "../task/process.hpp"
#include "cursor_sprite.hpp"
#include "session_owner_policy.hpp"
#include "../drivers/serial.hpp"
#include "../libk/format.hpp"
#endif

namespace windowing {
namespace {

constexpr int32_t HEADER_HEIGHT = 36;
constexpr int32_t MINIMUM_WIDTH = 260;
constexpr int32_t MINIMUM_HEIGHT = 160;
constexpr int32_t WORKSPACE_LEFT = 34;
constexpr int32_t WORKSPACE_TOP = 50;
constexpr int32_t WORKSPACE_RIGHT = 12;
constexpr int32_t DOCK_HEIGHT = 58;
constexpr int32_t DOCK_BOTTOM = 12;
// A floating dock is an overlay. Only a slim bottom safety margin belongs
// to the maximized window work area, never the full dock height.
constexpr int32_t WORKSPACE_BOTTOM = 18;
constexpr int32_t SPINE_X = 8;
constexpr int32_t SPINE_WIDTH = 18;
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
constexpr int32_t RIBBON_ITEM_MAX = 96;
constexpr int32_t RIBBON_ITEM_MIN = 48;
constexpr size_t DOCK_PIN_COUNT = 8U;
constexpr uint32_t DESKTOP_PIN_QUERY = 0U;
constexpr uint32_t DESKTOP_PIN_SET = 1U;
constexpr uint32_t DESKTOP_PIN_TOGGLE = 2U;
constexpr uint32_t DESKTOP_PIN_HOME_READY = 3U;
constexpr uint32_t DESKTOP_PIN_FOCUS_RUNNING = 4U;
constexpr uint32_t DESKTOP_PIN_APP_READY = 5U;

enum class DirtyMode : uint8_t {
    None = 0,
    Regions,
    Full,
};

struct SurfaceState {
    size_t width;
    size_t height;
    size_t stride;
    size_t size;
    bool valid;
    uint8_t payload[MAX_SURFACE_PAYLOAD_BYTES];
};

struct Slot {
    WindowInfo info;
    SurfaceState surface;
    DrawCallback draw;
    InputCallback input_callback;
    void* context;
    uint16_t generation;
    bool occupied;
    WindowState state_before_minimize;
};

struct DockPin {
    const char* title;
    char command;
    ui::DockIcon icon;
    const char* shortcut_label;
};

// Order is the public ku_desktop_app_id ABI.
constexpr DockPin kDockPins[DOCK_PIN_COUNT] = {
    {KU_FLUX_TITLE_HOME, 0, ui::DockIcon::Home, "HOME"},
    {KU_FLUX_TITLE_TERMINAL, 't', ui::DockIcon::Terminal, "TERM"},
    {KU_FLUX_TITLE_FILES, 'f', ui::DockIcon::Files, "FILES"},
    {KU_FLUX_TITLE_PERFORMANCE, 'v', ui::DockIcon::Monitor, "CTRL"},
    {KU_FLUX_TITLE_BROWSER, 'b', ui::DockIcon::Files, "WEB"},
    {KU_FLUX_TITLE_MONITOR, 'm', ui::DockIcon::Monitor, "MON"},
    {KU_FLUX_TITLE_SETTINGS, 's', ui::DockIcon::Settings, "SET"},
    {KU_FLUX_TITLE_ABOUT, 'a', ui::DockIcon::About, "ABOUT"},
};

Slot g_slots[MAX_WINDOWS]{};
uint8_t g_order[MAX_WINDOWS]{};
bool g_desktop_pinned[DOCK_PIN_COUNT]{};
size_t g_count = 0U;
int32_t g_screen_width = 0;
int32_t g_screen_height = 0;
WindowId g_focused = INVALID_WINDOW;
WindowId g_dragged = INVALID_WINDOW;
WindowId g_resized = INVALID_WINDOW;
WindowId g_hovered = INVALID_WINDOW;
// Application-level left-button capture is distinct from window-chrome drag.
WindowId g_pointer_capture = INVALID_WINDOW;
int32_t g_drag_offset_x = 0;
int32_t g_drag_offset_y = 0;
DirtyMode g_dirty = DirtyMode::None;
ui::Rect g_damage_regions[MAX_DAMAGE_REGIONS]{};
size_t g_damage_count = 0U;
bool g_initialized = false;
bool g_rendering = false;
// Mouse proximity reveals the floating dock; moving away collapses it into
// a narrow bottom-edge handle. This state never changes the work-area size.
bool g_dock_visible = true;
#ifndef KUROGANE_HOST_TEST
process::ProcessId g_session_root_pid = process::INVALID_PROCESS_ID;
#endif

#ifndef KUROGANE_HOST_TEST
bool g_cursor_visible = false;
int32_t g_cursor_x = 0;
int32_t g_cursor_y = 0;
uint32_t g_cursor_under[flux_cursor::kWidth * flux_cursor::kHeight]{};
bool g_cursor_saved[flux_cursor::kWidth * flux_cursor::kHeight]{};
#endif

void clear_surface(SurfaceState& surface) {
    for (size_t index = 0U; index < MAX_SURFACE_PAYLOAD_BYTES; ++index) {
        surface.payload[index] = 0U;
    }
    surface.width = 0U;
    surface.height = 0U;
    surface.stride = 0U;
    surface.size = 0U;
    surface.valid = false;
}

void release_slot(Slot& slot) {
    clear_surface(slot.surface);
    slot.info = {};
    slot.draw = nullptr;
    slot.input_callback = nullptr;
    slot.context = nullptr;
    slot.occupied = false;
}

void mark_full_dirty() {
    g_dirty = DirtyMode::Full;
    g_damage_count = 0U;
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

// Observe completed lifecycle changes, never fabricate input-test success.
// This runs in the serialized compositor context and does not expose titles,
// application contents or credentials. app is diagnostic metadata only.
void trace_transition(const char* action, const WindowInfo& info) {
#ifndef KUROGANE_HOST_TEST
    if (info.owner_pid == 0U) return;
    unsigned app = static_cast<unsigned>(DOCK_PIN_COUNT);
    for (size_t index = 0U; index < DOCK_PIN_COUNT; ++index) {
        if (text_equals(info.title, kDockPins[index].title)) {
            app = static_cast<unsigned>(index);
            break;
        }
    }
    ResourceSnapshot resources{};
    if (resource_snapshot(&resources) != Status::Ok) return;
    char line[384];
    const int length = k_snprintf(line, sizeof(line),
        "[WM] action=%s id=%u pid=%llu app=%u state=%u x=%d y=%d w=%d h=%d "
        "focus=%u windows=%zu surfaces=%zu bytes=%zu sw=%d sh=%d\n",
        action, static_cast<unsigned>(info.id),
        static_cast<unsigned long long>(info.owner_pid), app,
        static_cast<unsigned>(info.state), info.bounds.x, info.bounds.y,
        info.bounds.width, info.bounds.height, static_cast<unsigned>(g_focused),
        resources.windows, resources.retained_surfaces, resources.retained_bytes,
        g_screen_width, g_screen_height);
    if (length > 0 && static_cast<size_t>(length) < sizeof(line)) serial::write(line);
#else
    (void)action;
    (void)info;
#endif
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

// Draw callbacks are allowed to change window lifecycle and z-order.
// Capture the generation-safe IDs so this frame never skips a survivor,
// repeats a window or draws a newly reused slot in place of a closed one.
size_t snapshot_frame_windows(WindowId (&ids)[MAX_WINDOWS]) {
    const size_t count = g_count;
    for (size_t position = 0U; position < count; ++position) {
        ids[position] = g_slots[g_order[position]].info.id;
    }
    return count;
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
    return text_equals(title, "RED FLUX HOME");
}

bool is_performance_surface(const char* title) {
    return text_equals(title, "FORGE CONTROL") ||
        text_equals(title, "CONTROL CENTER");
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


ui::Rect clip_damage_region(const ui::Rect& input) {
    if (input.width <= 0 || input.height <= 0 ||
        g_screen_width <= 0 || g_screen_height <= 0) return {};
    int64_t left = input.x;
    int64_t top = input.y;
    int64_t right = static_cast<int64_t>(input.x) + input.width;
    int64_t bottom = static_cast<int64_t>(input.y) + input.height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > g_screen_width) right = g_screen_width;
    if (bottom > g_screen_height) bottom = g_screen_height;
    if (left >= right || top >= bottom) return {};
    return {
        static_cast<int32_t>(left),
        static_cast<int32_t>(top),
        static_cast<int32_t>(right - left),
        static_cast<int32_t>(bottom - top),
    };
}

bool damage_regions_touch(const ui::Rect& left, const ui::Rect& right) {
    const int64_t left_right = static_cast<int64_t>(left.x) + left.width;
    const int64_t left_bottom = static_cast<int64_t>(left.y) + left.height;
    const int64_t right_right = static_cast<int64_t>(right.x) + right.width;
    const int64_t right_bottom = static_cast<int64_t>(right.y) + right.height;
    return static_cast<int64_t>(left.x) <= right_right &&
        static_cast<int64_t>(right.x) <= left_right &&
        static_cast<int64_t>(left.y) <= right_bottom &&
        static_cast<int64_t>(right.y) <= left_bottom;
}

ui::Rect unite_damage_regions(const ui::Rect& left, const ui::Rect& right) {
    const int64_t x1 = left.x < right.x ? left.x : right.x;
    const int64_t y1 = left.y < right.y ? left.y : right.y;
    const int64_t left_right = static_cast<int64_t>(left.x) + left.width;
    const int64_t right_right = static_cast<int64_t>(right.x) + right.width;
    const int64_t left_bottom = static_cast<int64_t>(left.y) + left.height;
    const int64_t right_bottom = static_cast<int64_t>(right.y) + right.height;
    const int64_t x2 = left_right > right_right ? left_right : right_right;
    const int64_t y2 = left_bottom > right_bottom ? left_bottom : right_bottom;
    return {
        static_cast<int32_t>(x1), static_cast<int32_t>(y1),
        static_cast<int32_t>(x2 - x1), static_cast<int32_t>(y2 - y1),
    };
}

void add_damage_region(const ui::Rect& requested) {
    if (!g_initialized || g_dirty == DirtyMode::Full) return;
    ui::Rect region = clip_damage_region(requested);
    if (region.width <= 0 || region.height <= 0) return;

    size_t index = 0U;
    while (index < g_damage_count) {
        if (!damage_regions_touch(region, g_damage_regions[index])) {
            ++index;
            continue;
        }
        region = unite_damage_regions(region, g_damage_regions[index]);
        for (size_t move = index + 1U; move < g_damage_count; ++move) {
            g_damage_regions[move - 1U] = g_damage_regions[move];
        }
        --g_damage_count;
        index = 0U;
    }

    if (g_damage_count >= MAX_DAMAGE_REGIONS) {
        mark_full_dirty();
        return;
    }
    g_damage_regions[g_damage_count++] = region;
    g_dirty = DirtyMode::Regions;
}

int32_t pinned_section_width() {
    return DOCK_HOME_WIDTH +
        static_cast<int32_t>(DOCK_PIN_COUNT - 1U) * DOCK_PIN_SIZE +
        static_cast<int32_t>(DOCK_PIN_COUNT - 1U) * DOCK_PIN_GAP;
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

    const size_t tasks = exposed_window_count();
    const int32_t dock_max_width = g_screen_width - 48;
    const int32_t fixed_width = DOCK_PADDING * 2 + pinned_section_width() +
        DOCK_SEPARATOR;
    int32_t requested_width = fixed_width;
    if (tasks != 0U) {
        requested_width += static_cast<int32_t>(tasks) * RIBBON_ITEM_MAX +
            static_cast<int32_t>(tasks - 1U) * RIBBON_GAP;
    }
    const int32_t minimum_width = fixed_width < dock_max_width
        ? fixed_width : dock_max_width;
    const int32_t dock_width = clamp_size(
        requested_width, minimum_width, dock_max_width);
    geometry.pulse_ribbon = {
        (g_screen_width - dock_width) / 2,
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

ui::Rect calculate_content(const Slot& slot) {
    if (is_login_surface(slot)) return slot.info.bounds;
    if (is_home_surface(slot.info.title)) {
        return {
            slot.info.bounds.x + 8, slot.info.bounds.y + 8,
            slot.info.bounds.width - 16, slot.info.bounds.height - 16,
        };
    }
    const ui::Rect& bounds = slot.info.bounds;
    return {
        bounds.x + 4,
        bounds.y + HEADER_HEIGHT,
        bounds.width - 8,
        bounds.height - HEADER_HEIGHT - 4,
    };
}

ui::Rect dock_pin_rect(size_t position) {
    const WorkspaceGeometry workspace = calculate_workspace();
    if (position >= DOCK_PIN_COUNT) return {};
    const int32_t base_x = workspace.pulse_ribbon.x + DOCK_PADDING;
    if (position == 0U) {
        return {
            base_x,
            workspace.pulse_ribbon.y + (DOCK_HEIGHT - DOCK_PIN_SIZE) / 2,
            DOCK_HOME_WIDTH,
            DOCK_PIN_SIZE,
        };
    }
    return {
        base_x + DOCK_HOME_WIDTH + DOCK_PIN_GAP +
            static_cast<int32_t>(position - 1U) * (DOCK_PIN_SIZE + DOCK_PIN_GAP),
        workspace.pulse_ribbon.y + (DOCK_HEIGHT - DOCK_PIN_SIZE) / 2,
        DOCK_PIN_SIZE,
        DOCK_PIN_SIZE,
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
    if (!is_performance_surface(title)) return requested;
    const WorkspaceGeometry workspace = calculate_workspace();
    if (workspace.work_area.width < 320 || workspace.work_area.height < 300) {
        return requested;
    }
    int32_t width = workspace.work_area.width - 36;
    int32_t height = workspace.work_area.height - 12;
    if (width > 580) width = 580;
    if (height > 500) height = 500;
    if (width < 360) width = 360;
    if (height < 420) height = 420;
    if (width > workspace.work_area.width) width = workspace.work_area.width;
    if (height > workspace.work_area.height) height = workspace.work_area.height;
    return {
        workspace.work_area.x + (workspace.work_area.width - width) / 2,
        workspace.work_area.y + (workspace.work_area.height - height) / 2,
        width,
        height,
    };
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
        release_slot(slot);
    }
    g_count = write_position;
    g_focused = INVALID_WINDOW;
    g_dragged = INVALID_WINDOW;
    g_resized = INVALID_WINDOW;
    g_hovered = INVALID_WINDOW;
    g_pointer_capture = INVALID_WINDOW;
    update_z_order();
    mark_full_dirty();
}
#endif

WindowId hit_test(int32_t x, int32_t y);

void cancel_capture(WindowId id) {
    if (g_dragged == id) g_dragged = INVALID_WINDOW;
    if (g_resized == id) g_resized = INVALID_WINDOW;
    if (g_pointer_capture == id) g_pointer_capture = INVALID_WINDOW;
}

void clear_hover(WindowId id) {
    if (g_hovered == id) g_hovered = INVALID_WINDOW;
}

void update_pointer_feedback(const input::Event& event) {
    if (event.type != input::EventType::MouseMove &&
        event.type != input::EventType::MouseButtonDown &&
        event.type != input::EventType::MouseButtonUp) return;
    const WindowId previous = g_hovered;
    const WindowId next = hit_test(event.x, event.y);
    if (previous != INVALID_WINDOW && previous != next) {
        invalidate_window(previous);
    }
    if (next != INVALID_WINDOW) {
        // Repaint even within one window: the pointer may have crossed
        // native widget geometry while the owning window stayed the same.
        invalidate_window(next);
    }
    g_hovered = next;
}

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

#ifndef KUROGANE_HOST_TEST
size_t focused_position() {
    size_t exposed_position = 0U;
    for (size_t position = 0U; position < g_count; ++position) {
        const Slot& slot = g_slots[g_order[position]];
        if (!exposed(slot) || is_home_surface(slot.info.title)) continue;
        if (slot.info.id == g_focused) return exposed_position;
        ++exposed_position;
    }
    return exposed_window_count();
}
#endif

bool title_hit(const Slot& slot, int32_t x, int32_t y) {
    if (is_login_surface(slot) || is_home_surface(slot.info.title)) return false;
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
        // APPS is the session rescue entry point. Never hide an already
        // focused Home surface here: the launcher is also the command
        // broker for all pinned native application tiles.
        return focus(existing->info.id);
    }
    if (pin.command == 0) return Status::NotFound;

    Slot* launcher = find_by_title("RED FLUX HOME");
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
        maximum_width < MINIMUM_WIDTH ? maximum_width : MINIMUM_WIDTH,
        maximum_width);
    slot.info.bounds.height = clamp_size(
        pointer_y - slot.info.bounds.y + 1,
        maximum_height < MINIMUM_HEIGHT ? maximum_height : MINIMUM_HEIGHT,
        maximum_height);
    slot.info.restore_bounds = slot.info.bounds;
    mark_full_dirty();
}

#ifndef KUROGANE_HOST_TEST
uint32_t cursor_native_color(flux_cursor::Ink ink) {
    uint32_t rgb = UINT32_C(0xF5F7FA);
    switch (ink) {
        case flux_cursor::Ink::Outline: rgb = UINT32_C(0x14171D); break;
        case flux_cursor::Ink::Edge: rgb = UINT32_C(0xD0D7E2); break;
        case flux_cursor::Ink::Shadow: rgb = UINT32_C(0x535B68); break;
        case flux_cursor::Ink::Fill:
        case flux_cursor::Ink::Transparent: break;
    }
    if (graphics::info().pixel_format == KUROGANE_PIXEL_RGBX8) {
        return ((rgb & 0x0000FFU) << 16U) |
               (rgb & 0x00FF00U) |
               ((rgb & 0xFF0000U) >> 16U);
    }
    return rgb;
}

void hide_cursor() {
    if (!g_cursor_visible || !graphics::available()) return;
    const KuroganeFramebuffer& fb = graphics::info();
    flux_cursor::visit_visible(g_cursor_x, g_cursor_y, fb.width, fb.height,
        [&](uint32_t index, int32_t x, int32_t y, flux_cursor::Ink) {
            if (!g_cursor_saved[index]) return;
            auto* row = reinterpret_cast<uint32_t*>(
                reinterpret_cast<uint8_t*>(fb.base) +
                static_cast<size_t>(y) * fb.pitch);
            row[x] = g_cursor_under[index];
            g_cursor_saved[index] = false;
        });
    g_cursor_visible = false;
}

void show_cursor(int32_t x, int32_t y) {
    if (g_cursor_visible) hide_cursor();
    if (!graphics::available()) return;
    const KuroganeFramebuffer& fb = graphics::info();
    g_cursor_x = x;
    g_cursor_y = y;
    flux_cursor::visit_visible(x, y, fb.width, fb.height,
        [&](uint32_t index, int32_t px, int32_t py, flux_cursor::Ink ink) {
            auto* row = reinterpret_cast<uint32_t*>(
                reinterpret_cast<uint8_t*>(fb.base) +
                static_cast<size_t>(py) * fb.pitch);
            g_cursor_under[index] = row[px];
            g_cursor_saved[index] = true;
            row[px] = cursor_native_color(ink);
        });
    g_cursor_visible = true;
}

void move_cursor(int32_t x, int32_t y) {
    if (g_cursor_visible && x == g_cursor_x && y == g_cursor_y) return;
    hide_cursor();
    show_cursor(x, y);
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

    if (is_home_surface(slot.info.title)) {
        ui::panel(bounds, true);
        if (slot.draw != nullptr) {
            const ui::Rect content = calculate_content(slot);
            graphics::set_clip(content.x, content.y, content.width, content.height);
            graphics::set_text_scale_limit(1U);
            slot.draw(slot.info.id, content, true, slot.context);
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
        const ui::Rect content = calculate_content(slot);
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
        ui::login_backdrop("LOCAL SESSION / MOUSE READY");
        draw_window_slot(*login);
        return;
    }

    ui::desktop("KUROGANE / RED FLUX");
    // Do not imply a healthy session when neither Login nor its Home app is
    // present. This is deliberately shown only in the failure state, so
    // users can distinguish a missing Ring-3 launcher from a dead pointer.
    if (find_by_title("RED FLUX HOME") == nullptr) {
        const ui::Theme& colors = ui::default_theme();
        graphics::draw_text(
            54, g_screen_height / 2 + 170, "SESSION HOME NOT READY",
            colors.text, colors.desktop, 2U, true);
        graphics::draw_text(
            54, g_screen_height / 2 + 194, "CHECK COM1 LOG / FLUX DIAGNOSTIC",
            colors.text_muted, colors.desktop, 1U, true);
    }
    const WorkspaceGeometry workspace = calculate_workspace();
    const size_t tasks = exposed_window_count();
    ui::signal_spine(workspace.signal_spine, tasks, focused_position());

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

    WindowId frame_windows[MAX_WINDOWS]{};
    const size_t frame_count = snapshot_frame_windows(frame_windows);
    for (size_t position = 0U; position < frame_count; ++position) {
        // A callback can close a window and immediately reuse its slot.
        // Matching the original ID prevents drawing the replacement early.
        Slot* slot = find(frame_windows[position]);
        if (slot != nullptr) draw_window_slot(*slot);
    }

    if (g_dock_visible) {
        ui::dock_bar(workspace.pulse_ribbon, tasks);
        for (size_t index = 0U; index < DOCK_PIN_COUNT; ++index) {
            const Slot* running = find_by_title(kDockPins[index].title);
            const bool active = running != nullptr && running->info.focused;
            if (index == 0U) {
                // HOME is the session application broker: APPS always focuses
                // or restores it, never hides it when already focused.
                ui::button(dock_pin_rect(index), "APPS", active);
            } else {
                ui::dock_item(
                    dock_pin_rect(index), kDockPins[index].icon,
                    running != nullptr, active);
            }
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
    } else {
        // A tiny centered grabber replaces the full dock when the mouse is
        // elsewhere. No opaque 1/10-screen footer or permanent reservation.
        const ui::Theme& colors = ui::default_theme();
        graphics::fill_rounded_rect(
            g_screen_width / 2 - 26, g_screen_height - 5,
            52, 3, 1U, colors.text_muted);
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
    g_hovered = INVALID_WINDOW;
    g_pointer_capture = INVALID_WINDOW;
    g_dock_visible = true;
#ifndef KUROGANE_HOST_TEST
    g_session_root_pid = process::INVALID_PROCESS_ID;
    g_cursor_visible = false;
    for (size_t i = 0U; i < flux_cursor::kWidth * flux_cursor::kHeight; ++i)
        g_cursor_saved[i] = false;
    g_cursor_x = input::pointer_x();
    g_cursor_y = input::pointer_y();
#endif
    g_initialized = true;
    mark_full_dirty();
    return Status::Ok;
}

Status set_screen_size(uint32_t screen_width, uint32_t screen_height) {
    if (!g_initialized) return Status::NotInitialized;
    if (g_rendering) return Status::InvalidState;
    if (screen_width < 320U || screen_height < 200U ||
        screen_width > static_cast<uint32_t>(INT32_MAX) ||
        screen_height > static_cast<uint32_t>(INT32_MAX)) return Status::InvalidArgument;
    if (screen_width == static_cast<uint32_t>(g_screen_width) &&
        screen_height == static_cast<uint32_t>(g_screen_height)) return Status::Ok;
#ifndef KUROGANE_HOST_TEST
    if (graphics::width() != screen_width || graphics::height() != screen_height ||
        !input::set_screen_bounds(screen_width, screen_height)) return Status::InvalidState;
    // The old cursor backup belongs to the previous display geometry. Do not
    // restore it onto a new framebuffer; the next full frame will rebuild it.
    g_cursor_visible = false;
    for (size_t i = 0U; i < flux_cursor::kWidth * flux_cursor::kHeight; ++i)
        g_cursor_saved[i] = false;
#endif
    g_screen_width = static_cast<int32_t>(screen_width);
    g_screen_height = static_cast<int32_t>(screen_height);
    const ui::Rect area = calculate_workspace().work_area;
    const auto fit = [&area](ui::Rect bounds) {
        if (bounds.width > area.width) bounds.width = area.width;
        if (bounds.height > area.height) bounds.height = area.height;
        bounds.x = clamp_position(bounds.x, area.x, area.x + area.width - bounds.width);
        bounds.y = clamp_position(bounds.y, area.y, area.y + area.height - bounds.height);
        return bounds;
    };
    for (Slot& slot : g_slots) {
        if (!slot.occupied) continue;
        if (is_login_surface(slot)) {
            slot.info.bounds = {0, 0, g_screen_width, g_screen_height};
            slot.info.restore_bounds = slot.info.bounds;
        } else {
            slot.info.restore_bounds = fit(slot.info.restore_bounds);
            const bool maximized = slot.info.state == WindowState::Maximized ||
                (slot.info.state == WindowState::Minimized &&
                 slot.state_before_minimize == WindowState::Maximized);
            slot.info.bounds = maximized ? area : fit(slot.info.bounds);
        }
    }
    g_dragged = g_resized = g_hovered = g_pointer_capture = INVALID_WINDOW;
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

#ifndef KUROGANE_HOST_TEST
    const bool home = is_home_surface(title);
    const bool login = text_equals(title, "KUROGANE LOGIN");
    if (login || home) {
        process::Stat caller{};
        if (process::stat(owner_pid, &caller) != process::Status::Ok) {
            return Status::InvalidState;
        }
        if (login) {
            // Otherwise any Ring-3 process could name its window LOGIN and
            // trigger purge_exposed_session() against the real user.
            if (!session_owner_policy::login_allowed(
                    owner_pid, caller.parent_pid)) return Status::InvalidState;
            purge_exposed_session();
            g_session_root_pid = process::INVALID_PROCESS_ID;
        } else {
            const Slot* authenticated_login = login_surface();
            if (authenticated_login == nullptr ||
                find_by_title("RED FLUX HOME") != nullptr ||
                !session_owner_policy::home_allowed(
                    owner_pid, caller.parent_pid,
                    authenticated_login->info.owner_pid)) {
                return Status::InvalidState;
            }
        }
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
    slot.info.state = owner_pid == 0U
        ? WindowState::Minimized : WindowState::Normal;
    copy_title(slot.info.title, title);
    slot.draw = draw;
    slot.input_callback = input_callback;
    slot.context = context;
    clear_surface(slot.surface);
    g_order[g_count++] = static_cast<uint8_t>(selected);
#ifndef KUROGANE_HOST_TEST
    // Commit the session owner only after the Home window really exists.
    if (home) g_session_root_pid = owner_pid;
#endif
    if (owner_pid != 0U) g_focused = slot.info.id;
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

    const WindowInfo closed_info = slot->info;
    size_t position = 0U;
    while (position < g_count && g_order[position] != slot_index) ++position;
    for (size_t index = position + 1U; index < g_count; ++index) {
        g_order[index - 1U] = g_order[index];
    }
    --g_count;
    clear_hover(id);
    release_slot(*slot);
    cancel_capture(id);
    choose_top_focus();
    mark_full_dirty();
    trace_transition("close", closed_info);
    return Status::Ok;
}

Status close_owned_windows(uint64_t owner_pid, size_t* out_closed) {
    if (!g_initialized) return Status::NotInitialized;
    if (owner_pid == 0U) return Status::InvalidArgument;
    if (out_closed != nullptr) *out_closed = 0U;

    WindowId owned[MAX_WINDOWS]{};
    size_t owned_count = 0U;
    for (const Slot& slot : g_slots) {
        if (!slot.occupied || slot.info.owner_pid != owner_pid) continue;
        owned[owned_count++] = slot.info.id;
    }

    size_t closed = 0U;
    for (size_t index = 0U; index < owned_count; ++index) {
        const Status status = close(owned[index]);
        if (status != Status::Ok) {
            if (out_closed != nullptr) *out_closed = closed;
            return status;
        }
        ++closed;
    }
    if (out_closed != nullptr) *out_closed = closed;
    return Status::Ok;
}

Status dismiss(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    if (!is_home_surface(slot->info.title)) return close(id);
    if (slot->info.state == WindowState::Minimized) return Status::Ok;
    return minimize(id);
}

Status present_surface(
    WindowId id,
    size_t width,
    size_t height,
    size_t stride,
    const void* payload,
    size_t payload_size) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    if (payload == nullptr || width == 0U || height == 0U || stride == 0U ||
        width > stride) {
        return Status::InvalidArgument;
    }
    const size_t maximum_size = static_cast<size_t>(-1);
    if (stride > maximum_size / height) return Status::ArithmeticOverflow;
    const size_t required = stride * height;
    if (required != payload_size) return Status::InvalidArgument;
    if (required > MAX_SURFACE_PAYLOAD_BYTES) return Status::PayloadTooLarge;

    const auto* source_bytes = static_cast<const uint8_t*>(payload);
    for (size_t index = 0U; index < required; ++index) {
        slot->surface.payload[index] = source_bytes[index];
    }
    for (size_t index = required; index < slot->surface.size; ++index) {
        slot->surface.payload[index] = 0U;
    }
    const bool first_frame = !slot->surface.valid;
    slot->surface.width = width;
    slot->surface.height = height;
    slot->surface.stride = stride;
    slot->surface.size = required;
    slot->surface.valid = true;
    invalidate_window(id);
    if (first_frame) trace_transition("present", slot->info);
    return Status::Ok;
}

Status read_surface(WindowId id, SurfaceView* out_surface) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_surface == nullptr) return Status::InvalidArgument;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    if (!slot->surface.valid) return Status::InvalidState;
    out_surface->width = slot->surface.width;
    out_surface->height = slot->surface.height;
    out_surface->stride = slot->surface.stride;
    out_surface->data = slot->surface.payload;
    out_surface->size = slot->surface.size;
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
    // Clicking a widget in the already-focused topmost window does not change
    // z-order or chrome. Keep this path idempotent so transient pressed/hover
    // feedback can remain a bounded window-region repaint instead of forcing
    // a full desktop composition.
    if (g_focused == id && position + 1U == g_count) return Status::Ok;
    for (size_t index = position + 1U; index < g_count; ++index) {
        g_order[index - 1U] = g_order[index];
    }
    g_order[g_count - 1U] = static_cast<uint8_t>(slot_index);
    g_focused = id;
    update_z_order();
    mark_full_dirty();
    trace_transition("focus", slot->info);
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
    cancel_capture(id);
    clear_hover(id);
    slot->state_before_minimize = slot->info.state;
    slot->info.state = WindowState::Minimized;
    if (g_focused == id) choose_top_focus();
    mark_full_dirty();
    trace_transition("minimize", slot->info);
    return Status::Ok;
}

Status maximize(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr || !exposed(*slot)) return Status::NotFound;
    if (slot->info.state == WindowState::Maximized || is_login_surface(*slot)) {
        return Status::InvalidState;
    }
    cancel_capture(id);
    if (slot->info.state == WindowState::Normal) slot->info.restore_bounds = slot->info.bounds;
    const WorkspaceGeometry workspace = calculate_workspace();
    slot->info.state = WindowState::Maximized;
    slot->info.bounds = workspace.work_area;
    static_cast<void>(focus(id));
    mark_full_dirty();
    trace_transition("maximize", slot->info);
    return Status::Ok;
}

Status restore(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr || !exposed(*slot)) return Status::NotFound;
    if (slot->info.state == WindowState::Normal || is_login_surface(*slot)) {
        return Status::InvalidState;
    }
    cancel_capture(id);
    const bool restore_maximized = slot->info.state == WindowState::Minimized &&
        slot->state_before_minimize == WindowState::Maximized;
    slot->info.state = restore_maximized ? WindowState::Maximized : WindowState::Normal;
    slot->info.bounds = restore_maximized
        ? calculate_workspace().work_area : slot->info.restore_bounds;
    static_cast<void>(focus(id));
    mark_full_dirty();
    trace_transition("restore", slot->info);
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

Status list(ListCallback callback, void* context) {
    if (!g_initialized) return Status::NotInitialized;
    if (callback == nullptr) return Status::InvalidArgument;

    // A callback may close, create or focus windows, mutating g_order and
    // reclaiming a slot. Snapshot metadata before calling it. Generation-safe
    // lookup excludes closed windows and avoids passing a replacement as if
    // it were the former occupant; new windows appear on the next listing.
    WindowInfo snapshot[MAX_WINDOWS]{};
    const size_t count = g_count;
    for (size_t position = 0U; position < count; ++position) {
        snapshot[position] = g_slots[g_order[position]].info;
    }
    for (size_t position = 0U; position < count; ++position) {
        if (find(snapshot[position].id) == nullptr) continue;
        if (!callback(snapshot[position], context)) return Status::IterationStopped;
    }
    return Status::Ok;
}

WorkspaceGeometry workspace_geometry() { return calculate_workspace(); }

bool home_surface_ready() {
    if (!g_initialized) return false;
    const Slot* home = find_by_title("RED FLUX HOME");
    return home != nullptr && home->occupied &&
        home->info.state != WindowState::Minimized &&
        home->surface.valid && home->draw != nullptr;
}
bool dock_visible() { return g_dock_visible; }

Status chrome_geometry(WindowId id, ChromeGeometry* out_geometry) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_geometry == nullptr) return Status::InvalidArgument;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    *out_geometry = calculate_chrome(slot->info.bounds);
    return Status::Ok;
}

Status content_geometry(WindowId id, ui::Rect* out_geometry) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_geometry == nullptr) return Status::InvalidArgument;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    *out_geometry = calculate_content(*slot);
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
    if (action == DESKTOP_PIN_HOME_READY) {
        if (app_id != 0U) return Status::InvalidArgument;
        *out_pinned = home_surface_ready();
        return Status::Ok;
    }
    if (action == DESKTOP_PIN_APP_READY) {
        const Slot* existing = find_by_title(kDockPins[app_id].title);
        *out_pinned = existing != nullptr && existing->surface.valid;
        return Status::Ok;
    }
    if (action == DESKTOP_PIN_FOCUS_RUNNING) {
        // The caller's Home-process identity is checked at the syscall
        // boundary. This action never sends a synthetic command back to
        // itself and cannot spawn a process.
        Slot* existing = find_by_title(kDockPins[app_id].title);
        if (existing == nullptr) return Status::NotFound;
        *out_pinned = g_desktop_pinned[app_id];
        return existing->info.state == WindowState::Minimized
            ? restore(existing->info.id) : focus(existing->info.id);
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

    const bool primary_release =
        event.type == input::EventType::MouseButtonUp &&
        event.button == drivers::mouse::Left;
    const bool pointer_event =
        event.type == input::EventType::MouseMove ||
        event.type == input::EventType::MouseButtonDown ||
        event.type == input::EventType::MouseButtonUp ||
        event.type == input::EventType::MouseWheel;
    // A chrome drag/resize never becomes an application release.
    const bool system_release = primary_release &&
        (g_dragged != INVALID_WINDOW || g_resized != INVALID_WINDOW);

    // Flux rescue shortcuts are intentionally chorded to avoid intercepting
    // text input or normal function keys: Ctrl+Alt+F1..F8 brings up/focuses
    // Home, Terminal, Files, Forge, Web, Monitor, Settings and About.
    if (event.type == input::EventType::KeyDown && event.control && event.alt) {
        size_t pin = DOCK_PIN_COUNT;
        switch (event.key) {
            case drivers::keyboard::KeyCode::F1: pin = 0U; break;
            case drivers::keyboard::KeyCode::F2: pin = 1U; break;
            case drivers::keyboard::KeyCode::F3: pin = 2U; break;
            case drivers::keyboard::KeyCode::F4: pin = 3U; break;
            case drivers::keyboard::KeyCode::F5: pin = 4U; break;
            case drivers::keyboard::KeyCode::F6: pin = 5U; break;
            case drivers::keyboard::KeyCode::F7: pin = 6U; break;
            case drivers::keyboard::KeyCode::F8: pin = 7U; break;
            default: break;
        }
        if (pin < DOCK_PIN_COUNT && login_surface() == nullptr) {
            const Status result = activate_dock_pin(pin);
            // A missing session launcher must not corrupt keyboard state.
            return result == Status::NotFound ? Status::Ok : result;
        }
    }

    if (event.type == input::EventType::MouseMove && login_surface() == nullptr) {
        const bool visible = rect_contains(
            calculate_workspace().pulse_ribbon, event.x, event.y);
        if (visible != g_dock_visible) {
            g_dock_visible = visible;
            mark_full_dirty();  // Reveal/hide must repaint the old dock pixels.
        }
    }

    // Red Flux is mouse-first: physical desktop/window shortcuts are not
    // intercepted globally. Keyboard events continue to the focused app so
    // text entry, terminals and accessibility paths remain available.

    if (event.type == input::EventType::MouseButtonDown &&
        event.button == drivers::mouse::Left) {
        // A fresh primary-button press starts a fresh capture decision.
        // This recovers deterministically if a prior button-up was lost.
        g_dragged = INVALID_WINDOW;
        g_resized = INVALID_WINDOW;
        g_pointer_capture = INVALID_WINDOW;
        const WorkspaceGeometry workspace = calculate_workspace();
        if (login_surface() == nullptr &&
            rect_contains(workspace.pulse_ribbon, event.x, event.y)) {
            if (!g_dock_visible) {
                g_dock_visible = true;
                mark_full_dirty();
            }
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
            if (!is_login_surface(*slot) && !is_home_surface(slot->info.title)) {
                const ChromeGeometry chrome = calculate_chrome(slot->info.bounds);
                if (rect_contains(chrome.dismiss_control, event.x, event.y)) return dismiss(target);
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
                    return Status::Ok;
                }
            }
            // Non-client borders/chrome may focus a window but must not
            // deliver synthetic clicks to its application contents.
            if (!rect_contains(calculate_content(*slot), event.x, event.y)) {
                return Status::Ok;
            }
            g_pointer_capture = target;
        }
    } else if (event.type == input::EventType::MouseMove &&
               (event.buttons & drivers::mouse::Left) != 0U) {
        if (g_resized != INVALID_WINDOW) {
            Slot* slot = find(g_resized);
            if (slot != nullptr) {
                resize_window(*slot, event.x, event.y);
            } else {
                g_resized = INVALID_WINDOW;
            }
        } else if (g_dragged != INVALID_WINDOW) {
            const WindowId dragged = g_dragged;
            if (move(
                    dragged,
                    event.x - g_drag_offset_x,
                    event.y - g_drag_offset_y) != Status::Ok) {
                g_dragged = INVALID_WINDOW;
            }
        }
    } else if (event.type == input::EventType::MouseButtonUp &&
               event.button == drivers::mouse::Left) {
        const Slot* changed = find(g_dragged != INVALID_WINDOW ? g_dragged : g_resized);
        if (changed != nullptr) {
            trace_transition(g_dragged != INVALID_WINDOW ? "move" : "resize", changed->info);
        }
        g_dragged = INVALID_WINDOW;
        g_resized = INVALID_WINDOW;
    }

    update_pointer_feedback(event);

#ifndef KUROGANE_HOST_TEST
    if (event.type == input::EventType::MouseMove) move_cursor(event.x, event.y);
#endif

    // Keyboard ownership follows focus; pointer ownership follows the
    // topmost client content below the pointer, independently of focus.
    // While held, a primary-button gesture remains with its original
    // application even outside its bounds. System chrome consumes its own
    // gestures and unmatched primary releases must not leak to other apps.
    if (pointer_event) {
        if (!system_release && g_dragged == INVALID_WINDOW &&
            g_resized == INVALID_WINDOW) {
            WindowId destination = g_pointer_capture;
            if (destination == INVALID_WINDOW && !primary_release) {
                destination = hit_test(event.x, event.y);
                const Slot* candidate = find(destination);
                if (candidate == nullptr ||
                    !rect_contains(calculate_content(*candidate), event.x, event.y)) {
                    destination = INVALID_WINDOW;
                }
            }
            Slot* receiver = find(destination);
            if (receiver != nullptr && receiver->input_callback != nullptr) {
                receiver->input_callback(receiver->info.id, event, receiver->context);
            }
        }
        if (primary_release) g_pointer_capture = INVALID_WINDOW;
    } else {
        Slot* focused = find(g_focused);
        if (focused != nullptr && focused->input_callback != nullptr) {
            focused->input_callback(focused->info.id, event, focused->context);
        }
    }
    return Status::Ok;
}

void invalidate() {
    mark_full_dirty();
}

void invalidate_window(WindowId id) {
    if (!g_initialized) return;
    Slot* slot = find(id);
    if (slot == nullptr || !slot->occupied ||
        slot->info.state == WindowState::Minimized) return;
    add_damage_region(slot->info.bounds);
}

void invalidate_region(const ui::Rect& region) {
    add_damage_region(region);
}

Status damage_snapshot(DamageSnapshot* out_snapshot) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_snapshot == nullptr) return Status::InvalidArgument;
    *out_snapshot = {};
    out_snapshot->full = g_dirty == DirtyMode::Full;
    if (g_dirty != DirtyMode::Regions) return Status::Ok;
    out_snapshot->count = g_damage_count;
    for (size_t index = 0U; index < g_damage_count; ++index) {
        out_snapshot->regions[index] = g_damage_regions[index];
    }
    return Status::Ok;
}

Status interaction_snapshot(InteractionSnapshot* out_snapshot) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_snapshot == nullptr) return Status::InvalidArgument;
    out_snapshot->focused = g_focused;
    out_snapshot->dragged = g_dragged;
    out_snapshot->resized = g_resized;
    out_snapshot->hovered = g_hovered;
    return Status::Ok;
}

Status resource_snapshot(ResourceSnapshot* out_snapshot) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_snapshot == nullptr) return Status::InvalidArgument;
    *out_snapshot = {};
    for (const Slot& slot : g_slots) {
        if (!slot.occupied) continue;
        ++out_snapshot->windows;
        if (!slot.surface.valid) continue;
        ++out_snapshot->retained_surfaces;
        out_snapshot->retained_bytes += slot.surface.size;
    }
    return Status::Ok;
}

bool render_if_needed() {
#ifndef KUROGANE_HOST_TEST
    if (g_initialized && !g_rendering && graphics::available() &&
        (graphics::width() != static_cast<uint32_t>(g_screen_width) ||
         graphics::height() != static_cast<uint32_t>(g_screen_height))) {
        if (set_screen_size(graphics::width(), graphics::height()) != Status::Ok) return false;
    }
#endif
    if (!g_initialized || g_rendering || g_dirty == DirtyMode::None) return false;
    g_rendering = true;
#ifndef KUROGANE_HOST_TEST
    const DirtyMode pending_mode = g_dirty;
#endif
    const size_t pending_count = g_damage_count;
    ui::Rect pending[MAX_DAMAGE_REGIONS]{};
    for (size_t index = 0U; index < pending_count; ++index) {
        pending[index] = g_damage_regions[index];
    }
    // Consume only this frame's snapshot. Draw callbacks may invalidate new
    // regions (or the entire desktop) for the following frame.
    g_dirty = DirtyMode::None;
    g_damage_count = 0U;
#ifndef KUROGANE_HOST_TEST
    hide_cursor();
    const bool buffered = graphics::begin_frame();
    graphics::DamageRect regions[MAX_DAMAGE_REGIONS]{};
    bool partial_raster = false;
    if (pending_mode == DirtyMode::Regions && pending_count != 0U) {
        for (size_t index = 0U; index < pending_count; ++index) {
            regions[index] = {
                pending[index].x, pending[index].y,
                pending[index].width, pending[index].height,
            };
        }
        partial_raster = graphics::set_damage_regions(regions, pending_count);
    }
    render_layers();
    graphics::reset_damage_regions();
    if (buffered) {
        if (partial_raster) {
            graphics::end_frame_regions(regions, pending_count);
        } else {
            graphics::end_frame();
        }
    }
    show_cursor(input::pointer_x(), input::pointer_y());
#else
    // Host adapter omits framebuffer operations but exercises draw callbacks
    // inside the same frame lifecycle as production.
    WindowId frame_windows[MAX_WINDOWS]{};
    const size_t frame_count = snapshot_frame_windows(frame_windows);
    for (size_t position = 0U; position < frame_count; ++position) {
        Slot* slot = find(frame_windows[position]);
        if (slot != nullptr && slot->info.state != WindowState::Minimized &&
            slot->draw != nullptr) {
            slot->draw(slot->info.id, calculate_content(*slot),
                slot->info.focused, slot->context);
        }
    }
#endif
    g_rendering = false;
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
        case Status::PayloadTooLarge: return "window surface payload too large";
        case Status::ArithmeticOverflow: return "window surface size overflow";
        case Status::InvalidState: return "invalid window state";
        case Status::IterationStopped: return "window iteration stopped";
    }
    return "unknown window status";
}

} // namespace windowing
