#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


# ----- UI primitive: three-segment system status strip.
path = "kernel/ui/ui.hpp"
text = read(path)
text = replace_once(
    text,
    "void desktop(const char* title);\n",
    "void desktop(const char* title);\n"
    "void status_strip(\n"
    "    const Rect& bounds, const char* network, const char* audio,\n"
    "    const char* uptime, bool network_ready, bool audio_ready);\n",
    "status strip UI declaration")
write(path, text)

path = "kernel/ui/ui.cpp"
text = read(path)
old_static = '''    if (width > 640) {
        const int32_t status_x = width - 238;
        graphics::fill_rect(status_x + 3, 14, 218, 23, kSurfaceShadow);
        graphics::fill_rect(status_x, 11, 218, 24, kTheme.panel_alt);
        graphics::fill_rect(status_x, 11, 4, 24, kTheme.accent);
        graphics::draw_text(status_x + 14, 19, "SESSION / RED FLUX 3.2",
                            kTheme.text_muted, kTheme.panel_alt, 1U, true);
    }

'''
text = replace_once(text, old_static, "", "remove static session badge")
anchor = "void panel(const Rect& bounds, bool raised) {\n"
status_impl = r'''void status_strip(
    const Rect& bounds, const char* network, const char* audio,
    const char* uptime, bool network_ready, bool audio_ready) {
    if (bounds.width < 180 || bounds.height < 20) return;
    const int32_t segment = bounds.width / 3;
    graphics::fill_rect(
        bounds.x + 3, bounds.y + 3, bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(
        bounds.x, bounds.y, bounds.width, bounds.height, kTheme.panel_alt);
    graphics::draw_rect(
        bounds.x, bounds.y, bounds.width, bounds.height, kTheme.border);
    graphics::fill_rect(
        bounds.x, bounds.y, 4, bounds.height,
        network_ready ? kRedBright : kInactiveSignal);
    graphics::fill_rect(
        bounds.x + segment, bounds.y + 4, 1, bounds.height - 8, kTheme.border);
    graphics::fill_rect(
        bounds.x + segment * 2, bounds.y + 4, 1, bounds.height - 8, kTheme.border);
    graphics::fill_rect(
        bounds.x + segment + 5, bounds.y + bounds.height - 3,
        segment - 10, 2, audio_ready ? kRedMuted : kInactiveSignal);
    graphics::draw_text(
        bounds.x + 12, bounds.y + 8, network ? network : "NET --",
        network_ready ? kTheme.text : kTheme.text_muted,
        kTheme.panel_alt, 1U, true);
    graphics::draw_text(
        bounds.x + segment + 10, bounds.y + 8, audio ? audio : "AUD --",
        audio_ready ? kTheme.text : kTheme.text_muted,
        kTheme.panel_alt, 1U, true);
    graphics::draw_text(
        bounds.x + segment * 2 + 10, bounds.y + 8, uptime ? uptime : "UP 0s",
        kTheme.text_muted, kTheme.panel_alt, 1U, true);
}

'''
text = replace_once(text, anchor, status_impl + anchor, "status strip UI implementation")
write(path, text)

# ----- Window Core owns non-destructive system status state and bounded 1 Hz damage.
path = "kernel/ui/window_manager.hpp"
text = read(path)
text = replace_once(
    text,
    "struct ResourceSnapshot {\n    size_t windows;\n    size_t retained_surfaces;\n    size_t retained_bytes;\n};\n\n",
    "struct ResourceSnapshot {\n"
    "    size_t windows;\n"
    "    size_t retained_surfaces;\n"
    "    size_t retained_bytes;\n"
    "};\n\n"
    "struct SystemStatusSnapshot {\n"
    "    bool network_ready;\n"
    "    bool network_physical;\n"
    "    bool audio_ready;\n"
    "    bool audio_muted;\n"
    "    uint32_t audio_volume_percent;\n"
    "    uint64_t uptime_seconds;\n"
    "};\n\n",
    "system status public snapshot")
