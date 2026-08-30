from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: anchor count={count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    'kernel/drivers/framebuffer.hpp',
    '''using Color = uint32_t;\n\nconstexpr Color rgb''',
    '''using Color = uint32_t;\n\nstruct DamageRect {\n    int32_t x;\n    int32_t y;\n    int32_t width;\n    int32_t height;\n};\n\nconstexpr Color rgb''',
)
replace_once(
    'kernel/drivers/framebuffer.hpp',
    '''bool begin_frame();\nvoid end_frame();\nbool frame_active();\n''',
    '''bool begin_frame();\nvoid end_frame();\nvoid end_frame_regions(const DamageRect* regions, size_t count);\nbool frame_active();\n''',
)

framebuffer = Path('kernel/drivers/framebuffer.cpp')
text = framebuffer.read_text()
start = text.find('void end_frame() {\n')
end = text.find('\nbool frame_active()', start)
if start < 0 or end < 0:
    raise SystemExit('framebuffer end_frame boundaries missing')
replacement = r'''void end_frame_regions(const DamageRect* regions, size_t count) {
    if (!g_available || !g_frame_active) return;

    if (regions != nullptr) {
        auto* framebuffer_bytes = reinterpret_cast<uint8_t*>(g_framebuffer.base);
        const uint32_t frame_width = g_framebuffer.width;
        const uint32_t frame_height = g_framebuffer.height;
        for (size_t region_index = 0U; region_index < count; ++region_index) {
            const DamageRect& region = regions[region_index];
            if (region.width <= 0 || region.height <= 0) continue;
            int64_t left = region.x;
            int64_t top = region.y;
            int64_t right = static_cast<int64_t>(region.x) + region.width;
            int64_t bottom = static_cast<int64_t>(region.y) + region.height;
            if (left < 0) left = 0;
            if (top < 0) top = 0;
            if (right > frame_width) right = frame_width;
            if (bottom > frame_height) bottom = frame_height;
            if (left >= right || top >= bottom) continue;

            const uint32_t x_begin = static_cast<uint32_t>(left);
            const uint32_t x_end = static_cast<uint32_t>(right);
            for (uint32_t y = static_cast<uint32_t>(top);
                 y < static_cast<uint32_t>(bottom); ++y) {
                auto* destination_row = reinterpret_cast<uint32_t*>(
                    framebuffer_bytes + static_cast<size_t>(y) * g_framebuffer.pitch);
                const auto* source_row = g_backbuffer +
                    static_cast<size_t>(y) * static_cast<size_t>(frame_width);

                uint32_t first_changed = x_begin;
                while (first_changed < x_end &&
                       destination_row[first_changed] == source_row[first_changed]) {
                    ++first_changed;
                }
                if (first_changed == x_end) continue;

                uint32_t last_changed = x_end;
                while (last_changed > first_changed &&
                       destination_row[last_changed - 1U] ==
                           source_row[last_changed - 1U]) {
                    --last_changed;
                }
                const size_t changed_pixels = static_cast<size_t>(
                    last_changed - first_changed);
                memcpy(
                    destination_row + first_changed,
                    source_row + first_changed,
                    changed_pixels * sizeof(uint32_t));
            }
        }
    }

    g_frame_active = false;
    reset_clip();
    reset_text_scale_limit();
}

void end_frame() {
    if (!g_available || !g_frame_active) return;
    const DamageRect full = {
        0, 0,
        static_cast<int32_t>(g_framebuffer.width),
        static_cast<int32_t>(g_framebuffer.height),
    };
    end_frame_regions(&full, 1U);
}
'''
framebuffer.write_text(text[:start] + replacement + text[end:])

replace_once(
    'kernel/ui/window_manager.hpp',
    '''constexpr size_t MAX_WINDOWS = 12U;\n''',
    '''constexpr size_t MAX_WINDOWS = 12U;\nconstexpr size_t MAX_DAMAGE_REGIONS = 16U;\n''',
)
replace_once(
    'kernel/ui/window_manager.hpp',
    '''void invalidate();\nbool render_if_needed();\n''',
    '''void invalidate();\nvoid invalidate_window(WindowId id);\nvoid invalidate_region(const ui::Rect& region);\nbool render_if_needed();\n''',
)

