#pragma once

#include <kurogane/ui.h>

#include "../ui/ui.hpp"

namespace user::ui_presenter {

bool style_valid(const ku_ui_line_style& style);
void draw_frame(
    const ku_ui_frame& frame,
    const ui::Rect& content,
    bool focused);

} // namespace user::ui_presenter
