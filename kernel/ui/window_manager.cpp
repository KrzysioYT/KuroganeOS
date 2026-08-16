#include "window_manager.hpp"

#ifndef KUROGANE_HOST_TEST
#include "../drivers/framebuffer.hpp"
#endif

namespace windowing {
namespace {

constexpr int32_t TITLE_HEIGHT = 30;
constexpr int32_t TASKBAR_HEIGHT = 30;
constexpr int32_t MINIMUM_WIDTH = 160;
constexpr int32_t MINIMUM_HEIGHT = 90;

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

bool rect_contains(const ui::Rect& rectangle, int32_t x, int32_t y) {
    return x >= rectangle.x && y >= rectangle.y &&
        x < rectangle.x + rectangle.width &&
        y < rectangle.y + rectangle.height;
}

bool valid_bounds(const ui::Rect& bounds) {
    if (bounds.width < MINIMUM_WIDTH || bounds.height < MINIMUM_HEIGHT ||
        bounds.width > g_screen_width ||
        bounds.height > g_screen_height - TASKBAR_HEIGHT) return false;
    return bounds.x >= 0 && bounds.y >= 0 &&
        bounds.x <= g_screen_width - bounds.width &&
        bounds.y <= g_screen_height - TASKBAR_HEIGHT - bounds.height;
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

ui::Rect close_button(const ui::Rect& bounds) {
    return {bounds.x + bounds.width - 27, bounds.y + 4, 22, 20};
}

ui::Rect maximize_button(const ui::Rect& bounds) {
    return {bounds.x + bounds.width - 53, bounds.y + 4, 22, 20};
}

ui::Rect minimize_button(const ui::Rect& bounds) {
    return {bounds.x + bounds.width - 79, bounds.y + 4, 22, 20};
}

bool title_hit(const ui::Rect& bounds, int32_t x, int32_t y) {
    return rect_contains({bounds.x, bounds.y, bounds.width, TITLE_HEIGHT}, x, y);
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

#ifndef KUROGANE_HOST_TEST
void draw_cursor(int32_t x, int32_t y) {
    constexpr graphics::Color outline = graphics::rgb(2U, 6U, 23U);
    constexpr graphics::Color fill = graphics::rgb(248U, 250U, 252U);
    for (int32_t row = 0; row < 14; ++row) {
        const int32_t width = row / 2 + 1;
        graphics::fill_rect(x, y + row, width + 2, 1, outline);
        if (width > 1) graphics::fill_rect(x + 1, y + row, width, 1, fill);
    }
    graphics::fill_rect(x + 3, y + 10, 3, 8, outline);
}

void render() {
    ui::desktop("KUROGANE OS DESKTOP");
    for (size_t position = 0U; position < g_count; ++position) {
        Slot& slot = g_slots[g_order[position]];
        if (slot.info.state == WindowState::Minimized) continue;
        const ui::Rect& bounds = slot.info.bounds;
        ui::window(bounds, slot.info.title);
        ui::button(minimize_button(bounds), "-", false);
        ui::button(maximize_button(bounds), "[]", false);
        ui::button(close_button(bounds), "X", slot.info.focused);
        if (slot.draw != nullptr) {
            const ui::Rect content = {
                bounds.x + 2,
                bounds.y + TITLE_HEIGHT,
                bounds.width - 4,
                bounds.height - TITLE_HEIGHT - 2
            };
            slot.draw(slot.info.id, content, slot.info.focused, slot.context);
        }
    }
    ui::taskbar("WINDOWS: click title to focus/drag; controls: - [] X");
    const int32_t task_y = g_screen_height - TASKBAR_HEIGHT + 3;
    int32_t task_x = 8;
    for (size_t position = 0U; position < g_count; ++position) {
        Slot& slot = g_slots[g_order[position]];
        const ui::Rect button = {task_x, task_y, 112, 23};
        ui::button(button, slot.info.title, slot.info.focused);
        task_x += 118;
        if (task_x + 112 >= g_screen_width) break;
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
    slot->info.bounds.x = clamp_position(
        x, 0, g_screen_width - slot->info.bounds.width);
    slot->info.bounds.y = clamp_position(
        y, 0, g_screen_height - TASKBAR_HEIGHT - slot->info.bounds.height);
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
    slot->info.state = WindowState::Maximized;
    slot->info.bounds = {0, 38, g_screen_width,
                         g_screen_height - 38 - TASKBAR_HEIGHT};
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
        if (event.y >= g_screen_height - TASKBAR_HEIGHT && event.x >= 8) {
            const size_t position = static_cast<size_t>((event.x - 8) / 118);
            if (position < g_count) {
                Slot& task = g_slots[g_order[position]];
                return task.info.state == WindowState::Minimized
                    ? restore(task.info.id)
                    : focus(task.info.id);
            }
        }
        const WindowId target = hit_test(event.x, event.y);
        if (target != INVALID_WINDOW) {
            static_cast<void>(focus(target));
            Slot* slot = find(target);
            if (rect_contains(close_button(slot->info.bounds), event.x, event.y)) {
                return close(target);
            }
            if (rect_contains(minimize_button(slot->info.bounds), event.x, event.y)) {
                return minimize(target);
            }
            if (rect_contains(maximize_button(slot->info.bounds), event.x, event.y)) {
                return slot->info.state == WindowState::Maximized
                    ? restore(target)
                    : maximize(target);
            }
            if (slot->info.state == WindowState::Normal &&
                title_hit(slot->info.bounds, event.x, event.y)) {
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
