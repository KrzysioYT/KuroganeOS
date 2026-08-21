#include "ui_presenter.hpp"

/*
 * KUI v3 compatibility bridge.
 *
 * Keep the mature syscall/process/filesystem implementation byte-for-byte in
 * runtime_base_legacy.inc. Only window creation is intercepted so user UI
 * frames are rasterized by the geometry-aware presenter. This isolates the
 * desktop modernization from unrelated runtime code while old line-flow apps
 * remain source/binary compatible with the current SDK rebuild.
 */
namespace windowing {
Status create_window_geometry_bridge(
    const char* title,
    uint64_t owner_pid,
    const ui::Rect& bounds,
    DrawCallback draw,
    InputCallback input_callback,
    void* context,
    WindowId* out_id);
} // namespace windowing

/*
 * The legacy runtime validator predates the visual-state bits. Fold them into
 * its known DOCUMENT_CONTENT mask while compiling the frozen include. The
 * public enum is untouched once the include ends.
 */
#define KU_UI_LINE_STYLE_DOCUMENT_CONTENT \
    ((UINT32_C(1) << 2) | (UINT32_C(1) << 3) | (UINT32_C(1) << 4) | \
     (UINT32_C(1) << 5))
#define create_window create_window_geometry_bridge
#include "runtime_base_legacy.inc"
#undef create_window
#undef KU_UI_LINE_STYLE_DOCUMENT_CONTENT

namespace user::runtime {
namespace {

void geometry_draw_user_window(
    windowing::WindowId,
    const ui::Rect& content,
    bool focused,
    void* opaque) {
    auto* context = static_cast<Context*>(opaque);
    if (context == nullptr || !context->active || !context->ui.active) return;
    ui_presenter::draw_frame(context->ui.frame, content, focused);
}

} // namespace
} // namespace user::runtime

namespace windowing {

Status create_window_geometry_bridge(
    const char* title,
    uint64_t owner_pid,
    const ui::Rect& bounds,
    DrawCallback,
    InputCallback input_callback,
    void* context,
    WindowId* out_id) {
    return create_window(
        title,
        owner_pid,
        bounds,
        user::runtime::geometry_draw_user_window,
        input_callback,
        context,
        out_id);
}

} // namespace windowing