text = replace_once(
    text,
    "Status resource_snapshot(ResourceSnapshot* out_snapshot);\n",
    "Status resource_snapshot(ResourceSnapshot* out_snapshot);\n"
    "Status system_status_snapshot(SystemStatusSnapshot* out_snapshot);\n"
    "ui::Rect status_strip_geometry();\n"
    "bool refresh_system_status(uint64_t timer_ticks);\n",
    "system status public API")
text = text.replace(
    "// Performance starts pinned by default.\n",
    "// Control Center starts pinned by default.\n")
write(path, text)

path = "kernel/ui/window_manager.cpp"
text = read(path)
text = replace_once(
    text,
    '#include "../drivers/framebuffer.hpp"\n#include "../task/process.hpp"\n',
    '#include "../drivers/framebuffer.hpp"\n'
    '#include "../drivers/audio/ac97.hpp"\n'
    '#include "../net/service.hpp"\n'
    '#include "../task/process.hpp"\n',
    "status service includes")
text = replace_once(
    text,
    "constexpr int32_t RIBBON_ITEM_MIN = 48;\n",
    "constexpr int32_t RIBBON_ITEM_MIN = 48;\n"
    "constexpr int32_t STATUS_STRIP_WIDTH = 282;\n"
    "constexpr int32_t STATUS_STRIP_HEIGHT = 26;\n"
    "constexpr int32_t STATUS_STRIP_RIGHT = 18;\n"
    "constexpr int32_t STATUS_STRIP_TOP = 10;\n"
    "constexpr uint64_t STATUS_REFRESH_TICKS = UINT64_C(100);\n",
    "status strip constants")
text = replace_once(
    text,
    "size_t g_damage_count = 0U;\nbool g_initialized = false;\n",
    "size_t g_damage_count = 0U;\n"
    "SystemStatusSnapshot g_system_status{};\n"
    "uint64_t g_last_status_tick = 0U;\n"
    "bool g_status_sampled = false;\n"
    "bool g_initialized = false;\n",
    "status strip state")

copy_anchor = "void copy_title(char* destination, const char* title) {\n"
format_helpers = r'''void append_status_text(char* destination, size_t capacity, const char* source) {
    if (destination == nullptr || source == nullptr || capacity == 0U) return;
    size_t used = 0U;
    while (used < capacity && destination[used] != '\0') ++used;
    if (used >= capacity) return;
    size_t source_index = 0U;
    while (used + 1U < capacity && source[source_index] != '\0') {
        destination[used++] = source[source_index++];
    }
    destination[used] = '\0';
}

void append_status_uint(char* destination, size_t capacity, uint64_t value) {
    char reverse[24]{};
    size_t count = 0U;
    do {
        reverse[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(reverse));
    char number[24]{};
    size_t written = 0U;
    while (count != 0U && written + 1U < sizeof(number)) {
        number[written++] = reverse[--count];
    }
    number[written] = '\0';
    append_status_text(destination, capacity, number);
}

'''
text = replace_once(text, copy_anchor, format_helpers + copy_anchor, "status text helpers")

workspace_anchor = "WorkspaceGeometry calculate_workspace() {\n"
status_geometry = r'''ui::Rect calculate_status_strip() {
    if (!g_initialized || g_screen_width < 700) return {};
    return {
        g_screen_width - STATUS_STRIP_RIGHT - STATUS_STRIP_WIDTH,
        STATUS_STRIP_TOP,
        STATUS_STRIP_WIDTH,
        STATUS_STRIP_HEIGHT,
    };
}

'''
text = replace_once(text, workspace_anchor, status_geometry + workspace_anchor, "status strip geometry")

