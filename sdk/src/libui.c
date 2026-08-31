#include <kurogane/libui.h>
#include <string.h>

static kui_view* find_view(kui_scene* scene, uint32_t id) {
    uint32_t index;
    if (scene == (kui_scene*)0 || id == 0U) return (kui_view*)0;
    for (index = 0U; index < scene->view_count; ++index) {
        if (scene->views[index].id == id) return &scene->views[index];
    }
    return (kui_view*)0;
}

static const kui_view* find_view_const(const kui_scene* scene, uint32_t id) {
    uint32_t index;
    if (scene == (const kui_scene*)0 || id == 0U) return (const kui_view*)0;
    for (index = 0U; index < scene->view_count; ++index) {
        if (scene->views[index].id == id) return &scene->views[index];
    }
    return (const kui_view*)0;
}

static int interactive_view(const kui_view* view) {
    if (view == (const kui_view*)0 || (view->flags & KUI_VIEW_DISABLED) != 0U ||
        (view->flags & KUI_VIEW_HIDDEN) != 0U) return 0;
    return view->type == KUI_VIEW_BUTTON || view->type == KUI_VIEW_INPUT ||
        view->type == KUI_VIEW_LIST_ITEM || view->type == KUI_VIEW_TILE;
}

static uint32_t visible_view_count(const kui_scene* scene) {
    uint32_t index;
    uint32_t count = 0U;
    if (scene == (const kui_scene*)0) return 0U;
    for (index = 0U; index < scene->view_count; ++index) {
        if ((scene->views[index].flags & KUI_VIEW_HIDDEN) == 0U) ++count;
    }
    return count;
}

static uint32_t view_depth(const kui_scene* scene, const kui_view* view) {
    uint32_t depth = 0U;
    uint32_t parent;
    if (scene == (const kui_scene*)0 || view == (const kui_view*)0) return 0U;
    parent = view->parent_id;
    while (parent != 0U && depth < 3U) {
        const kui_view* parent_view = find_view_const(scene, parent);
        if (parent_view == (const kui_view*)0) break;
        ++depth;
        parent = parent_view->parent_id;
    }
    return depth;
}

void kui_frame_initialize(ku_ui_frame* frame) {
    if (frame == (ku_ui_frame*)0) return;
    memset(frame, 0, sizeof(*frame));
    frame->structure_size = sizeof(*frame);
    frame->background_rgb = UINT32_C(0x090A0C);
    frame->foreground_rgb = UINT32_C(0xECEEF1);
    frame->accent_rgb = UINT32_C(0xDE192D);
}

ku_status_t kui_frame_set_line(
    ku_ui_frame* frame, uint32_t line, const char* text) {
    if (frame == (ku_ui_frame*)0 || text == (const char*)0 ||
        line >= KU_UI_MAX_LINES) return KU_STATUS_INVALID_ARGUMENT;
    if (strlcpy(frame->lines[line], text, KU_UI_LINE_CAPACITY) >=
        KU_UI_LINE_CAPACITY) return KU_STATUS_OUT_OF_RANGE;
    if (frame->line_count <= line) frame->line_count = line + 1U;
    return KU_STATUS_OK;
}

ku_status_t kui_present(ku_window_t window, const ku_ui_frame* frame) {
    return ku_ui_present(window, frame);
}

int kui_next_event(ku_window_t window, ku_ui_event* event) {
    const ku_status_t status = ku_ui_poll(window, event);
    if (status == KU_STATUS_WOULD_BLOCK) return 0;
    return status == KU_STATUS_OK ? 1 : -1;
}

void kui_scene_initialize(kui_scene* scene) {
    if (scene == (kui_scene*)0) return;
    memset(scene, 0, sizeof(*scene));
    scene->background_rgb = UINT32_C(0x090A0C);
    scene->foreground_rgb = UINT32_C(0xECEEF1);
    scene->accent_rgb = UINT32_C(0xDE192D);
    scene->visible_rows = KU_UI_MAX_LINES;
}

void kui_scene_set_palette(
    kui_scene* scene,
    uint32_t background_rgb,
    uint32_t foreground_rgb,
    uint32_t accent_rgb) {
    if (scene == (kui_scene*)0) return;
    scene->background_rgb = background_rgb & UINT32_C(0xFFFFFF);
    scene->foreground_rgb = foreground_rgb & UINT32_C(0xFFFFFF);
    scene->accent_rgb = accent_rgb & UINT32_C(0xFFFFFF);
}

