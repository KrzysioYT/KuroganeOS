#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <kurogane/desktop.h>

#include "../kernel/ui/window_manager.hpp"

namespace {

struct InputTrace {
    size_t calls = 0U;
    char last_character = '\0';
};

void input_trace(
    windowing::WindowId,
    const input::Event& event,
    void* context) {
    auto* trace = static_cast<InputTrace*>(context);
    assert(trace != nullptr);
    ++trace->calls;
    trace->last_character = event.character;
}

input::Event click_at(const ui::Rect& bounds) {
    input::Event event{};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = bounds.x + bounds.width / 2;
    event.y = bounds.y + bounds.height / 2;
    return event;
}

windowing::WindowInfo query_info(windowing::WindowId id) {
    windowing::WindowInfo info{};
    assert(windowing::query(id, &info) == windowing::Status::Ok);
    return info;
}

} // namespace

int main() {
    using namespace windowing;

    assert(initialize(1280U, 720U) == Status::Ok);
    assert(initialized());
    assert(window_count() == 0U);
    const WorkspaceGeometry workspace = workspace_geometry();
    assert(workspace.work_area.width > 900);
    assert(workspace.work_area.height > 500);
    assert(workspace.pulse_ribbon.width > 0);

    InputTrace launcher_trace{};
    WindowId launcher = INVALID_WINDOW;
    assert(create_window(
        "BLADE LAUNCHER",
        100U,
        {120, 100, 620, 420},
        nullptr,
        input_trace,
        &launcher_trace,
        &launcher) == Status::Ok);
    assert(query_info(launcher).state == WindowState::Minimized);

    WindowId kurosh = INVALID_WINDOW;
    WindowId vault = INVALID_WINDOW;
    assert(create_window(
        "KUROSH", 101U, {90, 90, 520, 360},
        nullptr, nullptr, nullptr, &kurosh) == Status::Ok);
    assert(create_window(
        "VAULT", 102U, {180, 130, 620, 420},
        nullptr, nullptr, nullptr, &vault) == Status::Ok);
    assert(window_count() == 3U);
    assert(focused_window() == vault);
    assert(query_info(vault).focused);
    assert(!query_info(kurosh).focused);

    // Alt+Tab rotates focus through exposed, non-home windows.
    input::Event alt_tab{};
    alt_tab.type = input::EventType::KeyDown;
    alt_tab.key = drivers::keyboard::KeyCode::Tab;
    alt_tab.alt = true;
    assert(dispatch(alt_tab) == Status::Ok);
    assert(focused_window() == kurosh);

    // Minimize via real window chrome and restore by clicking its task-ribbon item.
    ChromeGeometry chrome{};
    assert(chrome_geometry(kurosh, &chrome) == Status::Ok);
    assert(dispatch(click_at(chrome.minimize_control)) == Status::Ok);
    assert(query_info(kurosh).state == WindowState::Minimized);
    assert(focused_window() == vault);

    ui::Rect task0{};
    assert(pulse_item_geometry(0U, &task0) == Status::Ok);
    assert(dispatch(click_at(task0)) == Status::Ok);
    assert(query_info(kurosh).state == WindowState::Normal);
    assert(focused_window() == kurosh);

    // Expand control maximizes and the same control restores original geometry.
    const ui::Rect original = query_info(kurosh).bounds;
    assert(chrome_geometry(kurosh, &chrome) == Status::Ok);
    assert(dispatch(click_at(chrome.expand_control)) == Status::Ok);
    WindowInfo maximized = query_info(kurosh);
    assert(maximized.state == WindowState::Maximized);
    assert(maximized.bounds.x == workspace.work_area.x);
    assert(maximized.bounds.y == workspace.work_area.y);
    assert(maximized.bounds.width == workspace.work_area.width);
    assert(maximized.bounds.height == workspace.work_area.height);

    assert(chrome_geometry(kurosh, &chrome) == Status::Ok);
    assert(dispatch(click_at(chrome.expand_control)) == Status::Ok);
    WindowInfo restored = query_info(kurosh);
    assert(restored.state == WindowState::Normal);
    assert(restored.bounds.x == original.x);
    assert(restored.bounds.y == original.y);
    assert(restored.bounds.width == original.width);
    assert(restored.bounds.height == original.height);

    // Alt+F4 closes the focused application and moves focus to the next top window.
    input::Event alt_f4{};
    alt_f4.type = input::EventType::KeyDown;
    alt_f4.key = drivers::keyboard::KeyCode::F4;
    alt_f4.alt = true;
    assert(dispatch(alt_f4) == Status::Ok);
    assert(windowing::query(kurosh, &restored) == Status::NotFound);
    assert(window_count() == 2U);
    assert(focused_window() == vault);

    // The home surface is persistent: close minimizes it rather than destroying it.
    assert(restore(launcher) == Status::Ok);
    assert(focused_window() == launcher);
    assert(close(launcher) == Status::Ok);
    assert(query_info(launcher).state == WindowState::Minimized);
    assert(window_count() == 2U);
    assert(focused_window() == vault);

    // Stale IDs stay invalid even when a slot is reused (generation counter).
    const WindowId old_vault = vault;
    assert(close(vault) == Status::Ok);
    assert(window_count() == 1U);
    WindowId replacement = INVALID_WINDOW;
    assert(create_window(
        "SYSTEM MONITOR", 103U, {220, 150, 560, 380},
        nullptr, nullptr, nullptr, &replacement) == Status::Ok);
    assert(replacement != old_vault);
    WindowInfo ignored{};
    assert(windowing::query(old_vault, &ignored) == Status::NotFound);
    assert(windowing::query(replacement, &ignored) == Status::Ok);

    // Desktop pin ABI remains stable and the immutable home pin cannot be disabled.
    bool pinned = false;
    assert(desktop_pin(KU_DESKTOP_APP_HOME, KU_DESKTOP_PIN_QUERY, false, &pinned) == Status::Ok);
    assert(pinned);
    assert(desktop_pin(KU_DESKTOP_APP_HOME, KU_DESKTOP_PIN_SET, false, &pinned) == Status::Ok);
    assert(pinned);
    assert(desktop_pin(KU_DESKTOP_APP_TERMINAL, KU_DESKTOP_PIN_SET, true, &pinned) == Status::Ok);
    assert(pinned);
    assert(desktop_pin(KU_DESKTOP_APP_TERMINAL, KU_DESKTOP_PIN_TOGGLE, false, &pinned) == Status::Ok);
    assert(!pinned);

    assert(render_if_needed());
    assert(!render_if_needed());

    std::cout << "[TEST] desktop_window_lifecycle: PASS\n";
    std::cout << "[TEST] desktop_focus_cycle: PASS\n";
    std::cout << "[TEST] desktop_task_ribbon_restore: PASS\n";
    std::cout << "[TEST] desktop_window_generation: PASS\n";
    return 0;
}