text = replace_once(
    text,
    "    ui::desktop(\"KUROGANE / RED FLUX\");\n    const WorkspaceGeometry workspace = calculate_workspace();\n",
    "    ui::desktop(\"KUROGANE / RED FLUX\");\n"
    "    {\n"
    "        const ui::Rect status_bounds = calculate_status_strip();\n"
    "        if (status_bounds.width > 0) {\n"
    "            char network[20] = \"NET \";\n"
    "            char audio[20] = \"AUD \";\n"
    "            char uptime[24] = \"UP \";\n"
    "            append_status_text(\n"
    "                network, sizeof(network),\n"
    "                g_system_status.network_ready ? \"ON\"\n"
    "                    : (g_system_status.network_physical ? \"WAIT\" : \"OFF\"));\n"
    "            if (!g_system_status.audio_ready) {\n"
    "                append_status_text(audio, sizeof(audio), \"--\");\n"
    "            } else if (g_system_status.audio_muted) {\n"
    "                append_status_text(audio, sizeof(audio), \"MUTE\");\n"
    "            } else {\n"
    "                append_status_uint(\n"
    "                    audio, sizeof(audio), g_system_status.audio_volume_percent);\n"
    "                append_status_text(audio, sizeof(audio), \"%\");\n"
    "            }\n"
    "            append_status_uint(\n"
    "                uptime, sizeof(uptime), g_system_status.uptime_seconds);\n"
    "            append_status_text(uptime, sizeof(uptime), \"s\");\n"
    "            ui::status_strip(\n"
    "                status_bounds, network, audio, uptime,\n"
    "                g_system_status.network_ready, g_system_status.audio_ready);\n"
    "        }\n"
    "    }\n"
    "    const WorkspaceGeometry workspace = calculate_workspace();\n",
    "render live status strip")

text = replace_once(
    text,
    "    g_damage_count = 0U;\n    g_screen_width = static_cast<int32_t>(screen_width);\n",
    "    g_damage_count = 0U;\n"
    "    g_system_status = {};\n"
    "    g_last_status_tick = 0U;\n"
    "    g_status_sampled = false;\n"
    "    g_screen_width = static_cast<int32_t>(screen_width);\n",
    "initialize status strip state")

public_anchor = "Status resource_snapshot(ResourceSnapshot* out_snapshot) {\n"
status_public = r'''Status system_status_snapshot(SystemStatusSnapshot* out_snapshot) {
    if (!g_initialized) return Status::NotInitialized;
    if (out_snapshot == nullptr) return Status::InvalidArgument;
    *out_snapshot = g_system_status;
    return Status::Ok;
}

ui::Rect status_strip_geometry() { return calculate_status_strip(); }

bool refresh_system_status(uint64_t timer_ticks) {
    if (!g_initialized) return false;
    if (g_status_sampled && timer_ticks - g_last_status_tick < STATUS_REFRESH_TICKS) {
        return false;
    }

    SystemStatusSnapshot next = g_system_status;
    next.uptime_seconds = timer_ticks / STATUS_REFRESH_TICKS;
#ifndef KUROGANE_HOST_TEST
    next.network_ready = net::service::ready();
    next.network_physical = net::service::physical_interface();
    next.audio_ready = drivers::audio::ac97::initialized();
    if (next.audio_ready) {
        next.audio_muted = drivers::audio::ac97::muted();
        next.audio_volume_percent = drivers::audio::ac97::master_volume_percent();
    } else {
        next.audio_muted = false;
        next.audio_volume_percent = 0U;
    }
#endif
    g_last_status_tick = timer_ticks;
    g_status_sampled = true;

    const bool changed =
        next.network_ready != g_system_status.network_ready ||
        next.network_physical != g_system_status.network_physical ||
        next.audio_ready != g_system_status.audio_ready ||
        next.audio_muted != g_system_status.audio_muted ||
        next.audio_volume_percent != g_system_status.audio_volume_percent ||
        next.uptime_seconds != g_system_status.uptime_seconds;
    if (!changed) return false;
    g_system_status = next;
    if (login_surface() == nullptr) add_damage_region(calculate_status_strip());
    return true;
}

'''
text = replace_once(text, public_anchor, status_public + public_anchor, "status strip public functions")
write(path, text)

