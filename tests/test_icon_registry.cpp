#include "../kernel/ui/icon_registry.hpp"

#include <cstring>

namespace {
size_t g_blended_pixels = 0U;
int32_t g_min_x = 100000;
int32_t g_min_y = 100000;
int32_t g_max_x = -1;
int32_t g_max_y = -1;
} // namespace

namespace graphics {
void blend_pixel(int32_t x, int32_t y, uint32_t, uint8_t alpha) {
    if (alpha == 0U) return;
    ++g_blended_pixels;
    if (x < g_min_x) g_min_x = x;
    if (y < g_min_y) g_min_y = y;
    if (x > g_max_x) g_max_x = x;
    if (y > g_max_y) g_max_y = y;
}
} // namespace graphics

int main() {
    using namespace ui::icons;
    if (count() != 350U ||
        count(Category::Application) != 20U ||
        count(Category::Folder) != 20U ||
        count(Category::FileType) != 30U ||
        count(Category::Device) != 20U ||
        count(Category::Status) != 30U ||
        count(Category::Action) != 30U ||
        count(Category::Navigation) != 30U ||
        count(Category::Widget) != 30U ||
        count(Category::Cursor) != 20U ||
        count(Category::Special) != 30U ||
        count(Category::Branding) != 20U ||
        count(Category::Micro) != 60U ||
        count(Category::KuroganeApp) != 10U) return 1;

    const IconAsset* vault = find(KU_ICON_KUROGANE_APP_VAULT_FILE_MANAGER);
    if (vault == nullptr || vault->width != 24U || vault->height != 24U ||
        std::strcmp(vault->name, "vault_file_manager") != 0 ||
        std::strcmp(
            vault->source_path,
            "icons/13_kurogane_apps/24x24/vault_file_manager.png") != 0) return 2;
    if (!valid(KU_ICON_NONE) || !valid(KU_ICON_FOLDER_DOCUMENTS) ||
        valid(UINT16_C(0xFFFF))) return 3;

    if (cursor(Cursor::Default) != KU_ICON_CURSOR_DEFAULT ||
        cursor(Cursor::Pointer) != KU_ICON_CURSOR_POINTER ||
        cursor(Cursor::Hand) != KU_ICON_CURSOR_HAND ||
        cursor(Cursor::Text) != KU_ICON_CURSOR_TEXT ||
        cursor(Cursor::Working) != KU_ICON_CURSOR_WAIT ||
        cursor(Cursor::Busy) != KU_ICON_CURSOR_BUSY ||
        cursor(Cursor::Move) != KU_ICON_CURSOR_MOVE ||
        cursor(Cursor::Resize) != KU_ICON_CURSOR_RESIZE ||
        cursor(Cursor::Help) != KU_ICON_CURSOR_HELP ||
        cursor(Cursor::NotAllowed) != KU_ICON_CURSOR_NOT_ALLOWED) return 4;

    draw(KU_ICON_KUROGANE_APP_BLADE_LAUNCHER, 10, 20, 32, 32);
    if (g_blended_pixels == 0U || g_min_x < 10 || g_min_y < 20 ||
        g_max_x >= 42 || g_max_y >= 52) return 5;
    if (std::strstr(pack_name(), "KuroganeOS 5.0") == nullptr ||
        std::strlen(manifest_sha256()) != 64U) return 6;
    return 0;
}
