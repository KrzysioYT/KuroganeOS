#pragma once

#include "ui.hpp"

#include <kurogane/ui.h>

namespace ui {

// ABI-v2 dispatcher. Scenes without absolute bounds keep the existing Forged
// flow renderer; scenes that opt into spatial hints use content-local geometry.
void spatial_surface(
    const Rect& bounds,
    const ku_ui_surface& surface,
    bool focused);

} // namespace ui