# ----- Flux session ticks refresh the small strip and prove it after login.
path = "kernel/apps/desktop_session.cpp"
text = read(path)
text = replace_once(
    text,
    "namespace {\n\nbool flux_session_start(const char*) {\n",
    "namespace {\n\nbool g_status_strip_marker_emitted = false;\n\n"
    "bool flux_session_start(const char*) {\n",
    "status strip runtime marker state")
text = replace_once(
    text,
    "    windowing::invalidate();\n",
    "    g_status_strip_marker_emitted = false;\n"
    "    windowing::invalidate();\n",
    "reset status marker")
old_tick = '''void flux_session_tick(uint64_t) {
    // Repaint only when a window operation or userspace UI present marks the
    // WindowManager dirty. Idle desktop must not continuously touch GOP.
    static_cast<void>(windowing::render_if_needed());
}
'''
new_tick = '''void flux_session_tick(uint64_t ticks) {
    // Status changes dirty only the compact top strip. The rest of an idle
    // desktop remains untouched, preserving the regional compositor model.
    static_cast<void>(windowing::refresh_system_status(ticks));
    if (!g_status_strip_marker_emitted) {
        const windowing::WindowId focused = windowing::focused_window();
        windowing::WindowInfo info{};
        if (focused != windowing::INVALID_WINDOW &&
            windowing::query(focused, &info) == windowing::Status::Ok &&
            !kstd::streq(info.title, "KUROGANE LOGIN")) {
            terminal::println("[TEST] flux_status_strip_live: PASS");
            g_status_strip_marker_emitted = true;
        }
    }
    static_cast<void>(windowing::render_if_needed());
}
'''
text = replace_once(text, old_tick, new_tick, "status strip session tick")
write(path, text)

# ----- Host regression: 1 Hz uptime dirties only the status rectangle.
path = "tests/test_window_manager.cpp"
text = read(path)
anchor = "    if (!render_if_needed() || render_if_needed()) return 20;\n\n"
test = r'''    if (!render_if_needed() || render_if_needed()) return 20;

    SystemStatusSnapshot system_status{};
    if (!refresh_system_status(100U) ||
        system_status_snapshot(&system_status) != Status::Ok ||
        system_status.uptime_seconds != 1U ||
        system_status.network_ready || system_status.audio_ready) return 137;
    DamageSnapshot status_damage{};
    const ui::Rect status_bounds = status_strip_geometry();
    if (status_bounds.width <= 0 ||
        damage_snapshot(&status_damage) != Status::Ok || status_damage.full ||
        status_damage.count != 1U ||
        status_damage.regions[0].x != status_bounds.x ||
        status_damage.regions[0].y != status_bounds.y ||
        status_damage.regions[0].width != status_bounds.width ||
        status_damage.regions[0].height != status_bounds.height) return 138;
    if (!render_if_needed() || refresh_system_status(150U) || render_if_needed()) {
        return 139;
    }

'''
text = replace_once(text, anchor, test, "status strip bounded damage regression")
write(path, text)

path = "tests/test_mouse_first_apps.py"
text = read(path)
anchor = 'browser = read("userspace/gui/browser/main.c")\n'
contract = '''desktop_ui = read("kernel/ui/ui.cpp")
assert "SESSION / RED FLUX 3.2" not in desktop_ui
assert "void status_strip(" in desktop_ui
window_core_status = read("kernel/ui/window_manager.cpp")
assert "refresh_system_status" in window_core_status
assert "add_damage_region(calculate_status_strip())" in window_core_status
desktop_host = read("kernel/apps/desktop_session.cpp")
assert "flux_status_strip_live: PASS" in desktop_host

'''
text = replace_once(text, anchor, contract + anchor, "status strip source contract")
write(path, text)

print("Flux live status strip migration applied")
