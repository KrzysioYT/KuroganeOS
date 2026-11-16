#pragma once

#include "ui.hpp"

namespace ui {

struct WindowChromeMetrics {
    uint32_t scale;

    constexpr int32_t pixels(int32_t logical) const {
        return static_cast<int32_t>((static_cast<int64_t>(logical) * scale + 99) / 100);
    }

    constexpr int32_t header_height(int32_t window_height) const {
        // Leave room for a 36px input and 16px of padding in compact windows.
        const int32_t available = window_height - content_inset() - pixels(52);
        const int32_t minimum = control_height();
        const int32_t ideal = pixels(36);
        return available < minimum ? minimum : (available < ideal ? available : ideal);
    }
    constexpr int32_t control_width() const { return pixels(22); }
    constexpr int32_t control_height() const { return pixels(20); }
    constexpr int32_t control_spacing() const { return control_width() + pixels(4); }
    constexpr int32_t control_right() const { return pixels(8); }
    constexpr int32_t content_inset() const { return pixels(4); }
    constexpr int32_t resize_grip() const { return pixels(18); }
};

} // namespace ui
