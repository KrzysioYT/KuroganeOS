#!/usr/bin/env python3
"""Apply bounded compositor damage clipping and host raster qualification."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded match, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> None:
    replace_once(
        "kernel/drivers/framebuffer.hpp",
        "struct DamageRect {\n    int32_t x;\n    int32_t y;\n    int32_t width;\n    int32_t height;\n};\n",
        "struct DamageRect {\n"
        "    int32_t x;\n"
        "    int32_t y;\n"
        "    int32_t width;\n"
        "    int32_t height;\n"
        "};\n\n"
        "// Bounded outer compositor clip.  Application/window clips may only\n"
        "// further restrict drawing and cannot expand beyond these regions.\n"
        "constexpr size_t MAX_COMPOSITOR_DAMAGE_REGIONS = 16U;\n",
    )
    replace_once(
        "kernel/drivers/framebuffer.hpp",
        "bool frame_active();\n\n// All primitive drawing honors the current clip.",
        "bool frame_active();\n"
        "bool set_damage_regions(const DamageRect* regions, size_t count);\n"
        "void reset_damage_regions();\n"
        "bool damage_regions_active();\n\n"
        "// All primitive drawing honors the current clip.",
    )

    replace_once(
        "kernel/drivers/framebuffer.cpp",
        "ClipState g_clip{};\nuint32_t g_text_scale_limit = UINT32_MAX;",
        "ClipState g_clip{};\n"
        "DamageRect g_damage_regions[MAX_COMPOSITOR_DAMAGE_REGIONS]{};\n"
        "size_t g_damage_count = 0U;\n"
        "bool g_damage_active = false;\n"
        "uint32_t g_text_scale_limit = UINT32_MAX;",
    )
    replace_once(
        "kernel/drivers/framebuffer.cpp",
        "uint32_t effective_scale(uint32_t requested) {\n"
        "    if (requested == 0U) return 0U;\n"
        "    return requested > g_text_scale_limit ? g_text_scale_limit : requested;\n"
        "}\n\n"
        "void clip_edges(",
        "uint32_t effective_scale(uint32_t requested) {\n"
        "    if (requested == 0U) return 0U;\n"
        "    return requested > g_text_scale_limit ? g_text_scale_limit : requested;\n"
        "}\n\n"
        "bool clip_damage_rect(const DamageRect& input, DamageRect* output) {\n"
        "    if (output == nullptr || input.width <= 0 || input.height <= 0) return false;\n"
        "    int64_t left = input.x;\n"
        "    int64_t top = input.y;\n"
        "    int64_t right = static_cast<int64_t>(input.x) + input.width;\n"
        "    int64_t bottom = static_cast<int64_t>(input.y) + input.height;\n"
        "    if (left < 0) left = 0;\n"
        "    if (top < 0) top = 0;\n"
        "    if (right > static_cast<int64_t>(g_framebuffer.width)) right = g_framebuffer.width;\n"
        "    if (bottom > static_cast<int64_t>(g_framebuffer.height)) bottom = g_framebuffer.height;\n"
        "    if (left >= right || top >= bottom) return false;\n"
        "    *output = {\n"
        "        static_cast<int32_t>(left), static_cast<int32_t>(top),\n"
        "        static_cast<int32_t>(right - left),\n"
        "        static_cast<int32_t>(bottom - top),\n"
        "    };\n"
        "    return true;\n"
        "}\n\n"
        "bool damage_contains(int32_t x, int32_t y) {\n"
        "    if (!g_damage_active) return true;\n"
        "    for (size_t index = 0U; index < g_damage_count; ++index) {\n"
        "        const DamageRect& region = g_damage_regions[index];\n"
        "        if (x >= region.x && y >= region.y &&\n"
        "            x < region.x + region.width && y < region.y + region.height) {\n"
        "            return true;\n"
        "        }\n"
        "    }\n"
        "    return false;\n"
        "}\n\n"
        "void clip_edges(",
    )
    replace_once(
        "kernel/drivers/framebuffer.cpp",
        "    g_frame_active = false;\n    g_clip = {};\n    g_text_scale_limit = UINT32_MAX;",
        "    g_frame_active = false;\n"
        "    g_clip = {};\n"
        "    g_damage_count = 0U;\n"
        "    g_damage_active = false;\n"
        "    g_text_scale_limit = UINT32_MAX;",
    )
    replace_once(
        "kernel/drivers/framebuffer.cpp",
        "    g_frame_active = true;\n    reset_clip();\n    reset_text_scale_limit();",
        "    g_frame_active = true;\n"
        "    reset_damage_regions();\n"
        "    reset_clip();\n"
        "    reset_text_scale_limit();",
    )
    replace_once(
        "kernel/drivers/framebuffer.cpp",
        "    g_frame_active = false;\n    reset_clip();\n    reset_text_scale_limit();\n}\n\nvoid end_frame()",
        "    g_frame_active = false;\n"
        "    reset_damage_regions();\n"
        "    reset_clip();\n"
        "    reset_text_scale_limit();\n"
        "}\n\nvoid end_frame()",
    )
    replace_once(
        "kernel/drivers/framebuffer.cpp",
        "bool frame_active() { return g_frame_active; }\n\nvoid set_clip(",
        "bool frame_active() { return g_frame_active; }\n\n"
        "bool set_damage_regions(const DamageRect* regions, size_t count) {\n"
        "    reset_damage_regions();\n"
        "    if (!g_available || regions == nullptr || count == 0U ||\n"
        "        count > MAX_COMPOSITOR_DAMAGE_REGIONS) return false;\n"
        "    for (size_t index = 0U; index < count; ++index) {\n"
        "        DamageRect clipped{};\n"
        "        if (!clip_damage_rect(regions[index], &clipped)) continue;\n"
        "        g_damage_regions[g_damage_count++] = clipped;\n"
        "    }\n"
        "    g_damage_active = g_damage_count != 0U;\n"
        "    return g_damage_active;\n"
        "}\n\n"
        "void reset_damage_regions() {\n"
        "    g_damage_count = 0U;\n"
        "    g_damage_active = false;\n"
        "}\n\n"
        "bool damage_regions_active() { return g_damage_active; }\n\n"
        "void set_clip(",
    )
    replace_once(
        "kernel/drivers/framebuffer.cpp",
        "    if (g_clip.enabled &&\n        (x < g_clip.left || x >= g_clip.right ||\n         y < g_clip.top || y >= g_clip.bottom)) return;\n",
        "    if (g_clip.enabled &&\n"
        "        (x < g_clip.left || x >= g_clip.right ||\n"
        "         y < g_clip.top || y >= g_clip.bottom)) return;\n"
        "    if (!damage_contains(x, y)) return;\n",
    )

    old_fill = '''void fill_rect(int32_t x, int32_t y, int32_t rectangle_width,
               int32_t rectangle_height, Color color) {
    if (!g_available || rectangle_width <= 0 || rectangle_height <= 0) return;
    int64_t left = x;
    int64_t top = y;
    int64_t right = static_cast<int64_t>(x) + rectangle_width;
    int64_t bottom = static_cast<int64_t>(y) + rectangle_height;
    clip_edges(left, top, right, bottom);
    if (left >= right || top >= bottom) return;

    const uint32_t native = native_color(color);
    for (int32_t py = static_cast<int32_t>(top);
         py < static_cast<int32_t>(bottom); ++py) {
        auto* row = reinterpret_cast<uint32_t*>(
            draw_base() + static_cast<size_t>(py) * draw_pitch());
        for (int32_t px = static_cast<int32_t>(left);
             px < static_cast<int32_t>(right); ++px) {
            row[px] = native;
        }
    }
}
'''
    new_fill = '''void fill_rect(int32_t x, int32_t y, int32_t rectangle_width,
               int32_t rectangle_height, Color color) {
    if (!g_available || rectangle_width <= 0 || rectangle_height <= 0) return;
    int64_t left = x;
    int64_t top = y;
    int64_t right = static_cast<int64_t>(x) + rectangle_width;
    int64_t bottom = static_cast<int64_t>(y) + rectangle_height;
    clip_edges(left, top, right, bottom);
    if (left >= right || top >= bottom) return;

    const uint32_t native = native_color(color);
    const auto paint = [native](int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
        for (int32_t py = y1; py < y2; ++py) {
            auto* row = reinterpret_cast<uint32_t*>(
                draw_base() + static_cast<size_t>(py) * draw_pitch());
            for (int32_t px = x1; px < x2; ++px) row[px] = native;
        }
    };
    if (!g_damage_active) {
        paint(static_cast<int32_t>(left), static_cast<int32_t>(top),
              static_cast<int32_t>(right), static_cast<int32_t>(bottom));
        return;
    }

    for (size_t index = 0U; index < g_damage_count; ++index) {
        const DamageRect& damage = g_damage_regions[index];
        int64_t clipped_left = left > damage.x ? left : damage.x;
        int64_t clipped_top = top > damage.y ? top : damage.y;
        const int64_t damage_right = static_cast<int64_t>(damage.x) + damage.width;
        const int64_t damage_bottom = static_cast<int64_t>(damage.y) + damage.height;
        int64_t clipped_right = right < damage_right ? right : damage_right;
        int64_t clipped_bottom = bottom < damage_bottom ? bottom : damage_bottom;
        if (clipped_left >= clipped_right || clipped_top >= clipped_bottom) continue;
        paint(static_cast<int32_t>(clipped_left), static_cast<int32_t>(clipped_top),
              static_cast<int32_t>(clipped_right), static_cast<int32_t>(clipped_bottom));
    }
}
'''
    replace_once("kernel/drivers/framebuffer.cpp", old_fill, new_fill)

    old_scroll = '''void scroll_up(uint32_t pixels, Color fill) {
    if (!g_available || pixels == 0U) return;
    if (pixels >= g_framebuffer.height) {
        clear(fill);
        return;
    }
    reset_clip();
    const size_t pitch = draw_pitch();
    const size_t bytes_to_move =
        static_cast<size_t>(g_framebuffer.height - pixels) * pitch;
    auto* base = draw_base();
    memmove(base, base + static_cast<size_t>(pixels) * pitch, bytes_to_move);
    fill_rect(0, static_cast<int32_t>(g_framebuffer.height - pixels),
              static_cast<int32_t>(g_framebuffer.width),
              static_cast<int32_t>(pixels), fill);
}
'''
    new_scroll = '''void scroll_up(uint32_t pixels, Color fill) {
    if (!g_available || pixels == 0U) return;
    if (pixels >= g_framebuffer.height) {
        clear(fill);
        return;
    }
    reset_clip();
    const size_t pitch = draw_pitch();
    auto* base = draw_base();
    if (!g_damage_active) {
        const size_t bytes_to_move =
            static_cast<size_t>(g_framebuffer.height - pixels) * pitch;
        memmove(base, base + static_cast<size_t>(pixels) * pitch, bytes_to_move);
        fill_rect(0, static_cast<int32_t>(g_framebuffer.height - pixels),
                  static_cast<int32_t>(g_framebuffer.width),
                  static_cast<int32_t>(pixels), fill);
        return;
    }

    const uint32_t native = native_color(fill);
    // Process destination rows top-to-bottom.  Every scroll source row is
    // strictly below its destination, so source pixels are read before they
    // can be modified even when damage regions overlap.
    for (uint32_t y = 0U; y < g_framebuffer.height; ++y) {
        for (size_t index = 0U; index < g_damage_count; ++index) {
            const DamageRect& region = g_damage_regions[index];
            if (y < static_cast<uint32_t>(region.y) ||
                y >= static_cast<uint32_t>(region.y + region.height)) continue;
            auto* destination = reinterpret_cast<uint32_t*>(
                base + static_cast<size_t>(y) * pitch) + region.x;
            const size_t span = static_cast<size_t>(region.width);
            if (y + pixels < g_framebuffer.height) {
                const auto* source = reinterpret_cast<const uint32_t*>(
                    base + static_cast<size_t>(y + pixels) * pitch) + region.x;
                memmove(destination, source, span * sizeof(uint32_t));
            } else {
                for (size_t x = 0U; x < span; ++x) destination[x] = native;
            }
        }
    }
}
'''
    replace_once("kernel/drivers/framebuffer.cpp", old_scroll, new_scroll)

    old_render = '''#ifndef KUROGANE_HOST_TEST
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
'''
    new_render = '''#ifndef KUROGANE_HOST_TEST
    hide_cursor();
    const bool buffered = graphics::begin_frame();
    graphics::DamageRect regions[MAX_DAMAGE_REGIONS]{};
    bool partial_raster = false;
    if (pending_mode == DirtyMode::Regions && pending_count != 0U) {
        for (size_t index = 0U; index < pending_count; ++index) {
            regions[index] = {
                pending[index].x, pending[index].y,
                pending[index].width, pending[index].height,
            };
        }
        partial_raster = graphics::set_damage_regions(regions, pending_count);
    }
    render_layers();
    graphics::reset_damage_regions();
    if (buffered) {
        if (partial_raster) {
            graphics::end_frame_regions(regions, pending_count);
        } else {
            graphics::end_frame();
        }
    }
    show_cursor(input::pointer_x(), input::pointer_y());
#endif
'''
    replace_once("kernel/ui/window_manager.cpp", old_render, new_render)

    test_path = ROOT / "tests/test_framebuffer_damage.cpp"
    if test_path.exists():
        raise SystemExit("tests/test_framebuffer_damage.cpp already exists")
    test_path.write_text(r'''#include "../kernel/drivers/framebuffer.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {
constexpr uint32_t WIDTH = 16U;
constexpr uint32_t HEIGHT = 12U;
uint32_t g_pixels[WIDTH * HEIGHT]{};

bool equals(uint32_t x, uint32_t y, uint32_t value) {
    return g_pixels[static_cast<size_t>(y) * WIDTH + x] == value;
}

bool all_equal(uint32_t value) {
    for (uint32_t pixel : g_pixels) {
        if (pixel != value) return false;
    }
    return true;
}
}

int main() {
    KuroganeFramebuffer framebuffer{};
    framebuffer.base = g_pixels;
    framebuffer.width = WIDTH;
    framebuffer.height = HEIGHT;
    framebuffer.pitch = WIDTH * sizeof(uint32_t);
    framebuffer.bpp = 32U;
    framebuffer.pixel_format = KUROGANE_PIXEL_BGRX8;
    if (!graphics::init(framebuffer)) return 1;

    constexpr uint32_t baseline = 0x00112233U;
    constexpr uint32_t changed = 0x00445566U;
    constexpr uint32_t clipped = 0x00778899U;
    if (!graphics::begin_frame()) return 2;
    graphics::clear(baseline);
    graphics::end_frame();
    if (!all_equal(baseline)) return 3;

    const graphics::DamageRect damage[] = {
        {3, 2, 4, 3},
        {10, 7, 3, 2},
    };
    if (!graphics::begin_frame()) return 4;
    if (!graphics::set_damage_regions(damage, 2U) ||
        !graphics::damage_regions_active()) return 5;

    // clear() and reset_clip() must never escape the outer compositor mask.
    graphics::clear(changed);
    graphics::set_clip(4, 3, 1, 1);
    graphics::fill_rect(0, 0, static_cast<int32_t>(WIDTH),
                        static_cast<int32_t>(HEIGHT), clipped);
    graphics::reset_clip();
    graphics::fill_rect(0, 0, static_cast<int32_t>(WIDTH),
                        static_cast<int32_t>(HEIGHT), changed);
    graphics::put_pixel(0, 0, clipped);
    graphics::end_frame_regions(damage, 2U);

    for (uint32_t y = 0U; y < HEIGHT; ++y) {
        for (uint32_t x = 0U; x < WIDTH; ++x) {
            const bool damaged =
                (x >= 3U && x < 7U && y >= 2U && y < 5U) ||
                (x >= 10U && x < 13U && y >= 7U && y < 9U);
            if (!equals(x, y, damaged ? changed : baseline)) return 6;
        }
    }

    // A later full present exposes the whole retained backbuffer.  This proves
    // the partial raster itself, not merely the final GOP blit, stayed bounded.
    if (!graphics::begin_frame()) return 7;
    graphics::end_frame();
    for (uint32_t y = 0U; y < HEIGHT; ++y) {
        for (uint32_t x = 0U; x < WIDTH; ++x) {
            const bool damaged =
                (x >= 3U && x < 7U && y >= 2U && y < 5U) ||
                (x >= 10U && x < 13U && y >= 7U && y < 9U);
            if (!equals(x, y, damaged ? changed : baseline)) return 8;
        }
    }

    // Damage-aware scroll may read outside the damage region but may write only
    // inside it.  The subsequent full present again exposes hidden corruption.
    const graphics::DamageRect scroll_damage = {5, 4, 2, 4};
    if (!graphics::begin_frame()) return 9;
    if (!graphics::set_damage_regions(&scroll_damage, 1U)) return 10;
    graphics::scroll_up(1U, clipped);
    graphics::end_frame_regions(&scroll_damage, 1U);
    if (!graphics::begin_frame()) return 11;
    graphics::end_frame();
    for (uint32_t y = 0U; y < HEIGHT; ++y) {
        for (uint32_t x = 0U; x < WIDTH; ++x) {
            const bool first_damage =
                (x >= 3U && x < 7U && y >= 2U && y < 5U) ||
                (x >= 10U && x < 13U && y >= 7U && y < 9U);
            if (x < 5U || x >= 7U || y < 4U || y >= 8U) {
                if (!equals(x, y, first_damage ? changed : baseline)) return 12;
            }
        }
    }

    graphics::DamageRect too_many[graphics::MAX_COMPOSITOR_DAMAGE_REGIONS + 1U]{};
    if (graphics::set_damage_regions(
            too_many, graphics::MAX_COMPOSITOR_DAMAGE_REGIONS + 1U) ||
        graphics::damage_regions_active()) return 13;

    std::puts("framebuffer compositor damage raster tests passed");
    return 0;
}
''', encoding="utf-8")

    replace_once(
        "scripts/run-host-tests.sh",
        '"$OUT_DIR/test_graphics_runtime"\n\n"$HOST_CXX" \\\n',
        '"$OUT_DIR/test_graphics_runtime"\n\n'
        '# Exercise the real framebuffer backbuffer with a bounded outer damage mask.\n'
        '"$HOST_CXX" \\\n'
        '  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \\\n'
        '  tests/test_framebuffer_damage.cpp \\\n'
        '  kernel/drivers/framebuffer.cpp \\\n'
        '  -o "$OUT_DIR/test_framebuffer_damage"\n\n'
        '"$OUT_DIR/test_framebuffer_damage"\n\n'
        '"$HOST_CXX" \\\n',
    )

    print("[dev-apply-flux-region-raster] applied bounded region-only compositor raster")


if __name__ == "__main__":
    main()
