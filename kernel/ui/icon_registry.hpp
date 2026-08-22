#pragma once

#include <kurogane/icons.h>

#include <stddef.h>
#include <stdint.h>

namespace ui::icons {

enum class Category : uint8_t {
    Application = KU_ICON_CATEGORY_APPLICATION,
    Folder = KU_ICON_CATEGORY_FOLDER,
    FileType = KU_ICON_CATEGORY_FILE_TYPE,
    Device = KU_ICON_CATEGORY_DEVICE,
    Status = KU_ICON_CATEGORY_STATUS,
    Action = KU_ICON_CATEGORY_ACTION,
    Navigation = KU_ICON_CATEGORY_NAVIGATION,
    Widget = KU_ICON_CATEGORY_WIDGET,
    Cursor = KU_ICON_CATEGORY_CURSOR,
    Special = KU_ICON_CATEGORY_SPECIAL,
    Branding = KU_ICON_CATEGORY_BRANDING,
    Micro = KU_ICON_CATEGORY_MICRO,
    KuroganeApp = KU_ICON_CATEGORY_KUROGANE_APP,
};

enum class Cursor : uint8_t {
    Default = 0,
    Pointer,
    Hand,
    Text,
    Working,
    Busy,
    Move,
    Resize,
    Help,
    NotAllowed,
};

struct IconAsset {
    ku_icon_id_t id;
    Category category;
    uint8_t width;
    uint8_t height;
    const char* name;
    const char* source_path;
    const uint32_t* argb;
};

const IconAsset* find(ku_icon_id_t id);
bool valid(ku_icon_id_t id);
size_t count();
size_t count(Category category);
const char* pack_name();
const char* manifest_sha256();
ku_icon_id_t cursor(Cursor cursor);

void draw(ku_icon_id_t id, int32_t x, int32_t y, int32_t width, int32_t height);

} // namespace ui::icons
