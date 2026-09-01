#pragma once

#include "../ui/window_manager.hpp"

namespace windowing {

Status create_ring3_window(
    const char* title,
    uint64_t owner_pid,
    const ui::Rect& bounds,
    DrawCallback draw,
    InputCallback input_callback,
    void* context,
    WindowId* out_id);

} // namespace windowing
