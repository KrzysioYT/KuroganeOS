#include "../kernel/ui/window_manager.hpp"

namespace {
size_t g_draws = 0U;
size_t g_inputs = 0U;

void draw(windowing::WindowId, const ui::Rect&, bool, void*) { ++g_draws; }
void receive(windowing::WindowId, const input::Event&, void*) { ++g_inputs; }
} // namespace

int main() {
    using namespace windowing;
    if (initialize(0U, 0U) != Status::InvalidArgument ||
        initialize(800U, 600U) != Status::Ok) return 1;

    const WorkspaceGeometry workspace = workspace_geometry();
    if (workspace.work_area.x <= 0 || workspace.work_area.y <= 0 ||
        workspace.work_area.width >= 800 || workspace.work_area.height >= 600 ||
        workspace.signal_spine.width <= 0 || workspace.pulse_ribbon.height <= 0) {
        return 2;
    }

    WindowId first = INVALID_WINDOW;
    WindowId second = INVALID_WINDOW;
    if (create_window("First", 10U, {40, 50, 300, 220},
                      draw, receive, nullptr, &first) != Status::Ok ||
        create_window("Second", 11U, {100, 100, 320, 240},
                      draw, receive, nullptr, &second) != Status::Ok ||
        first == second || window_count() != 2U ||
        focused_window() != second) return 3;

    WindowInfo info{};
    if (query(second, &info) != Status::Ok || !info.focused ||
        info.owner_pid != 11U || info.z_order != 1U) return 4;
    if (focus(first) != Status::Ok || focused_window() != first ||
        query(first, &info) != Status::Ok || info.z_order != 1U) return 5;

    ChromeGeometry chrome{};
    if (chrome_geometry(first, &chrome) != Status::Ok ||
        chrome.header.height <= 30 || chrome.resize_grip.width <= 0 ||
        chrome.minimize_control.x >= chrome.expand_control.x ||
        chrome.expand_control.x >= chrome.dismiss_control.x) return 6;

    if (move(first, -100, 10000) != Status::Ok ||
        query(first, &info) != Status::Ok ||
        info.bounds.x != workspace.work_area.x ||
        info.bounds.y != workspace.work_area.y + workspace.work_area.height - info.bounds.height) {
        return 7;
    }

    const ui::Rect restored_bounds = info.bounds;
    if (maximize(first) != Status::Ok ||
        query(first, &info) != Status::Ok ||
        info.state != WindowState::Maximized ||
        info.bounds.x != workspace.work_area.x ||
        info.bounds.y != workspace.work_area.y ||
        info.bounds.width != workspace.work_area.width ||
        info.bounds.height != workspace.work_area.height) return 8;
    if (restore(first) != Status::Ok ||
        query(first, &info) != Status::Ok ||
        info.bounds.x != restored_bounds.x || info.bounds.y != restored_bounds.y) return 9;

    if (minimize(first) != Status::Ok || focused_window() != second) return 10;

    // Focus(first) moved it to the top of z-order, so its Pulse Ribbon item is
    // the second item. Clicking that item must restore and focus it.
    ui::Rect pulse_first{};
    if (pulse_item_geometry(1U, &pulse_first) != Status::Ok ||
        pulse_first.width <= 0 || pulse_first.height <= 0) return 11;
    input::Event event{};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = pulse_first.x + pulse_first.width / 2;
    event.y = pulse_first.y + pulse_first.height / 2;
    if (dispatch(event) != Status::Ok || focused_window() != first ||
        query(first, &info) != Status::Ok || info.state != WindowState::Normal) {
        return 12;
    }

    // Header drag still routes through the same chrome geometry.
    if (chrome_geometry(first, &chrome) != Status::Ok) return 13;
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = chrome.header.x + 10;
    event.y = chrome.header.y + 10;
    if (dispatch(event) != Status::Ok) return 14;
    event.type = input::EventType::MouseMove;
    event.x = 200;
    event.y = 150;
    if (dispatch(event) != Status::Ok || query(first, &info) != Status::Ok ||
        info.bounds.x != 190 || info.bounds.y != 140) return 15;
    event.type = input::EventType::MouseButtonUp;
    event.buttons = 0U;
    if (dispatch(event) != Status::Ok || g_inputs == 0U) return 16;

    // Exercise the geometric expand control instead of a textual [] button.
    if (chrome_geometry(first, &chrome) != Status::Ok) return 17;
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = chrome.expand_control.x + chrome.expand_control.width / 2;
    event.y = chrome.expand_control.y + chrome.expand_control.height / 2;
    if (dispatch(event) != Status::Ok || query(first, &info) != Status::Ok ||
        info.state != WindowState::Maximized) return 18;

    // Alt+F4 remains a stable keyboard path regardless of Flux chrome.
    event = {};
    event.type = input::EventType::KeyDown;
    event.alt = true;
    event.key = drivers::keyboard::KeyCode::F4;
    if (dispatch(event) != Status::Ok || window_count() != 1U ||
        query(first, &info) != Status::NotFound) return 19;
    if (!render_if_needed() || render_if_needed()) return 20;
    return 0;
}
