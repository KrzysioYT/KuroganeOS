#include "icon_registry.hpp"

#include "../drivers/framebuffer.hpp"

namespace ui::icons {
namespace {

#include "generated/icon_registry_data.inc"

} // namespace

const IconAsset* find(ku_icon_id_t id) {
    if (id == KU_ICON_NONE) return nullptr;
    for (size_t index = 0U; index < kGeneratedIconCount; ++index) {
        if (kGeneratedIcons[index].id == id) return &kGeneratedIcons[index];
    }
    return nullptr;
}

bool valid(ku_icon_id_t id) {
    return id == KU_ICON_NONE || find(id) != nullptr;
}

size_t count() { return kGeneratedIconCount; }

size_t count(Category category) {
    size_t result = 0U;
    for (size_t index = 0U; index < kGeneratedIconCount; ++index) {
        if (kGeneratedIcons[index].category == category) ++result;
    }
    return result;
}

const char* pack_name() { return kGeneratedPackName; }

const char* manifest_sha256() { return kGeneratedManifestSha256; }

ku_icon_id_t cursor(Cursor shape) {
    switch (shape) {
        case Cursor::Default: return KU_ICON_CURSOR_DEFAULT;
        case Cursor::Pointer: return KU_ICON_CURSOR_POINTER;
        case Cursor::Hand: return KU_ICON_CURSOR_HAND;
        case Cursor::Text: return KU_ICON_CURSOR_TEXT;
        case Cursor::Working: return KU_ICON_CURSOR_WAIT;
        case Cursor::Busy: return KU_ICON_CURSOR_BUSY;
        case Cursor::Move: return KU_ICON_CURSOR_MOVE;
        case Cursor::Resize: return KU_ICON_CURSOR_RESIZE;
        case Cursor::Help: return KU_ICON_CURSOR_HELP;
        case Cursor::NotAllowed: return KU_ICON_CURSOR_NOT_ALLOWED;
    }
    return KU_ICON_CURSOR_DEFAULT;
}

void draw(ku_icon_id_t id, int32_t x, int32_t y, int32_t width, int32_t height) {
    const IconAsset* asset = find(id);
    if (asset == nullptr || asset->argb == nullptr || width <= 0 || height <= 0) return;
    for (int32_t target_y = 0; target_y < height; ++target_y) {
        const uint32_t source_y = static_cast<uint32_t>(target_y) * asset->height /
            static_cast<uint32_t>(height);
        for (int32_t target_x = 0; target_x < width; ++target_x) {
            const uint32_t source_x = static_cast<uint32_t>(target_x) * asset->width /
                static_cast<uint32_t>(width);
            const uint32_t pixel = asset->argb[
                static_cast<size_t>(source_y) * asset->width + source_x];
            const uint8_t alpha = static_cast<uint8_t>(pixel >> 24U);
            if (alpha == 0U) continue;
            graphics::blend_pixel(x + target_x, y + target_y,
                                  pixel & UINT32_C(0x00FFFFFF), alpha);
        }
    }
}

} // namespace ui::icons
