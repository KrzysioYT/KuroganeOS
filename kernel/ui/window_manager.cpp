#include "window_manager.hpp"

#ifndef KUROGANE_HOST_TEST
#include "../drivers/framebuffer.hpp"
#endif

namespace windowing {
namespace {

constexpr int32_t HEADER_HEIGHT = 36;
constexpr int32_t MINIMUM_WIDTH = 160;
constexpr int32_t MINIMUM_HEIGHT = 96;
constexpr int32_t WORKSPACE_LEFT = 34;
constexpr int32_t WORKSPACE_TOP = 42;
constexpr int32_t WORKSPACE_RIGHT = 12;
constexpr int32_t RIBBON_HEIGHT = 28;
constexpr int32_t RIBBON_BOTTOM = 8;
constexpr int32_t WORKSPACE_BOTTOM = RIBBON_HEIGHT + RIBBON_BOTTOM + 10;
constexpr int32_t SPINE_X = 8;
constexpr int32_t SPINE_WIDTH = 18;
constexpr int32_t CONTROL_WIDTH = 22;
constexpr int32_t CONTROL_HEIGHT = 20;
constexpr int32_t CONTROL_GAP = 4;
constexpr int32_t CONTROL_RIGHT = 8;
constexpr int32_t RIBBON_GAP = 6;
constexpr int32_t RIBBON_ITEM_MAX = 124;
constexpr int32_t RIBBON_ITEM_MIN = 54;
constexpr int32_t RIBBON_PADDING = 10;
constexpr int32_t RIBBON_SIGNAL_RESERVE = 24;

struct Slot {
    WindowInfo info;
    DrawCallback draw;
    InputCallback input_callback;
    void* context;
    uint16_t generation;
    bool occupied;
};

Slot g_slots[MAX_WINDOWS]{};
uint8_t g_order[MAX_WINDOWS]{};
size_t g_count = 0U;
int32_t g_screen_width = 0;
int32_t g_screen_height = 0;
WindowId g_focused = INVALID_WINDOW;
WindowId g_dragged = INVALID_WINDOW;
int32_t g_drag_offset_x = 0;
int32_t g_drag_offset_y = 0;
bool g_dirty = false;
bool g_initialized = false;

size_t text_length(const char* text, size_t maximum) {
    if (text == nullptr) return 0U;
    size_t length = 0U;
    while (length < maximum && text[length] != '\0') ++length;
    return length;
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
        WORKSPACE_TOP + 6,
        SPINE_WIDTH,
        work_height > 18 ? work_height - 12 : work_height,
    };

