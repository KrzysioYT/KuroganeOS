#!/usr/bin/env python3
"""Apply the Flux 3.6 input/focus/capture hardening slice."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one guarded match, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> None:
    header = "kernel/ui/window_manager.hpp"
    source = "kernel/ui/window_manager.cpp"
    tests = "tests/test_window_manager.cpp"

    replace_once(
        header,
        "struct WorkspaceGeometry {",
        "struct InteractionSnapshot {\n"
        "    WindowId focused;\n"
        "    WindowId dragged;\n"
        "    WindowId resized;\n"
        "};\n\n"
        "struct WorkspaceGeometry {",
    )
    replace_once(
        header,
        "Status damage_snapshot(DamageSnapshot* out_snapshot);\nStatus present_surface(",
        "Status damage_snapshot(DamageSnapshot* out_snapshot);\n"
        "Status interaction_snapshot(InteractionSnapshot* out_snapshot);\n"
        "Status present_surface(",
    )

    replace_once(
        source,
        "void choose_top_focus() {",
        "void cancel_capture(WindowId id) {\n"
        "    if (g_dragged == id) g_dragged = INVALID_WINDOW;\n"
        "    if (g_resized == id) g_resized = INVALID_WINDOW;\n"
        "}\n\n"
        "void choose_top_focus() {",
    )
    replace_once(
        source,
        "    --g_count;\n    release_slot(*slot);\n    if (g_dragged == id) g_dragged = INVALID_WINDOW;\n    if (g_resized == id) g_resized = INVALID_WINDOW;\n    choose_top_focus();",
        "    --g_count;\n"
        "    release_slot(*slot);\n"
        "    cancel_capture(id);\n"
        "    choose_top_focus();",
    )
    replace_once(
        source,
        "    slot->info.state = WindowState::Minimized;\n    if (g_focused == id) choose_top_focus();",
        "    cancel_capture(id);\n"
        "    slot->info.state = WindowState::Minimized;\n"
        "    if (g_focused == id) choose_top_focus();",
    )
    replace_once(
        source,
        "    if (slot->info.state == WindowState::Normal) slot->info.restore_bounds = slot->info.bounds;\n    const WorkspaceGeometry workspace = calculate_workspace();",
        "    cancel_capture(id);\n"
        "    if (slot->info.state == WindowState::Normal) slot->info.restore_bounds = slot->info.bounds;\n"
        "    const WorkspaceGeometry workspace = calculate_workspace();",
    )
    replace_once(
        source,
        "    slot->info.state = WindowState::Normal;\n    slot->info.bounds = slot->info.restore_bounds;",
        "    cancel_capture(id);\n"
        "    slot->info.state = WindowState::Normal;\n"
        "    slot->info.bounds = slot->info.restore_bounds;",
    )
    replace_once(
        source,
        "    if (event.type == input::EventType::MouseButtonDown &&\n        event.button == drivers::mouse::Left) {\n        const WorkspaceGeometry workspace = calculate_workspace();",
        "    if (event.type == input::EventType::MouseButtonDown &&\n"
        "        event.button == drivers::mouse::Left) {\n"
        "        // A fresh primary-button press starts a fresh capture decision.\n"
        "        // This recovers deterministically if a prior button-up was lost.\n"
        "        g_dragged = INVALID_WINDOW;\n"
        "        g_resized = INVALID_WINDOW;\n"
        "        const WorkspaceGeometry workspace = calculate_workspace();",
    )
    replace_once(
        source,
        "        if (g_resized != INVALID_WINDOW) {\n            Slot* slot = find(g_resized);\n            if (slot != nullptr) resize_window(*slot, event.x, event.y);\n        } else if (g_dragged != INVALID_WINDOW) {\n            static_cast<void>(move(\n                g_dragged,\n                event.x - g_drag_offset_x,\n                event.y - g_drag_offset_y));\n        }",
        "        if (g_resized != INVALID_WINDOW) {\n"
        "            Slot* slot = find(g_resized);\n"
        "            if (slot != nullptr) {\n"
        "                resize_window(*slot, event.x, event.y);\n"
        "            } else {\n"
        "                g_resized = INVALID_WINDOW;\n"
        "            }\n"
        "        } else if (g_dragged != INVALID_WINDOW) {\n"
        "            const WindowId dragged = g_dragged;\n"
        "            if (move(\n"
        "                    dragged,\n"
        "                    event.x - g_drag_offset_x,\n"
        "                    event.y - g_drag_offset_y) != Status::Ok) {\n"
        "                g_dragged = INVALID_WINDOW;\n"
        "            }\n"
        "        }",
    )
    replace_once(
        source,
        "Status damage_snapshot(DamageSnapshot* out_snapshot) {\n    if (!g_initialized) return Status::NotInitialized;\n    if (out_snapshot == nullptr) return Status::InvalidArgument;\n    *out_snapshot = {};\n    out_snapshot->full = g_dirty == DirtyMode::Full;\n    if (g_dirty != DirtyMode::Regions) return Status::Ok;\n    out_snapshot->count = g_damage_count;\n    for (size_t index = 0U; index < g_damage_count; ++index) {\n        out_snapshot->regions[index] = g_damage_regions[index];\n    }\n    return Status::Ok;\n}\n\nbool render_if_needed() {",
        "Status damage_snapshot(DamageSnapshot* out_snapshot) {\n"
        "    if (!g_initialized) return Status::NotInitialized;\n"
        "    if (out_snapshot == nullptr) return Status::InvalidArgument;\n"
        "    *out_snapshot = {};\n"
        "    out_snapshot->full = g_dirty == DirtyMode::Full;\n"
        "    if (g_dirty != DirtyMode::Regions) return Status::Ok;\n"
        "    out_snapshot->count = g_damage_count;\n"
        "    for (size_t index = 0U; index < g_damage_count; ++index) {\n"
        "        out_snapshot->regions[index] = g_damage_regions[index];\n"
        "    }\n"
        "    return Status::Ok;\n"
        "}\n\n"
        "Status interaction_snapshot(InteractionSnapshot* out_snapshot) {\n"
        "    if (!g_initialized) return Status::NotInitialized;\n"
        "    if (out_snapshot == nullptr) return Status::InvalidArgument;\n"
        "    out_snapshot->focused = g_focused;\n"
        "    out_snapshot->dragged = g_dragged;\n"
        "    out_snapshot->resized = g_resized;\n"
        "    return Status::Ok;\n"
        "}\n\n"
        "bool render_if_needed() {",
    )

    replace_once(
        tests,
        "    if (close(damage_window) != Status::Ok) return 42;\n    return 0;",
        "    if (close(damage_window) != Status::Ok) return 42;\n\n"
        "    // Capture is owned by the live generation and must be cancelled by\n"
        "    // every state transition that makes drag/resize invalid.\n"
        "    WindowId capture = INVALID_WINDOW;\n"
        "    if (create_window(\"Capture\", 15U, {120, 120, 300, 220},\n"
        "                      draw, receive, nullptr, &capture) != Status::Ok ||\n"
        "        chrome_geometry(capture, &chrome) != Status::Ok) return 43;\n"
        "    event = {};\n"
        "    event.type = input::EventType::MouseButtonDown;\n"
        "    event.button = drivers::mouse::Left;\n"
        "    event.buttons = drivers::mouse::Left;\n"
        "    event.x = chrome.header.x + 10;\n"
        "    event.y = chrome.header.y + 10;\n"
        "    if (dispatch(event) != Status::Ok) return 44;\n"
        "    InteractionSnapshot interaction{};\n"
        "    if (interaction_snapshot(&interaction) != Status::Ok ||\n"
        "        interaction.dragged != capture || interaction.resized != INVALID_WINDOW) return 45;\n"
        "    if (query(capture, &info) != Status::Ok) return 46;\n"
        "    const ui::Rect before_minimize = info.bounds;\n"
        "    if (minimize(capture) != Status::Ok ||\n"
        "        interaction_snapshot(&interaction) != Status::Ok ||\n"
        "        interaction.dragged != INVALID_WINDOW ||\n"
        "        interaction.resized != INVALID_WINDOW ||\n"
        "        interaction.focused == capture) return 47;\n"
        "    event = {};\n"
        "    event.type = input::EventType::MouseMove;\n"
        "    event.buttons = drivers::mouse::Left;\n"
        "    event.x = 700;\n"
        "    event.y = 500;\n"
        "    if (dispatch(event) != Status::Ok || query(capture, &info) != Status::Ok ||\n"
        "        info.bounds.x != before_minimize.x || info.bounds.y != before_minimize.y ||\n"
        "        info.bounds.width != before_minimize.width ||\n"
        "        info.bounds.height != before_minimize.height) return 48;\n"
        "    if (restore(capture) != Status::Ok || chrome_geometry(capture, &chrome) != Status::Ok) {\n"
        "        return 49;\n"
        "    }\n"
        "    event = {};\n"
        "    event.type = input::EventType::MouseButtonDown;\n"
        "    event.button = drivers::mouse::Left;\n"
        "    event.buttons = drivers::mouse::Left;\n"
        "    event.x = chrome.resize_grip.x + chrome.resize_grip.width / 2;\n"
        "    event.y = chrome.resize_grip.y + chrome.resize_grip.height / 2;\n"
        "    if (dispatch(event) != Status::Ok ||\n"
        "        interaction_snapshot(&interaction) != Status::Ok ||\n"
        "        interaction.resized != capture || interaction.dragged != INVALID_WINDOW) return 50;\n"
        "    if (maximize(capture) != Status::Ok ||\n"
        "        interaction_snapshot(&interaction) != Status::Ok ||\n"
        "        interaction.resized != INVALID_WINDOW || interaction.dragged != INVALID_WINDOW) {\n"
        "        return 51;\n"
        "    }\n"
        "    if (restore(capture) != Status::Ok || chrome_geometry(capture, &chrome) != Status::Ok) {\n"
        "        return 52;\n"
        "    }\n"
        "    event = {};\n"
        "    event.type = input::EventType::MouseButtonDown;\n"
        "    event.button = drivers::mouse::Left;\n"
        "    event.buttons = drivers::mouse::Left;\n"
        "    event.x = chrome.resize_grip.x + 1;\n"
        "    event.y = chrome.resize_grip.y + 1;\n"
        "    if (dispatch(event) != Status::Ok || close(capture) != Status::Ok ||\n"
        "        interaction_snapshot(&interaction) != Status::Ok ||\n"
        "        interaction.dragged != INVALID_WINDOW || interaction.resized != INVALID_WINDOW) {\n"
        "        return 53;\n"
        "    }\n"
        "    WindowId capture_replacement = INVALID_WINDOW;\n"
        "    if (create_window(\"Capture2\", 16U, {130, 130, 300, 220},\n"
        "                      draw, receive, nullptr, &capture_replacement) != Status::Ok ||\n"
        "        capture_replacement == capture || query(capture_replacement, &info) != Status::Ok) {\n"
        "        return 54;\n"
        "    }\n"
        "    const ui::Rect replacement_bounds = info.bounds;\n"
        "    event = {};\n"
        "    event.type = input::EventType::MouseMove;\n"
        "    event.buttons = drivers::mouse::Left;\n"
        "    event.x = 700;\n"
        "    event.y = 500;\n"
        "    if (dispatch(event) != Status::Ok || query(capture_replacement, &info) != Status::Ok ||\n"
        "        info.bounds.x != replacement_bounds.x || info.bounds.y != replacement_bounds.y ||\n"
        "        info.bounds.width != replacement_bounds.width ||\n"
        "        info.bounds.height != replacement_bounds.height) return 55;\n"
        "    if (close(capture_replacement) != Status::Ok) return 56;\n"
        "    return 0;",
    )

    print("[dev-apply-flux-input] applied input/focus/capture hardening patch")


if __name__ == "__main__":
    main()
