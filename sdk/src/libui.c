#include <kurogane/libui.h>
#include <string.h>

static void append_text(char* destination, size_t capacity, const char* source) {
    size_t used;
    if (destination == (char*)0 || source == (const char*)0 || capacity == 0U) return;
    used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

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

static int geometry_valid(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint32_t radius) {
    if (width == 0 && height == 0) return radius == 0U;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return 0;
    if (x < -4096 || x > 4096 || y < -4096 || y > 4096) return 0;
    if (radius > 48U || radius * 2U > (uint32_t)width ||
        radius * 2U > (uint32_t)height) return 0;
    return 1;
}

static int line_style_valid(const ku_ui_line_style* style) {
    const uint32_t known_flags =
        KU_UI_LINE_STYLE_INHERIT_COLORS |
        KU_UI_LINE_STYLE_TRANSPARENT_BACKGROUND |
        KU_UI_LINE_STYLE_DOCUMENT_CONTENT |
        KU_UI_LINE_STYLE_SELECTED |
        KU_UI_LINE_STYLE_MUTED |
        KU_UI_LINE_STYLE_ACCENT;
    if (style == (const ku_ui_line_style*)0 ||
        !ku_text_style_valid(&style->text) ||
        style->reserved != 0U ||
        (style->flags & ~known_flags) != 0U ||
        style->visual_role > KU_UI_VISUAL_CARD ||
        !geometry_valid(
            style->layout_x,
            style->layout_y,
            style->layout_width,
            style->layout_height,
            style->corner_radius)) {
        return 0;
    }
    return 1;
}

static int explicit_geometry(const kui_view* view) {
    return view != (const kui_view*)0 && view->width > 0 && view->height > 0;
}

static int interactive_view(const kui_view* view) {
    if (view == (const kui_view*)0 || (view->flags & KUI_VIEW_DISABLED) != 0U ||
        (view->flags & KUI_VIEW_HIDDEN) != 0U) return 0;
    return view->type == KUI_VIEW_BUTTON || view->type == KUI_VIEW_INPUT ||
        view->type == KUI_VIEW_LIST_ITEM;
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

static uint32_t visual_role_for_view(const kui_view* view) {
    if (view == (const kui_view*)0) return KU_UI_VISUAL_TEXT;
    switch (view->type) {
        case KUI_VIEW_PANEL: return KU_UI_VISUAL_PANEL;
        case KUI_VIEW_BUTTON: return KU_UI_VISUAL_BUTTON;
        case KUI_VIEW_INPUT: return KU_UI_VISUAL_INPUT;
        case KUI_VIEW_LIST_ITEM: return KU_UI_VISUAL_LIST_ITEM;
        case KUI_VIEW_PROGRESS: return KU_UI_VISUAL_PROGRESS;
        case KUI_VIEW_SEPARATOR: return KU_UI_VISUAL_SEPARATOR;
        case KUI_VIEW_CARD: return KU_UI_VISUAL_CARD;
        case KUI_VIEW_LABEL:
        default: return KU_UI_VISUAL_TEXT;
    }
}

static void render_view_line(
    const kui_scene* scene,
    const kui_view* view,
    char* line,
    size_t capacity) {
    uint32_t depth;
    const int geometry = explicit_geometry(view);
    if (line == (char*)0 || capacity == 0U || view == (const kui_view*)0) return;
    line[0] = '\0';

    /*
     * Geometry-aware KUI transports semantic state beside the text. Legacy
     * scenes retain the old indentation/prefixes for source compatibility.
     */
    if (!geometry) {
        depth = view_depth(scene, view);
        while (depth != 0U) {
            append_text(line, capacity, "  ");
            --depth;
        }
    }

    switch (view->type) {
        case KUI_VIEW_PANEL:
        case KUI_VIEW_LABEL:
        case KUI_VIEW_CARD:
        case KUI_VIEW_PROGRESS:
            append_text(line, capacity, view->text);
            break;
        case KUI_VIEW_BUTTON:
        case KUI_VIEW_INPUT:
        case KUI_VIEW_LIST_ITEM:
            if (!geometry) {
                append_text(line, capacity,
                    (view->flags & KUI_VIEW_SELECTED) != 0U ? "> " : "  ");
            }
            append_text(line, capacity, view->text);
            break;
        case KUI_VIEW_SEPARATOR:
            if (!geometry) append_text(line, capacity, "----------------------------");
            break;
        default:
            append_text(line, capacity, "INVALID VIEW");
            break;
    }
}

void kui_line_style_initialize(ku_ui_line_style* style, uint32_t text_context) {
    if (style == (ku_ui_line_style*)0) return;
    memset(style, 0, sizeof(*style));
    style->text = ku_text_default_style(text_context);
    style->flags = KU_UI_LINE_STYLE_INHERIT_COLORS;
    style->visual_role = KU_UI_VISUAL_TEXT;
}

void kui_frame_initialize(ku_ui_frame* frame) {
    uint32_t index;
    if (frame == (ku_ui_frame*)0) return;
    memset(frame, 0, sizeof(*frame));
    frame->structure_size = sizeof(*frame);
    frame->background_rgb = UINT32_C(0x0F1419);
    frame->foreground_rgb = UINT32_C(0xF0F2F4);
    frame->accent_rgb = UINT32_C(0xC0332F);
    for (index = 0U; index < KU_UI_MAX_LINES; ++index) {
        kui_line_style_initialize(
            &frame->line_styles[index], KU_TEXT_CONTEXT_SYSTEM_UI);
    }
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

ku_status_t kui_frame_set_line_style(
    ku_ui_frame* frame, uint32_t line, const ku_ui_line_style* style) {
    if (frame == (ku_ui_frame*)0 || line >= KU_UI_MAX_LINES ||
        !line_style_valid(style)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    frame->line_styles[line] = *style;
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
    scene->background_rgb = UINT32_C(0x0F1419);
    scene->foreground_rgb = UINT32_C(0xF0F2F4);
    scene->accent_rgb = UINT32_C(0xC0332F);
    scene->visible_rows = KU_UI_MAX_LINES;
    kui_line_style_initialize(
        &scene->default_style, KU_TEXT_CONTEXT_SYSTEM_UI);
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

ku_status_t kui_scene_set_default_style(
    kui_scene* scene, const ku_ui_line_style* style) {
    if (scene == (kui_scene*)0 || !line_style_valid(style)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    scene->default_style = *style;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_add(
    kui_scene* scene,
    uint32_t id,
    uint32_t parent_id,
    uint32_t type,
    const char* text) {
    kui_view* view;
    if (scene == (kui_scene*)0 || id == 0U || text == (const char*)0 ||
        type < KUI_VIEW_PANEL || type > KUI_VIEW_CARD) {
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
    view->style = scene->default_style;
    if (strlcpy(view->text, text, sizeof(view->text)) >= sizeof(view->text)) {
        --scene->view_count;
        memset(view, 0, sizeof(*view));
        return KU_STATUS_OUT_OF_RANGE;
    }
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

ku_status_t kui_scene_set_text(
    kui_scene* scene, uint32_t id, const char* text) {
    kui_view* view = find_view(scene, id);
    if (view == (kui_view*)0 || text == (const char*)0) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    return strlcpy(view->text, text, sizeof(view->text)) < sizeof(view->text)
        ? KU_STATUS_OK : KU_STATUS_OUT_OF_RANGE;
}

ku_status_t kui_scene_set_style(
    kui_scene* scene, uint32_t id, const ku_ui_line_style* style) {
    kui_view* view = find_view(scene, id);
    if (view == (kui_view*)0) return KU_STATUS_NOT_FOUND;
    if (!line_style_valid(style)) return KU_STATUS_INVALID_ARGUMENT;
    view->style = *style;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_set_bounds(
    kui_scene* scene,
    uint32_t id,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint32_t corner_radius) {
    kui_view* view = find_view(scene, id);
    if (view == (kui_view*)0) return KU_STATUS_NOT_FOUND;
    if (!geometry_valid(x, y, width, height, corner_radius) || width == 0) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    view->x = x;
    view->y = y;
    view->width = width;
    view->height = height;
    view->corner_radius = corner_radius;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_set_flags(
    kui_scene* scene, uint32_t id, uint32_t flags) {
    kui_view* view = find_view(scene, id);
    if (view == (kui_view*)0) return KU_STATUS_NOT_FOUND;
    view->flags = flags &
        (KUI_VIEW_HIDDEN | KUI_VIEW_SELECTED | KUI_VIEW_DISABLED |
         KUI_VIEW_MUTED | KUI_VIEW_ACCENT);
    if ((view->flags & KUI_VIEW_SELECTED) != 0U) scene->selected_id = id;
    else if (scene->selected_id == id) scene->selected_id = 0U;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_set_value(
    kui_scene* scene, uint32_t id, uint32_t value, uint32_t maximum) {
    kui_view* view = find_view(scene, id);
    if (view == (kui_view*)0 || view->type != KUI_VIEW_PROGRESS || maximum == 0U) {
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

ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene) {
    ku_ui_frame frame;
    uint32_t index;
    uint32_t visible_index = 0U;
    uint32_t output_line = 0U;
    uint32_t rows;
    if (scene == (const kui_scene*)0) return KU_STATUS_INVALID_ARGUMENT;
    kui_frame_initialize(&frame);
    frame.background_rgb = scene->background_rgb;
    frame.foreground_rgb = scene->foreground_rgb;
    frame.accent_rgb = scene->accent_rgb;
    rows = scene->visible_rows == 0U || scene->visible_rows > KU_UI_MAX_LINES
        ? KU_UI_MAX_LINES : scene->visible_rows;

    for (index = 0U; index < scene->view_count && output_line < rows; ++index) {
        const kui_view* view = &scene->views[index];
        char line[KU_UI_LINE_CAPACITY];
        ku_ui_line_style style;
        ku_status_t status;
        if ((view->flags & KUI_VIEW_HIDDEN) != 0U) continue;
        if (visible_index++ < scene->scroll_offset) continue;
        render_view_line(scene, view, line, sizeof(line));
        status = kui_frame_set_line(&frame, output_line, line);
        if (status != KU_STATUS_OK) return KU_STATUS_OUT_OF_RANGE;

        style = view->style;
        style.layout_x = view->x;
        style.layout_y = view->y;
        style.layout_width = view->width;
        style.layout_height = view->height;
        style.corner_radius = view->corner_radius;
        style.visual_role = visual_role_for_view(view);
        if ((view->flags & KUI_VIEW_SELECTED) != 0U) {
            style.flags |= KU_UI_LINE_STYLE_SELECTED;
        }
        if ((view->flags & (KUI_VIEW_DISABLED | KUI_VIEW_MUTED)) != 0U) {
            style.flags |= KU_UI_LINE_STYLE_MUTED;
        }
        if ((view->flags & KUI_VIEW_ACCENT) != 0U) {
            style.flags |= KU_UI_LINE_STYLE_ACCENT;
        }
        status = kui_frame_set_line_style(&frame, output_line, &style);
        if (status != KU_STATUS_OK) return status;
        ++output_line;
        if (view->type == KUI_VIEW_PROGRESS && view->maximum != 0U) {
            frame.progress_value = view->value;
            frame.progress_maximum = view->maximum;
        }
    }
    return kui_present(window, &frame);
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

ku_status_t kui_flow_card(kui_flow* flow, uint32_t id, const char* text) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add(flow->scene, id, flow->parent_id, KUI_VIEW_CARD, text);
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

ku_status_t kui_flow_separator(kui_flow* flow, uint32_t id) {
    return flow == (kui_flow*)0
        ? KU_STATUS_INVALID_ARGUMENT
        : kui_scene_add(flow->scene, id, flow->parent_id,
            KUI_VIEW_SEPARATOR, "");
}