    const int32_t ribbon_max_width = g_screen_width - 64;
    int32_t requested_width = 180;
    if (g_count != 0U) {
        requested_width = RIBBON_PADDING * 2 + RIBBON_SIGNAL_RESERVE +
            static_cast<int32_t>(g_count) * (RIBBON_ITEM_MAX + RIBBON_GAP);
    }
    const int32_t ribbon_width = clamp_size(
        requested_width,
        ribbon_max_width < 180 ? ribbon_max_width : 180,
        ribbon_max_width);
    geometry.pulse_ribbon = {
        (g_screen_width - ribbon_width) / 2,
        g_screen_height - RIBBON_HEIGHT - RIBBON_BOTTOM,
        ribbon_width,
        RIBBON_HEIGHT,
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
        bounds.x + bounds.width - 16,
        bounds.y + bounds.height - 16,
        16,
        16,
    };
    return geometry;
}

int32_t ribbon_item_width(const WorkspaceGeometry& workspace) {
    if (g_count == 0U) return 0;
    const int32_t usable = workspace.pulse_ribbon.width -
        RIBBON_PADDING * 2 - RIBBON_SIGNAL_RESERVE -
        static_cast<int32_t>(g_count - 1U) * RIBBON_GAP;
    return clamp_size(
        usable / static_cast<int32_t>(g_count),
        RIBBON_ITEM_MIN,
        RIBBON_ITEM_MAX);
}

ui::Rect ribbon_item_rect(size_t position) {
    const WorkspaceGeometry workspace = calculate_workspace();
    if (position >= g_count) return {};
    const int32_t width = ribbon_item_width(workspace);
    return {
        workspace.pulse_ribbon.x + RIBBON_PADDING +
            static_cast<int32_t>(position) * (width + RIBBON_GAP),
        workspace.pulse_ribbon.y + 4,
        width,
        workspace.pulse_ribbon.height - 8,
    };
}

bool valid_bounds(const ui::Rect& bounds) {
    if (!g_initialized) return false;
    const WorkspaceGeometry workspace = calculate_workspace();
    if (bounds.width < MINIMUM_WIDTH || bounds.height < MINIMUM_HEIGHT ||
        bounds.width > g_screen_width ||
        bounds.height > workspace.work_area.height) return false;
    return bounds.x >= 0 && bounds.y >= WORKSPACE_TOP &&
        bounds.x <= g_screen_width - bounds.width &&
        bounds.y <= workspace.work_area.y + workspace.work_area.height - bounds.height;
}

void update_z_order() {
    for (size_t position = 0U; position < g_count; ++position) {
        g_slots[g_order[position]].info.z_order =
            static_cast<uint8_t>(position);
        g_slots[g_order[position]].info.focused =
            g_slots[g_order[position]].info.id == g_focused;
    }
}

void choose_top_focus() {
    g_focused = INVALID_WINDOW;
    for (size_t position = g_count; position > 0U; --position) {
        Slot& slot = g_slots[g_order[position - 1U]];
        if (slot.info.state != WindowState::Minimized) {
            g_focused = slot.info.id;
            break;
        }
    }
    update_z_order();
}

size_t focused_position() {
    for (size_t position = 0U; position < g_count; ++position) {
        if (g_slots[g_order[position]].info.id == g_focused) return position;
    }
    return g_count;
}

bool title_hit(const Slot& slot, int32_t x, int32_t y) {
    const ChromeGeometry chrome = calculate_chrome(slot.info.bounds);
    return rect_contains(chrome.header, x, y) &&
        !rect_contains(chrome.minimize_control, x, y) &&
        !rect_contains(chrome.expand_control, x, y) &&
        !rect_contains(chrome.dismiss_control, x, y);
}

WindowId hit_test(int32_t x, int32_t y) {
    for (size_t position = g_count; position > 0U; --position) {
        Slot& slot = g_slots[g_order[position - 1U]];
        if (slot.info.state != WindowState::Minimized &&
            rect_contains(slot.info.bounds, x, y)) return slot.info.id;
    }
    return INVALID_WINDOW;
}

Status cycle_focus() {
    if (g_count == 0U) return Status::NotFound;
    size_t current = g_count;
    for (size_t position = 0U; position < g_count; ++position) {
        if (g_slots[g_order[position]].info.id == g_focused) {
            current = position;
            break;
        }
    }
    for (size_t offset = 1U; offset <= g_count; ++offset) {
        const size_t position = (current + offset) % g_count;
        Slot& slot = g_slots[g_order[position]];
        if (slot.info.state != WindowState::Minimized) return focus(slot.info.id);
    }
    return Status::NotFound;
}

Status activate_ribbon_item(size_t position) {
    if (position >= g_count) return Status::NotFound;
    Slot& slot = g_slots[g_order[position]];
    if (slot.info.state == WindowState::Minimized) {
        return restore(slot.info.id);
    }
    return focus(slot.info.id);
}

#ifndef KUROGANE_HOST_TEST
void draw_cursor(int32_t x, int32_t y) {
    constexpr graphics::Color outline = graphics::rgb(2U, 6U, 23U);
    constexpr graphics::Color fill = graphics::rgb(248U, 250U, 252U);
    constexpr graphics::Color signal = graphics::rgb(62U, 220U, 181U);
    for (int32_t row = 0; row < 14; ++row) {
        const int32_t width = row / 2 + 1;
        graphics::fill_rect(x, y + row, width + 2, 1, outline);
        if (width > 1) graphics::fill_rect(x + 1, y + row, width, 1, fill);
    }
    graphics::fill_rect(x + 3, y + 10, 3, 8, outline);
    graphics::fill_rect(x + 7, y + 14, 3, 3, signal);
}

void render() {
    ui::desktop("KUROGANE / FLUX");
    const WorkspaceGeometry workspace = calculate_workspace();
    ui::signal_spine(
        workspace.signal_spine,
        g_count,
        focused_position());

    for (size_t position = 0U; position < g_count; ++position) {
        Slot& slot = g_slots[g_order[position]];
        if (slot.info.state == WindowState::Minimized) continue;
        const ui::Rect& bounds = slot.info.bounds;
        const ChromeGeometry chrome = calculate_chrome(bounds);
        ui::flux_window(bounds, slot.info.title, slot.info.focused);
        ui::flux_control(
            chrome.minimize_control,
            ui::FluxControl::Minimize,
            slot.info.focused);
        ui::flux_control(
            chrome.expand_control,
            ui::FluxControl::Expand,
            slot.info.state == WindowState::Maximized);
        ui::flux_control(
            chrome.dismiss_control,
            ui::FluxControl::Dismiss,
            slot.info.focused);
        if (slot.draw != nullptr) {
            const ui::Rect content = {
                bounds.x + 4,
                bounds.y + HEADER_HEIGHT,
                bounds.width - 8,
                bounds.height - HEADER_HEIGHT - 4,
            };
            slot.draw(slot.info.id, content, slot.info.focused, slot.context);
        }
    }

    ui::pulse_ribbon(workspace.pulse_ribbon, g_count);
    for (size_t position = 0U; position < g_count; ++position) {
        const Slot& slot = g_slots[g_order[position]];
        ui::pulse_item(
            ribbon_item_rect(position),
            slot.info.title,
            slot.info.focused,
            slot.info.state == WindowState::Minimized);
    }
    draw_cursor(input::pointer_x(), input::pointer_y());
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
    g_count = 0U;
    g_screen_width = static_cast<int32_t>(screen_width);
    g_screen_height = static_cast<int32_t>(screen_height);
    g_focused = INVALID_WINDOW;
    g_dragged = INVALID_WINDOW;
    g_dirty = true;
    g_initialized = true;
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
        text_length(title, 33U) > 32U || !valid_bounds(bounds)) {
        return Status::InvalidArgument;
    }
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
    slot.info.bounds = bounds;
    slot.info.restore_bounds = bounds;
    slot.info.owner_pid = owner_pid;
    slot.info.state = WindowState::Normal;
    copy_title(slot.info.title, title);
    slot.draw = draw;
    slot.input_callback = input_callback;
    slot.context = context;
    g_order[g_count++] = static_cast<uint8_t>(selected);
    g_focused = slot.info.id;
    update_z_order();
    g_dirty = true;
    *out_id = slot.info.id;
    return Status::Ok;
}

