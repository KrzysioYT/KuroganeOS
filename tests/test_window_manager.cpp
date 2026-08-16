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
    WindowId first = INVALID_WINDOW;
    WindowId second = INVALID_WINDOW;
    if (create_window("First", 10U, {20, 50, 300, 220},
                      draw, receive, nullptr, &first) != Status::Ok ||
        create_window("Second", 11U, {100, 100, 320, 240},
                      draw, receive, nullptr, &second) != Status::Ok ||
        first == second || window_count() != 2U ||
        focused_window() != second) return 2;
    WindowInfo info{};
    if (query(second, &info) != Status::Ok || !info.focused ||
        info.owner_pid != 11U || info.z_order != 1U) return 3;
    if (focus(first) != Status::Ok || focused_window() != first ||
        query(first, &info) != Status::Ok || info.z_order != 1U) return 4;
    if (move(first, -100, 10000) != Status::Ok ||
        query(first, &info) != Status::Ok || info.bounds.x != 0 ||
        info.bounds.y != 350) return 5;
    if (maximize(first) != Status::Ok ||
        query(first, &info) != Status::Ok ||
        info.state != WindowState::Maximized || info.bounds.width != 800) return 6;
    if (restore(first) != Status::Ok ||
        query(first, &info) != Status::Ok || info.bounds.x != 0 ||
        info.bounds.y != 350) return 7;
    if (minimize(first) != Status::Ok || focused_window() != second ||
        restore(first) != Status::Ok || focused_window() != first) return 8;

    input::Event event{};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = info.bounds.x + 10;
    event.y = info.bounds.y + 10;
    if (dispatch(event) != Status::Ok) return 9;
    event.type = input::EventType::MouseMove;
    event.x = 200;
    event.y = 150;
    if (dispatch(event) != Status::Ok || query(first, &info) != Status::Ok ||
        info.bounds.x != 190 || info.bounds.y != 140) return 10;
    event.type = input::EventType::MouseButtonUp;
    event.buttons = 0U;
    if (dispatch(event) != Status::Ok || g_inputs == 0U) return 11;

    event = {};
    event.type = input::EventType::KeyDown;
    event.alt = true;
    event.key = drivers::keyboard::KeyCode::F4;
    if (dispatch(event) != Status::Ok || window_count() != 1U ||
        query(first, &info) != Status::NotFound) return 12;
    if (!render_if_needed() || render_if_needed()) return 13;
    return 0;
}