ku_status_t kui_scene_add(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    uint32_t type,
    const char* text) {
    kui_view* view;
    if (scene == (kui_scene*)0 || id == 0U || text == (const char*)0 ||
        type < KUI_VIEW_PANEL || type > KUI_VIEW_NOTICE) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    if (scene->view_count >= KUI_MAX_VIEWS) return KU_STATUS_OUT_OF_MEMORY;
    if (find_view(scene, id) != (kui_view*)0) return KU_STATUS_BAD_STATE;
    if (parent_id != 0U && find_view(scene, parent_id) == (kui_view*)0) {
        return KU_STATUS_NOT_FOUND;
    }
    view = &scene->views[scene->view_count++];
    memset(view, 0, sizeof(*view));
    view->id = id;
    view->parent_id = parent_id;
    view->type = type;
    if (strlcpy(view->text, text, sizeof(view->text)) >= sizeof(view->text)) {
        --scene->view_count;
        memset(view, 0, sizeof(*view));
        return KU_STATUS_OUT_OF_RANGE;
    }
    return KU_STATUS_OK;
}

ku_status_t kui_scene_add_tile(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    const char* text,
    uint32_t icon) {
    ku_status_t status;
    kui_view* view;
    if (icon >= KU_UI_NATIVE_ICON_COUNT) return KU_STATUS_INVALID_ARGUMENT;
    status = kui_scene_add(scene, id, parent_id, KUI_VIEW_TILE, text);
    if (status != KU_STATUS_OK) return status;
    view = find_view(scene, id);
    if (view == (kui_view*)0) return KU_STATUS_BAD_STATE;
    view->value = icon;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_add_progress(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    const char* text,
    uint32_t value,
    uint32_t maximum) {
    ku_status_t status = kui_scene_add(
        scene, id, parent_id, KUI_VIEW_PROGRESS, text);
    if (status != KU_STATUS_OK) return status;
    return kui_scene_set_value(scene, id, value, maximum);
}

ku_status_t kui_scene_add_metric(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    const char* text,
    uint32_t value,
    uint32_t maximum) {
    ku_status_t status = kui_scene_add(
        scene, id, parent_id, KUI_VIEW_METRIC, text);
    if (status != KU_STATUS_OK) return status;
    return kui_scene_set_value(scene, id, value, maximum);
}

ku_status_t kui_scene_add_notice(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    const char* text,
    uint32_t priority) {
    ku_status_t status;
    if (priority < 1U || priority > 4U) return KU_STATUS_INVALID_ARGUMENT;
    status = kui_scene_add(scene, id, parent_id, KUI_VIEW_NOTICE, text);
    if (status != KU_STATUS_OK) return status;
    return kui_scene_set_value(scene, id, priority, 4U);
}

ku_status_t kui_scene_set_text(
    kui_scene* scene, uint32_t id, const char* text) {
    kui_view* view = find_view(scene, id);
    if (view == (kui_view*)0 || text == (const char*)0) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    return strlcpy(view->text, text, sizeof(view->text)) < sizeof(view->text)
        ? KU_STATUS_OK : KU_STATUS_OUT_OF_RANGE;
}

ku_status_t kui_scene_set_flags(
    kui_scene* scene, uint32_t id, uint32_t flags) {
    kui_view* view = find_view(scene, id);
    if (view == (kui_view*)0) return KU_STATUS_NOT_FOUND;
    view->flags = flags &
        (KUI_VIEW_HIDDEN | KUI_VIEW_SELECTED | KUI_VIEW_DISABLED |
         KUI_VIEW_PINNED | KUI_VIEW_RUNNING);
    if ((view->flags & KUI_VIEW_SELECTED) != 0U) scene->selected_id = id;
    else if (scene->selected_id == id) scene->selected_id = 0U;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_set_value(
    kui_scene* scene, uint32_t id, uint32_t value, uint32_t maximum) {
    kui_view* view = find_view(scene, id);
    if (view == (kui_view*)0 ||
        (view->type != KUI_VIEW_PROGRESS && view->type != KUI_VIEW_METRIC &&
         view->type != KUI_VIEW_NOTICE) ||
        maximum == 0U) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    view->maximum = maximum;
    view->value = value > maximum ? maximum : value;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_scroll(kui_scene* scene, int32_t delta) {
    uint32_t count;
    uint32_t rows;
    uint32_t maximum;
    int64_t next;
    if (scene == (kui_scene*)0) return KU_STATUS_INVALID_ARGUMENT;
    count = visible_view_count(scene);
    rows = scene->visible_rows == 0U ? KU_UI_MAX_LINES : scene->visible_rows;
    maximum = count > rows ? count - rows : 0U;
    next = (int64_t)scene->scroll_offset + (int64_t)delta;
    if (next < 0) next = 0;
    if ((uint64_t)next > maximum) next = maximum;
    scene->scroll_offset = (uint32_t)next;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_select(kui_scene* scene, uint32_t id) {
    uint32_t index;
    kui_view* target;
    if (scene == (kui_scene*)0) return KU_STATUS_INVALID_ARGUMENT;
    target = find_view(scene, id);
    if (!interactive_view(target)) return KU_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < scene->view_count; ++index) {
        scene->views[index].flags &= ~KUI_VIEW_SELECTED;
    }
    target->flags |= KUI_VIEW_SELECTED;
    scene->selected_id = id;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_select_next(kui_scene* scene, int32_t direction) {
    uint32_t interactive[KUI_MAX_VIEWS];
    uint32_t count = 0U;
    uint32_t index;
    uint32_t current = 0U;
    if (scene == (kui_scene*)0 || direction == 0) return KU_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < scene->view_count; ++index) {
        if (interactive_view(&scene->views[index])) interactive[count++] = index;
    }
    if (count == 0U) return KU_STATUS_NOT_FOUND;
    if (scene->selected_id != 0U) {
        for (index = 0U; index < count; ++index) {
            if (scene->views[interactive[index]].id == scene->selected_id) {
                current = index;
                break;
            }
        }
    } else {
        current = direction > 0 ? count - 1U : 0U;
    }
    if (direction > 0) current = (current + 1U) % count;
    else current = current == 0U ? count - 1U : current - 1U;
    return kui_scene_select(scene, scene->views[interactive[current]].id);
}

uint32_t kui_scene_selected(const kui_scene* scene) {
    return scene == (const kui_scene*)0 ? 0U : scene->selected_id;
}

typedef struct kui_native_layout {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} kui_native_layout;

typedef struct kui_native_layout_state {
    int32_t cursor_y;
    uint32_t tile_column;
    uint32_t metric_column;
} kui_native_layout_state;

#define KUI_TILE_COLUMNS 3U
#define KUI_TILE_WIDTH 184
#define KUI_TILE_HEIGHT 68
#define KUI_TILE_GAP_X 12
#define KUI_TILE_GAP_Y 8
#define KUI_METRIC_COLUMNS 5U
#define KUI_METRIC_WIDTH 112
#define KUI_METRIC_HEIGHT 58
#define KUI_METRIC_GAP_X 8
#define KUI_METRIC_GAP_Y 8

static int32_t native_view_height(uint32_t type) {
    switch (type) {
        case KUI_VIEW_PANEL: return 38;
        case KUI_VIEW_LABEL: return 22;
        case KUI_VIEW_BUTTON: return 34;
        case KUI_VIEW_INPUT: return 36;
        case KUI_VIEW_LIST_ITEM: return 36;
        case KUI_VIEW_PROGRESS: return 44;
        case KUI_VIEW_SEPARATOR: return 10;
        case KUI_VIEW_TILE: return KUI_TILE_HEIGHT;
        case KUI_VIEW_METRIC: return KUI_METRIC_HEIGHT;
        case KUI_VIEW_NOTICE: return 72;
        default: return 0;
    }
}

static void native_layout_initialize(kui_native_layout_state* state) {
    state->cursor_y = 16;
    state->tile_column = 0U;
    state->metric_column = 0U;
}

static void native_flush_tiles(kui_native_layout_state* state) {
    if (state->tile_column == 0U) return;
    state->cursor_y += KUI_TILE_HEIGHT + KUI_TILE_GAP_Y;
    state->tile_column = 0U;
}

static void native_flush_metrics(kui_native_layout_state* state) {
    if (state->metric_column == 0U) return;
    state->cursor_y += KUI_METRIC_HEIGHT + KUI_METRIC_GAP_Y;
    state->metric_column = 0U;
}

static void native_layout_view(
    const kui_scene* scene,
    const kui_view* view,
    kui_native_layout_state* state,
    kui_native_layout* output) {
    const uint32_t depth = view_depth(scene, view);
    const int32_t indent = (int32_t)(depth * 12U);
    if (view->type == KUI_VIEW_METRIC) {
        native_flush_tiles(state);
        if (state->metric_column >= KUI_METRIC_COLUMNS) native_flush_metrics(state);
        output->x = 16 + indent +
            (int32_t)state->metric_column * (KUI_METRIC_WIDTH + KUI_METRIC_GAP_X);
        output->y = state->cursor_y;
        output->width = KUI_METRIC_WIDTH;
        output->height = KUI_METRIC_HEIGHT;
        ++state->metric_column;
        if (state->metric_column == KUI_METRIC_COLUMNS) native_flush_metrics(state);
        return;
    }
    if (view->type == KUI_VIEW_TILE) {
        native_flush_metrics(state);
        if (state->tile_column >= KUI_TILE_COLUMNS) native_flush_tiles(state);
        output->x = 16 + indent +
            (int32_t)state->tile_column * (KUI_TILE_WIDTH + KUI_TILE_GAP_X);
        output->y = state->cursor_y;
        output->width = KUI_TILE_WIDTH;
        output->height = KUI_TILE_HEIGHT;
        ++state->tile_column;
        if (state->tile_column == KUI_TILE_COLUMNS) native_flush_tiles(state);
        return;
    }

    native_flush_metrics(state);
    native_flush_tiles(state);
    output->x = 16 + indent;
    output->y = state->cursor_y;
    output->width = 0;
    output->height = native_view_height(view->type);
    state->cursor_y += output->height + 6;
}

static uint32_t native_rows(const kui_scene* scene) {
    if (scene->visible_rows == 0U || scene->visible_rows > KU_UI_NATIVE_MAX_COMMANDS) {
        return KU_UI_NATIVE_MAX_COMMANDS;
    }
    return scene->visible_rows;
}

uint32_t kui_scene_hit_test(const kui_scene* scene, int32_t x, int32_t y) {
    uint32_t visible_index = 0U;
    uint32_t output_index = 0U;
    uint32_t index;
    kui_native_layout_state state;
    const uint32_t rows = scene == (const kui_scene*)0 ? 0U : native_rows(scene);
    if (scene == (const kui_scene*)0 || x < 0 || y < 0 ||
        x > KU_UI_NATIVE_COORD_LIMIT || y > KU_UI_NATIVE_COORD_LIMIT) return 0U;
    native_layout_initialize(&state);

    for (index = 0U; index < scene->view_count && output_index < rows; ++index) {
        const kui_view* view = &scene->views[index];
        kui_native_layout geometry;
        int within_x;
        if ((view->flags & KUI_VIEW_HIDDEN) != 0U) continue;
        if (visible_index++ < scene->scroll_offset) continue;
        native_layout_view(scene, view, &state, &geometry);
        if (geometry.height <= 0) continue;
        within_x = x >= geometry.x &&
            (geometry.width == 0 || x < geometry.x + geometry.width);
        if (interactive_view(view) && within_x &&
            y >= geometry.y && y < geometry.y + geometry.height) {
            return view->id;
        }
        ++output_index;
    }
    return 0U;
}

ku_status_t kui_scene_build_native(
    const kui_scene* scene, ku_ui_native_frame* frame) {
    uint32_t visible_index = 0U;
    uint32_t output_index = 0U;
    uint32_t index;
    uint32_t rows;
    kui_native_layout_state state;
    if (scene == (const kui_scene*)0 || frame == (ku_ui_native_frame*)0) {
        return KU_STATUS_INVALID_ARGUMENT;
    }

    memset(frame, 0, sizeof(*frame));
    frame->structure_size = sizeof(*frame);
    frame->magic = KU_UI_NATIVE_MAGIC;
    frame->version = KU_UI_NATIVE_VERSION;
    frame->background_rgb = scene->background_rgb;
    frame->foreground_rgb = scene->foreground_rgb;
    frame->accent_rgb = scene->accent_rgb;
    rows = native_rows(scene);
    native_layout_initialize(&state);

    for (index = 0U; index < scene->view_count && output_index < rows; ++index) {
        const kui_view* view = &scene->views[index];
        kui_native_layout geometry;
        ku_ui_native_command* command;
        if ((view->flags & KUI_VIEW_HIDDEN) != 0U) continue;
        if (visible_index++ < scene->scroll_offset) continue;
        native_layout_view(scene, view, &state, &geometry);
        if (geometry.height <= 0) return KU_STATUS_CORRUPT_DATA;

        command = &frame->commands[output_index++];
        command->type = view->type;
        if ((view->flags & KUI_VIEW_SELECTED) != 0U) command->flags |= KU_UI_NATIVE_SELECTED;
        if ((view->flags & KUI_VIEW_DISABLED) != 0U) command->flags |= KU_UI_NATIVE_DISABLED;
        if ((view->flags & KUI_VIEW_PINNED) != 0U) command->flags |= KU_UI_NATIVE_PINNED;
        if ((view->flags & KUI_VIEW_RUNNING) != 0U) command->flags |= KU_UI_NATIVE_RUNNING;
        command->x = geometry.x;
        command->y = geometry.y;
        command->width = geometry.width;
        command->height = geometry.height;
        command->foreground_rgb = scene->foreground_rgb;
        command->background_rgb = scene->background_rgb;
        command->accent_rgb = scene->accent_rgb;
        command->value = view->value;
        command->maximum = view->maximum;
        if (strlcpy(command->text, view->text, sizeof(command->text)) >=
            sizeof(command->text)) return KU_STATUS_OUT_OF_RANGE;
    }
    frame->command_count = output_index;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene) {
    ku_ui_native_frame frame;
    const ku_status_t status = kui_scene_build_native(scene, &frame);
    if (status != KU_STATUS_OK) return status;
    return ku_ui_present_native(window, &frame);
}

void kui_flow_begin(kui_flow* flow, kui_scene* scene, uint32_t parent_id) {
    if (flow == (kui_flow*)0) return;
    flow->scene = scene;
    flow->parent_id = parent_id;
}

ku_status_t kui_flow_panel(kui_flow* flow, uint32_t id, const char* text) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add(flow->scene, id, flow->parent_id, KUI_VIEW_PANEL, text);
}

ku_status_t kui_flow_label(kui_flow* flow, uint32_t id, const char* text) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add(flow->scene, id, flow->parent_id, KUI_VIEW_LABEL, text);
}

ku_status_t kui_flow_button(kui_flow* flow, uint32_t id, const char* text) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add(flow->scene, id, flow->parent_id, KUI_VIEW_BUTTON, text);
}

