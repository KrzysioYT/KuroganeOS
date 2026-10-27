#include <cassert>
#include <cstdlib>
#include "../kernel/ui/window_manager.cpp"

namespace {
char command;
void receive(windowing::WindowId, const input::Event& event, void*) {
    if (event.type == input::EventType::KeyDown) command = event.character;
}
void click(int x, int y, int width, int height) {
    input::Event event{};
    event.x = static_cast<int>((static_cast<int64_t>(x) * 32767 / 1023) * (width - 1) / 32767);
    event.y = static_cast<int>((static_cast<int64_t>(y) * 32767 / 767) * (height - 1) / 32767);
    event.type = input::EventType::MouseMove;
    assert(windowing::dispatch(event) == windowing::Status::Ok);
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left; event.buttons = drivers::mouse::Left;
    assert(windowing::dispatch(event) == windowing::Status::Ok);
    event.type = input::EventType::MouseButtonUp; event.buttons = 0;
    assert(windowing::dispatch(event) == windowing::Status::Ok);
}
}
int main(int argc, char** argv) {
    using namespace windowing;
    const int terminal_x = argc > 1 ? std::atoi(argv[1]) : 367;
    const int widths[] = {1024, 1280};
    for (int width : widths) {
        const int height = width == 1024 ? 768 : 800;
        assert(initialize(width, height) == Status::Ok);
        WindowId home, files;
        assert(create_window(KU_FLUX_TITLE_HOME, 1, {40, 50, 600, 450},
            nullptr, receive, nullptr, &home) == Status::Ok);
        command = 0;
        click(450, 726, width, height);
        assert(command == 'f');
        assert(create_window(KU_FLUX_TITLE_FILES, 2, {80, 80, 600, 450},
            nullptr, nullptr, nullptr, &files) == Status::Ok);
        command = 0;
        click(terminal_x, 726, width, height);
        assert(command == 't');
    }
}
