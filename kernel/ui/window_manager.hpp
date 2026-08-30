#pragma once

#include "ui.hpp"
#include "../input/input.hpp"

#include <stddef.h>
#include <stdint.h>

namespace windowing {

constexpr size_t MAX_WINDOWS = 12U;
constexpr size_t MAX_DAMAGE_REGIONS = 16U;
using WindowId = uint32_t;
constexpr WindowId INVALID_WINDOW = 0U;

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    InvalidArgument,
    NotFound,
    CapacityReached,
    InvalidState,
    IterationStopped,
};

enum class WindowState : uint8_t {
    Normal = 0,
    Minimized,
    Maximized,
};

struct WindowInfo {
    WindowId id;
    ui::Rect bounds;
    ui::Rect restore_bounds;
    uint64_t owner_pid;
    WindowState state;
    uint8_t z_order;
    bool focused;
    char title[33];
};

struct WorkspaceGeometry {
    ui::Rect work_area;
    ui::Rect signal_spine;
    ui::Rect pulse_ribbon;
};

struct ChromeGeometry {
    ui::Rect header;
    ui::Rect minimize_control;
    ui::Rect expand_control;
    ui::Rect dismiss_control;
    ui::Rect resize_grip;
};

using DrawCallback = void (*)(
    WindowId id,
    const ui::Rect& content_bounds,
    bool focused,
    void* context);
using InputCallback = void (*)(
    WindowId id,
    const input::Event& event,
    void* context);
using ListCallback = bool (*)(const WindowInfo& window, void* context);

Status initialize(uint32_t screen_width, uint32_t screen_height);
Status create_window(
    const char* title,
    uint64_t owner_pid,
    const ui::Rect& bounds,
    DrawCallback draw,
    InputCallback input_callback,
    void* context,
    WindowId* out_id);
Status close(WindowId id);
Status focus(WindowId id);
Status move(WindowId id, int32_t x, int32_t y);
Status minimize(WindowId id);
Status maximize(WindowId id);
Status restore(WindowId id);
Status query(WindowId id, WindowInfo* out_info);
Status list(ListCallback callback, void* context);
Status dispatch(const input::Event& event);
WorkspaceGeometry workspace_geometry();
Status chrome_geometry(WindowId id, ChromeGeometry* out_geometry);
Status pulse_item_geometry(size_t position, ui::Rect* out_bounds);

// Public syscall bridge for session-local desktop shortcuts. `action` uses the
// KU_DESKTOP_PIN_* values from the SDK. Home is intentionally immutable and
// Performance starts pinned by default.
Status desktop_pin(
    uint32_t app_id,
    uint32_t action,
    bool value,
    bool* out_pinned);

void invalidate();
void invalidate_window(WindowId id);
void invalidate_region(const ui::Rect& region);
bool render_if_needed();
size_t window_count();
WindowId focused_window();
bool initialized();
const char* status_message(Status status);

} // namespace windowing