ku_status_t kui_flow_input(kui_flow* flow, uint32_t id, const char* text) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add(flow->scene, id, flow->parent_id, KUI_VIEW_INPUT, text);
}

ku_status_t kui_flow_list_item(kui_flow* flow, uint32_t id, const char* text) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add(flow->scene, id, flow->parent_id, KUI_VIEW_LIST_ITEM, text);
}

ku_status_t kui_flow_tile(
    kui_flow* flow, uint32_t id, const char* text, uint32_t icon) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add_tile(flow->scene, id, flow->parent_id, text, icon);
}

ku_status_t kui_flow_progress(
    kui_flow* flow,
    uint32_t id,
    const char* text,
    uint32_t value,
    uint32_t maximum) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add_progress(
            flow->scene, id, flow->parent_id, text, value, maximum);
}

ku_status_t kui_flow_metric(
    kui_flow* flow,
    uint32_t id,
    const char* text,
    uint32_t value,
    uint32_t maximum) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add_metric(
            flow->scene, id, flow->parent_id, text, value, maximum);
}

ku_status_t kui_flow_notice(
    kui_flow* flow,
    uint32_t id,
    const char* text,
    uint32_t priority) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add_notice(flow->scene, id, flow->parent_id, text, priority);
}

ku_status_t kui_flow_separator(kui_flow* flow, uint32_t id) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add(flow->scene, id, flow->parent_id,
            KUI_VIEW_SEPARATOR, "");
}
