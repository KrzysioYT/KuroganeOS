#ifndef KUROGANE_LIBUI_H
#define KUROGANE_LIBUI_H

#include <kurogane/design.h>
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
    KUI_VIEW_SEPARATOR = 7,
    KUI_VIEW_TILE = 8,
    KUI_VIEW_METRIC = 9,
    KUI_VIEW_NOTICE = 10,
    KUI_VIEW_TOGGLE = 11
};

enum kui_view_flags {
    KUI_VIEW_HIDDEN = UINT32_C(1) << 0,
    KUI_VIEW_SELECTED = UINT32_C(1) << 1,
    KUI_VIEW_DISABLED = UINT32_C(1) << 2,
    KUI_VIEW_PINNED = UINT32_C(1) << 3,
    KUI_VIEW_RUNNING = UINT32_C(1) << 4,
    KUI_VIEW_DESTRUCTIVE = UINT32_C(1) << 5
};

typedef struct kui_view {
    uint32_t id;
    uint32_t parent_id;
    uint32_t type;
    uint32_t flags;
    uint32_t value;
    uint32_t maximum;
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
    uint32_t reserved;
    kui_view views[KUI_MAX_VIEWS];
} kui_scene;

typedef struct kui_flow {
    kui_scene* scene;
    uint32_t parent_id;
} kui_flow;

void kui_frame_initialize(ku_ui_frame* frame);
ku_status_t kui_frame_set_line(
    ku_ui_frame* frame, uint32_t line, const char* text);
ku_status_t kui_present(ku_window_t window, const ku_ui_frame* frame);
int kui_next_event(ku_window_t window, ku_ui_event* event);

void kui_scene_initialize(kui_scene* scene);
void kui_scene_apply_flux_theme(kui_scene* scene);
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
ku_status_t kui_scene_add_tile(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    const char* text,
    uint32_t icon);
ku_status_t kui_scene_add_progress(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    const char* text,
    uint32_t value,
    uint32_t maximum);
ku_status_t kui_scene_add_metric(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    const char* text,
    uint32_t value,
    uint32_t maximum);
ku_status_t kui_scene_add_notice(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    const char* text,
    uint32_t priority);
ku_status_t kui_scene_add_toggle(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    const char* text,
    int checked);
ku_status_t kui_scene_set_text(
    kui_scene* scene, uint32_t id, const char* text);
ku_status_t kui_scene_set_flags(
    kui_scene* scene, uint32_t id, uint32_t flags);
ku_status_t kui_scene_set_value(
    kui_scene* scene, uint32_t id, uint32_t value, uint32_t maximum);
ku_status_t kui_scene_scroll(kui_scene* scene, int32_t delta);
ku_status_t kui_scene_select(kui_scene* scene, uint32_t id);
ku_status_t kui_scene_select_next(kui_scene* scene, int32_t direction);
uint32_t kui_scene_selected(const kui_scene* scene);
// Returns the interactive visible view under a content-local pointer, or 0.
uint32_t kui_scene_hit_test(const kui_scene* scene, int32_t x, int32_t y);
ku_status_t kui_scene_build_native(
    const kui_scene* scene, ku_ui_native_frame* frame);
ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene);

void kui_flow_begin(kui_flow* flow, kui_scene* scene, uint32_t parent_id);
ku_status_t kui_flow_panel(kui_flow* flow, uint32_t id, const char* text);
ku_status_t kui_flow_label(kui_flow* flow, uint32_t id, const char* text);
ku_status_t kui_flow_button(kui_flow* flow, uint32_t id, const char* text);
ku_status_t kui_flow_input(kui_flow* flow, uint32_t id, const char* text);
ku_status_t kui_flow_list_item(kui_flow* flow, uint32_t id, const char* text);
ku_status_t kui_flow_tile(
    kui_flow* flow, uint32_t id, const char* text, uint32_t icon);
ku_status_t kui_flow_progress(
    kui_flow* flow,
    uint32_t id,
    const char* text,
    uint32_t value,
    uint32_t maximum);
ku_status_t kui_flow_metric(
    kui_flow* flow,
    uint32_t id,
    const char* text,
    uint32_t value,
    uint32_t maximum);
ku_status_t kui_flow_notice(
    kui_flow* flow,
    uint32_t id,
    const char* text,
    uint32_t priority);
ku_status_t kui_flow_toggle(
    kui_flow* flow,
    uint32_t id,
    const char* text,
    int checked);
ku_status_t kui_flow_separator(kui_flow* flow, uint32_t id);

#ifdef __cplusplus
}
#endif
#endif
