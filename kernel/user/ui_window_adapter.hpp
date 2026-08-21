#pragma once

#include "../ui/window_manager.hpp"

namespace user::runtime::ui_adapter {

windowing::Status create_window(
    const char* title,
    uint64_t owner_pid,
    const ui::Rect& bounds,
    windowing::DrawCallback draw,
    windowing::InputCallback input_callback,
    void* context,
    windowing::WindowId* out_id);

} // namespace user::runtime::ui_adapter