wm = Path('kernel/ui/window_manager.cpp')
text = wm.read_text()
text = text.replace(
    '''enum class DirtyMode : uint8_t {\n    None = 0,\n    Full,\n};\n''',
    '''enum class DirtyMode : uint8_t {\n    None = 0,\n    Regions,\n    Full,\n};\n''',
    1,
)
text = text.replace(
    '''DirtyMode g_dirty = DirtyMode::None;\nbool g_initialized = false;\n''',
    '''DirtyMode g_dirty = DirtyMode::None;\nui::Rect g_damage_regions[MAX_DAMAGE_REGIONS]{};\nsize_t g_damage_count = 0U;\nbool g_initialized = false;\n''',
    1,
)
text = text.replace(
    '''void mark_full_dirty() {\n    g_dirty = DirtyMode::Full;\n}\n''',
    '''void mark_full_dirty() {\n    g_dirty = DirtyMode::Full;\n    g_damage_count = 0U;\n}\n''',
    1,
)
anchor = '''bool rect_contains(const ui::Rect& rectangle, int32_t x, int32_t y) {\n    return x >= rectangle.x && y >= rectangle.y &&\n        x < rectangle.x + rectangle.width &&\n        y < rectangle.y + rectangle.height;\n}\n'''
if text.count(anchor) != 1:
    raise SystemExit('window manager rect_contains anchor mismatch')
damage_helpers = r'''

ui::Rect clip_damage_region(const ui::Rect& input) {
    if (input.width <= 0 || input.height <= 0 ||
        g_screen_width <= 0 || g_screen_height <= 0) return {};
    int64_t left = input.x;
    int64_t top = input.y;
    int64_t right = static_cast<int64_t>(input.x) + input.width;
    int64_t bottom = static_cast<int64_t>(input.y) + input.height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > g_screen_width) right = g_screen_width;
    if (bottom > g_screen_height) bottom = g_screen_height;
    if (left >= right || top >= bottom) return {};
    return {
        static_cast<int32_t>(left),
        static_cast<int32_t>(top),
        static_cast<int32_t>(right - left),
        static_cast<int32_t>(bottom - top),
    };
}

bool damage_regions_touch(const ui::Rect& left, const ui::Rect& right) {
    const int64_t left_right = static_cast<int64_t>(left.x) + left.width;
    const int64_t left_bottom = static_cast<int64_t>(left.y) + left.height;
    const int64_t right_right = static_cast<int64_t>(right.x) + right.width;
    const int64_t right_bottom = static_cast<int64_t>(right.y) + right.height;
    return static_cast<int64_t>(left.x) <= right_right &&
        static_cast<int64_t>(right.x) <= left_right &&
        static_cast<int64_t>(left.y) <= right_bottom &&
        static_cast<int64_t>(right.y) <= left_bottom;
}

ui::Rect unite_damage_regions(const ui::Rect& left, const ui::Rect& right) {
    const int64_t x1 = left.x < right.x ? left.x : right.x;
    const int64_t y1 = left.y < right.y ? left.y : right.y;
    const int64_t left_right = static_cast<int64_t>(left.x) + left.width;
    const int64_t right_right = static_cast<int64_t>(right.x) + right.width;
    const int64_t left_bottom = static_cast<int64_t>(left.y) + left.height;
    const int64_t right_bottom = static_cast<int64_t>(right.y) + right.height;
    const int64_t x2 = left_right > right_right ? left_right : right_right;
    const int64_t y2 = left_bottom > right_bottom ? left_bottom : right_bottom;
    return {
        static_cast<int32_t>(x1), static_cast<int32_t>(y1),
        static_cast<int32_t>(x2 - x1), static_cast<int32_t>(y2 - y1),
    };
}

void add_damage_region(const ui::Rect& requested) {
    if (!g_initialized || g_dirty == DirtyMode::Full) return;
    ui::Rect region = clip_damage_region(requested);
    if (region.width <= 0 || region.height <= 0) return;

    size_t index = 0U;
    while (index < g_damage_count) {
        if (!damage_regions_touch(region, g_damage_regions[index])) {
            ++index;
            continue;
        }
        region = unite_damage_regions(region, g_damage_regions[index]);
        for (size_t move = index + 1U; move < g_damage_count; ++move) {
            g_damage_regions[move - 1U] = g_damage_regions[move];
        }
        --g_damage_count;
        index = 0U;
    }

    if (g_damage_count >= MAX_DAMAGE_REGIONS) {
        mark_full_dirty();
        return;
    }
    g_damage_regions[g_damage_count++] = region;
    g_dirty = DirtyMode::Regions;
}
'''
text = text.replace(anchor, anchor + damage_helpers, 1)

