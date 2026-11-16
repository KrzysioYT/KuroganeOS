#include <cassert>
#include <cstdlib>
#include "../kernel/ui/window_manager.cpp"

// This test links the window manager without framebuffer/graphics modules.
namespace ui {
uint32_t g_host_style = KU_FLUX_STYLE_MODERN;
uint32_t g_host_scale = 100U;
uint32_t desktop_style_id() { return g_host_style; }
uint32_t desktop_text_scale_percent() { return g_host_scale; }
}

namespace {
char command;
void receive(windowing::WindowId, const input::Event& event, void*) {
    if (event.type == input::EventType::KeyDown) command = event.character;
}
int tablet_pixel(int logical, int logical_max, int screen_max) {
    return (logical * 32767 / logical_max) * screen_max / 32767;
}
void click(int x, int y) {
    input::Event event{};
    event.x = x;
    event.y = y;
    event.type = input::EventType::MouseMove;
    assert(windowing::dispatch(event) == windowing::Status::Ok);
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left; event.buttons = drivers::mouse::Left;
    assert(windowing::dispatch(event) == windowing::Status::Ok);
    event.type = input::EventType::MouseButtonUp; event.buttons = 0;
    assert(windowing::dispatch(event) == windowing::Status::Ok);
}
}
int main() {
    using namespace windowing;
    const int widths[] = {1024, 1280};
    for (int width : widths) {
        const int height = width == 1024 ? 768 : 800;
        assert(initialize(width, height) == Status::Ok);
        WindowId home, files;
        assert(create_window(KU_FLUX_TITLE_HOME, 1, {120, 90, 600, 450},
            nullptr, receive, nullptr, &home) == Status::Ok);
        command = 0;
        const WorkspaceGeometry layout = workspace_geometry();
        assert(layout.signal_spine.width == 79);
        assert(layout.pulse_ribbon.width == width);
        assert(layout.work_area.x == 95 && layout.work_area.y == 66);
        const int rail_center = layout.signal_spine.x + layout.signal_spine.width / 2;
        // Exercise the exact normalized points used by the QMP smoke
        // through the USB tablet mapping, at both guest GOP sizes.
        click(tablet_pixel(42, 1023, width - 1), tablet_pixel(174, 767, height - 1));
        assert(command == 'f');
        // Custom order must change both paint position and the hitbox while
        // leaving the command associated with each app ID unchanged.
        uint32_t target_position = 3U;
        assert(desktop_dock_order(2U, 7U, &target_position) == Status::Ok);
        command = 0;
        click(tablet_pixel(42, 1023, width - 1), tablet_pixel(174, 767, height - 1));
        assert(command == 't');
        command = 0;
        click(tablet_pixel(42, 1023, width - 1), tablet_pixel(312, 767, height - 1));
        assert(command == 'f');
        target_position = 2U;
        assert(desktop_dock_order(2U, 7U, &target_position) == Status::Ok);
        assert(create_window(KU_FLUX_TITLE_FILES, 2, {120, 90, 600, 450},
            nullptr, nullptr, nullptr, &files) == Status::Ok);
        command = 0;
        click(tablet_pixel(42, 1023, width - 1), tablet_pixel(312, 767, height - 1));
        assert(command == 't');

        // Home is on the launch button, not in the task list. An app tab
        // must switch to its real WindowId, not synthesize a key.
        click(rail_center, layout.signal_spine.y + 6 + 36);
        assert(focused_window() == home);
        click(78 + 72, layout.pulse_ribbon.y + 22);
        assert(focused_window() == files);
        click(34, layout.pulse_ribbon.y + 22);
        assert(focused_window() == home);

        const uint32_t scales[] = {100U, 125U, 150U, 200U};
        for (const uint32_t scale : scales) {
            ui::g_host_scale = scale;
            const auto first = dock_pin_rect(0U);
            const auto vault = dock_pin_rect(2U);
            const auto last = dock_pin_rect(3U);
            assert(first.height >= 72);
            assert(first.y + first.height == vault.y);
            assert(last.y + last.height <= layout.signal_spine.y +
                layout.signal_spine.height - ui::kSideDockFooterHeight);
            click(rail_center, first.y + 12);
            assert(focused_window() == home);
            click(rail_center, vault.y + vault.height - 1);
            assert(focused_window() == files);
            if (scale > 100U) assert(vault.height > 72);
        }
        ui::g_host_scale = 100U;

        // Classic remains the compact floating-dock fallback, even at 1280.
        ui::g_host_style = KU_FLUX_STYLE_CLASSIC;
        assert(workspace_geometry().pulse_ribbon.width < width);
        ui::g_host_style = KU_FLUX_STYLE_MODERN;
    }

    // At the compact fallback size every pin and maximized task count
    // must still have a usable hit target inside the floating dock.
    assert(initialize(320U, 240U) == Status::Ok);
    WindowId home;
    assert(create_window(KU_FLUX_TITLE_HOME, 1U, {34, 50, 260, 160},
                         nullptr, receive, nullptr, &home) == Status::Ok);
    WindowId tasks[MAX_WINDOWS - 1U]{};
    for (size_t i = 0U; i < MAX_WINDOWS - 1U; ++i)
        assert(create_window("Task", i + 2U, {34, 50, 260, 160},
                             nullptr, nullptr, nullptr, &tasks[i]) == Status::Ok);
    const auto dock = workspace_geometry().pulse_ribbon;
    int32_t right = dock.x;
    for (size_t i = 0U; i < DOCK_PIN_COUNT; ++i) {
        const auto pin = dock_pin_rect(i);
        assert(pin.width > 0 && pin.x >= right);
        right = pin.x + pin.width;
        assert(right <= dock.x + dock.width);
    }
    for (size_t i = 0U; i < MAX_WINDOWS - 1U; ++i) {
        ui::Rect tab{};
        assert(pulse_item_geometry(i, &tab) == Status::Ok);
        assert(tab.width > 0 && tab.x >= right);
        right = tab.x + tab.width;
        assert(right <= dock.x + dock.width);
    }
    ui::Rect last{};
    assert(pulse_item_geometry(MAX_WINDOWS - 2U, &last) == Status::Ok);
    assert(focus(home) == Status::Ok);
    click(last.x + last.width / 2, last.y + last.height / 2);
    assert(focused_window() == tasks[MAX_WINDOWS - 2U]);
}
