#ifndef KUROGANE_LIBUI_H
#define KUROGANE_LIBUI_H

#include <kurogane/icons.h>
#include <kurogane/ui.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KUI_MAX_VIEWS 32U
#define KUI_VIEW_TEXT_CAPACITY KU_UI_LINE_CAPACITY

enum kui_view_type {
    KUI_VIEW_PANEL = 1,
    KUI_VIEW_LABEL = 2,
    KUI_VIEW_BUTTON = 3,
    KUI_VIEW_INPUT = 4,
    KUI_VIEW_LIST_ITEM = 5,
    KUI_VIEW_PROGRESS = 6,
    KUI_VIEW_SEPARATOR = 7
};

enum kui_view_flags {
    KUI_VIEW_HIDDEN = UINT32_C(1) << 0,
    KUI_VIEW_SELECTED = UINT32_C(1) << 1,
    KUI_VIEW_DISABLED = UINT32_C(1) << 2
};

typedef struct kui_view {
    uint32_t id;
    uint32_t parent_id;
    uint32_t type;
    uint32_t flags;
    uint32_t value;
    uint32_t maximum;
    uint32_t icon_id;
    uint32_t reserved;
    char text[KUI_VIEW_TEXT_CAPACITY];
} kui_view;

typedef struct kui_scene {
    uint32_t background_rgb;
    uint32_t foreground_rgb;
    uint32_t accent_rgb;
    uint32_t view_count;
    uint32_t scroll_offset;
    uint32_t visible_rows;
    uint32_t selected_id;
    uint32_t cursor;
    kui_view views[KUI_MAX_VIEWS];
} kui_scene;

typedef struct kui_flow {
    kui_scene* scene;
    uint32_t parent_id;
} kui_flow;

typedef struct kui_bounds {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} kui_bounds;

/*
 * Optional KuroganeOS 5 spatial layout. This uses the ABI-v2 compatible
 * packing defined in kurogane/ui.h; old flow scenes remain unchanged.
 */
static inline int kui_view_has_bounds(const kui_view* view) {
    return view != (const kui_view*)0 &&
        view->type != KUI_VIEW_PROGRESS &&
        (view->value & KU_UI_LAYOUT_ABSOLUTE) != 0U &&
        KU_UI_LAYOUT_WIDTH(view->maximum) > 0 &&
        KU_UI_LAYOUT_HEIGHT(view->maximum) > 0;
}

static inline kui_bounds kui_view_bounds(const kui_view* view) {
    kui_bounds bounds = {0, 0, 0, 0};
    if (!kui_view_has_bounds(view)) return bounds;
    bounds.x = KU_UI_LAYOUT_X(view->value);
    bounds.y = KU_UI_LAYOUT_Y(view->value);
    bounds.width = KU_UI_LAYOUT_WIDTH(view->maximum);
    bounds.height = KU_UI_LAYOUT_HEIGHT(view->maximum);
    return bounds;
}

static inline ku_status_t kui_scene_set_bounds(
    kui_scene* scene,
    uint32_t id,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height) {
    uint32_t index;
    if (scene == (kui_scene*)0 || id == 0U ||
        x < 0 || x > 32767 || y < 0 || y > 65535 ||
        width <= 0 || width > 65535 || height <= 0 || height > 65535) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < scene->view_count; ++index) {
        kui_view* view = &scene->views[index];
        if (view->id != id) continue;
        if (view->type == KUI_VIEW_PROGRESS) return KU_STATUS_NOT_SUPPORTED;
        view->value = KU_UI_LAYOUT_PACK_POSITION(x, y);
        view->maximum = KU_UI_LAYOUT_PACK_SIZE(width, height);
        return KU_STATUS_OK;
    }
    return KU_STATUS_NOT_FOUND;
}

static inline ku_status_t kui_scene_clear_bounds(kui_scene* scene, uint32_t id) {
    uint32_t index;
    if (scene == (kui_scene*)0 || id == 0U) return KU_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < scene->view_count; ++index) {
        kui_view* view = &scene->views[index];
        if (view->id != id) continue;
        if (view->type == KUI_VIEW_PROGRESS) return KU_STATUS_NOT_SUPPORTED;
        view->value = 0U;
        view->maximum = 0U;
        return KU_STATUS_OK;
    }
    return KU_STATUS_NOT_FOUND;
}

void kui_frame_initialize(ku_ui_frame* frame);
ku_status_t kui_frame_set_line(
    ku_ui_frame* frame, uint32_t line, const char* text);
ku_status_t kui_present(ku_window_t window, const ku_ui_frame* frame);
int kui_next_event(ku_window_t window, ku_ui_event* event);

void kui_scene_initialize(kui_scene* scene);
void kui_scene_set_palette(
    kui_scene* scene,
    uint32_t background_rgb,
    uint32_t foreground_rgb,
    uint32_t accent_rgb);
ku_status_t kui_scene_add(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    uint32_t type,
    const char* text);
ku_status_t kui_scene_add_progress(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    const char* text,
    uint32_t value,
    uint32_t maximum);
ku_status_t kui_scene_set_text(
    kui_scene* scene, uint32_t id, const char* text);
ku_status_t kui_scene_set_flags(
    kui_scene* scene, uint32_t id, uint32_t flags);
ku_status_t kui_scene_set_value(
    kui_scene* scene, uint32_t id, uint32_t value, uint32_t maximum);
ku_status_t kui_scene_set_icon(
    kui_scene* scene, uint32_t id, ku_icon_id_t icon_id);
ku_status_t kui_scene_set_cursor(kui_scene* scene, uint32_t cursor);
ku_status_t kui_scene_scroll(kui_scene* scene, int32_t delta);
ku_status_t kui_scene_select(kui_scene* scene, uint32_t id);
ku_status_t kui_scene_select_next(kui_scene* scene, int32_t direction);
uint32_t kui_scene_selected(const kui_scene* scene);
ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene);

void kui_flow_begin(kui_flow* flow, kui_scene* scene, uint32_t parent_id);
ku_status_t kui_flow_panel(kui_flow* flow, uint32_t id, const char* text);
ku_status_t kui_flow_label(kui_flow* flow, uint32_t id, const char* text);
ku_status_t kui_flow_button(kui_flow* flow, uint32_t id, const char* text);
ku_status_t kui_flow_input(kui_flow* flow, uint32_t id, const char* text);
ku_status_t kui_flow_list_item(kui_flow* flow, uint32_t id, const char* text);
ku_status_t kui_flow_progress(
    kui_flow* flow,
    uint32_t id,
    const char* text,
    uint32_t value,
    uint32_t maximum);
ku_status_t kui_flow_separator(kui_flow* flow, uint32_t id);

ku_status_t kui_flow_panel_icon(
    kui_flow* flow, uint32_t id, const char* text, ku_icon_id_t icon_id);
ku_status_t kui_flow_label_icon(
    kui_flow* flow, uint32_t id, const char* text, ku_icon_id_t icon_id);
ku_status_t kui_flow_button_icon(
    kui_flow* flow, uint32_t id, const char* text, ku_icon_id_t icon_id);
ku_status_t kui_flow_input_icon(
    kui_flow* flow, uint32_t id, const char* text, ku_icon_id_t icon_id);
ku_status_t kui_flow_list_item_icon(
    kui_flow* flow, uint32_t id, const char* text, ku_icon_id_t icon_id);
ku_status_t kui_flow_progress_icon(
    kui_flow* flow, uint32_t id, const char* text,
    uint32_t value, uint32_t maximum, ku_icon_id_t icon_id);

#ifdef __cplusplus
}
#endif
#endif
