#include <cassert>
#include <cstdlib>
#include "../kernel/ui/window_manager.cpp"

// This test links the window manager without framebuffer/graphics modules.
namespace ui {
uint32_t g_host_style = KU_FLUX_STYLE_MODERN;
uint32_t desktop_style_id() { return g_host_style; }
}

namespace {
char command;
void receive(windowing::WindowId, const input::Event& event, void*) {
    if (event.type == input::EventType::KeyDown) command = event.character;
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
        click(rail_center, layout.signal_spine.y + 6 + 72 + 36);
        assert(command == 'f');
        assert(create_window(KU_FLUX_TITLE_FILES, 2, {120, 90, 600, 450},
            nullptr, nullptr, nullptr, &files) == Status::Ok);
        command = 0;
        click(rail_center, layout.signal_spine.y + 6 + 3 * 72 + 36);
        assert(command == 't');

        // Home is on the launch button, not in the task list. An app tab
        // must switch to its real WindowId, not synthesize a key.
        click(rail_center, layout.signal_spine.y + 6 + 36);
        assert(focused_window() == home);
        click(78 + 72, layout.pulse_ribbon.y + 22);
        assert(focused_window() == files);
        click(34, layout.pulse_ribbon.y + 22);
        assert(focused_window() == home);

        // Classic remains the compact floating-dock fallback, even at 1280.
        ui::g_host_style = KU_FLUX_STYLE_CLASSIC;
        assert(workspace_geometry().pulse_ribbon.width < width);
        ui::g_host_style = KU_FLUX_STYLE_MODERN;
    }
}