Status close(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    size_t slot_index = 0U;
    Slot* slot = find(id, &slot_index);
    if (slot == nullptr) return Status::NotFound;
    size_t position = 0U;
    while (position < g_count && g_order[position] != slot_index) ++position;
    for (size_t index = position + 1U; index < g_count; ++index) {
        g_order[index - 1U] = g_order[index];
    }
    --g_count;
    slot->occupied = false;
    if (g_dragged == id) g_dragged = INVALID_WINDOW;
    choose_top_focus();
    g_dirty = true;
    return Status::Ok;
}

Status focus(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    if (g_count == 0U) return Status::NotFound;
    size_t slot_index = 0U;
    Slot* slot = find(id, &slot_index);
    if (slot == nullptr) return Status::NotFound;
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
    g_dirty = true;
    return Status::Ok;
}

Status move(WindowId id, int32_t x, int32_t y) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    if (slot->info.state != WindowState::Normal) return Status::InvalidState;
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
    g_dirty = true;
    return Status::Ok;
}

Status minimize(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    if (slot->info.state == WindowState::Minimized) return Status::InvalidState;
    slot->info.state = WindowState::Minimized;
    if (g_focused == id) choose_top_focus();
    g_dirty = true;
    return Status::Ok;
}

Status maximize(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    if (slot->info.state == WindowState::Maximized) return Status::InvalidState;
    if (slot->info.state == WindowState::Normal) {
        slot->info.restore_bounds = slot->info.bounds;
    }
    const WorkspaceGeometry workspace = calculate_workspace();
    slot->info.state = WindowState::Maximized;
    slot->info.bounds = workspace.work_area;
    static_cast<void>(focus(id));
    g_dirty = true;
    return Status::Ok;
}