old = '''void invalidate() {\n    mark_full_dirty();\n}\n\nbool render_if_needed() {\n    if (!g_initialized || g_dirty == DirtyMode::None) return false;\n#ifndef KUROGANE_HOST_TEST\n    hide_cursor();\n    const bool buffered = graphics::begin_frame();\n    render_layers();\n    if (buffered) graphics::end_frame();\n    show_cursor(input::pointer_x(), input::pointer_y());\n#endif\n    g_dirty = DirtyMode::None;\n    return true;\n}\n'''
new = r'''void invalidate() {
    mark_full_dirty();
}

void invalidate_window(WindowId id) {
    if (!g_initialized) return;
    Slot* slot = find(id);
    if (slot == nullptr || !slot->occupied ||
        slot->info.state == WindowState::Minimized) return;
    add_damage_region(slot->info.bounds);
}

void invalidate_region(const ui::Rect& region) {
    add_damage_region(region);
}

bool render_if_needed() {
    if (!g_initialized || g_dirty == DirtyMode::None) return false;
    const DirtyMode pending_mode = g_dirty;
    const size_t pending_count = g_damage_count;
    ui::Rect pending[MAX_DAMAGE_REGIONS]{};
    for (size_t index = 0U; index < pending_count; ++index) {
        pending[index] = g_damage_regions[index];
    }
#ifndef KUROGANE_HOST_TEST
    hide_cursor();
    const bool buffered = graphics::begin_frame();
    render_layers();
    if (buffered) {
        if (pending_mode == DirtyMode::Full || pending_count == 0U) {
            graphics::end_frame();
        } else {
            graphics::DamageRect regions[MAX_DAMAGE_REGIONS]{};
            for (size_t index = 0U; index < pending_count; ++index) {
                regions[index] = {
                    pending[index].x, pending[index].y,
                    pending[index].width, pending[index].height,
                };
            }
            graphics::end_frame_regions(regions, pending_count);
        }
    }
    show_cursor(input::pointer_x(), input::pointer_y());
#endif
    g_dirty = DirtyMode::None;
    g_damage_count = 0U;
    return true;
}
'''
if text.count(old) != 1:
    raise SystemExit('window manager invalidate/render anchor mismatch')
text = text.replace(old, new, 1)
wm.write_text(text)

replace_once(
    'kernel/user/runtime_base.inc',
    '''            windowing::invalidate();\n            frame.rax = KU_STATUS_OK;\n''',
    '''            windowing::invalidate_window(context->ui.window);\n            frame.rax = KU_STATUS_OK;\n''',
)

# Keep all deferred verification in the single backlog requested by the owner.
todo = Path('TODO-DEFERRED-TESTS.md')
text = todo.read_text()
anchor = '## 4.0 KuroFS deferred tests\n'
addition = (
    '## 3.6 Flux Stabilization\n'
    '- Damage-region regressions: bounded region capacity/fallback-to-full, clipping/overflow edges, overlap merge, per-window UI_PRESENT invalidation, minimized-window silence, full redraw on geometry/z-order changes, partial GOP span presentation and cursor interaction.\n\n'
    + anchor
)
if text.count(anchor) != 1:
    raise SystemExit('3.6 TODO insertion anchor mismatch')
todo.write_text(text.replace(anchor, addition, 1))

print('Flux bounded damage-region slice applied')