Status restore(WindowId id) {
    if (!g_initialized) return Status::NotInitialized;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    if (slot->info.state == WindowState::Normal) return Status::InvalidState;
    slot->info.state = WindowState::Normal;
    slot->info.bounds = slot->info.restore_bounds;
    static_cast<void>(focus(id));
    g_dirty = true;
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
    for (size_t position = 0U; position < g_count; ++position) {
        if (!callback(g_slots[g_order[position]].info, context)) {
            return Status::IterationStopped;
        }
    }
    return Status::Ok;
}

WorkspaceGeometry workspace_geometry() {
    return calculate_workspace();
}

Status chrome_geometry(WindowId id, ChromeGeometry* out_geometry) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_geometry == nullptr) return Status::InvalidArgument;
    Slot* slot = find(id);
    if (slot == nullptr) return Status::NotFound;
    *out_geometry = calculate_chrome(slot->info.bounds);
    return Status::Ok;
}

Status pulse_item_geometry(size_t position, ui::Rect* out_bounds) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_bounds == nullptr) return Status::InvalidArgument;
    if (position >= g_count) return Status::NotFound;
    *out_bounds = ribbon_item_rect(position);
    return Status::Ok;
}

Status dispatch(const input::Event& event) {
    if (!g_initialized) return Status::NotInitialized;
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
        if (rect_contains(workspace.pulse_ribbon, event.x, event.y)) {
            for (size_t position = 0U; position < g_count; ++position) {
                if (rect_contains(ribbon_item_rect(position), event.x, event.y)) {
                    return activate_ribbon_item(position);
                }
            }
        }

        const WindowId target = hit_test(event.x, event.y);
        if (target != INVALID_WINDOW) {
            static_cast<void>(focus(target));
            Slot* slot = find(target);
            if (slot == nullptr) return Status::NotFound;
            const ChromeGeometry chrome = calculate_chrome(slot->info.bounds);
            if (rect_contains(chrome.dismiss_control, event.x, event.y)) {
                return close(target);
            }
            if (rect_contains(chrome.minimize_control, event.x, event.y)) {
                return minimize(target);
            }
            if (rect_contains(chrome.expand_control, event.x, event.y)) {
                return slot->info.state == WindowState::Maximized
                    ? restore(target)
                    : maximize(target);
            }
            if (slot->info.state == WindowState::Normal &&
                title_hit(*slot, event.x, event.y)) {
                g_dragged = target;
                g_drag_offset_x = event.x - slot->info.bounds.x;
                g_drag_offset_y = event.y - slot->info.bounds.y;
            }
        }
    } else if (event.type == input::EventType::MouseMove &&
               g_dragged != INVALID_WINDOW &&
               (event.buttons & drivers::mouse::Left) != 0U) {
        static_cast<void>(move(
            g_dragged,
            event.x - g_drag_offset_x,
            event.y - g_drag_offset_y));
    } else if (event.type == input::EventType::MouseButtonUp &&
               event.button == drivers::mouse::Left) {
        g_dragged = INVALID_WINDOW;
    }
    Slot* focused = find(g_focused);
    if (focused != nullptr && focused->input_callback != nullptr) {
        focused->input_callback(focused->info.id, event, focused->context);
    }
    if (event.type == input::EventType::MouseMove ||
        event.type == input::EventType::MouseButtonDown ||
        event.type == input::EventType::MouseButtonUp) g_dirty = true;
    return Status::Ok;
}

void invalidate() { g_dirty = true; }

bool render_if_needed() {
    if (!g_initialized || !g_dirty) return false;
#ifndef KUROGANE_HOST_TEST
    render();
#endif
    g_dirty = false;
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
