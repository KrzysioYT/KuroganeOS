#!/usr/bin/env python3
"""Apply Flux 3.6 window destruction and session-root logout hardening."""

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
        "kernel/ui/window_manager.hpp",
        "Status close(WindowId id);\nStatus focus(WindowId id);",
        "Status close(WindowId id);\n"
        "// User-facing dismissal preserves the persistent HOME surface by\n"
        "// minimizing it. close() is lifecycle destruction and always releases\n"
        "// the slot/surface, including process-exit cleanup for HOME.\n"
        "Status dismiss(WindowId id);\n"
        "Status focus(WindowId id);",
    )

    replace_once(
        "kernel/ui/window_manager.cpp",
        "\n    if (is_home_surface(slot->info.title)) {\n"
        "        if (slot->info.state == WindowState::Minimized) return Status::Ok;\n"
        "        return minimize(id);\n"
        "    }\n\n"
        "    size_t position = 0U;",
        "\n    size_t position = 0U;",
    )

    close_tail = '''    mark_full_dirty();\n    return Status::Ok;\n}\n\nStatus present_surface('''
    dismiss_impl = '''    mark_full_dirty();\n    return Status::Ok;\n}\n\nStatus dismiss(WindowId id) {\n    if (!g_initialized) return Status::NotInitialized;\n    Slot* slot = find(id);\n    if (slot == nullptr) return Status::NotFound;\n    if (!is_home_surface(slot->info.title)) return close(id);\n    if (slot->info.state == WindowState::Minimized) return Status::Ok;\n    return minimize(id);\n}\n\nStatus present_surface('''
    replace_once("kernel/ui/window_manager.cpp", close_tail, dismiss_impl)

    replace_once(
        "kernel/ui/window_manager.cpp",
        "        return g_focused == INVALID_WINDOW ? Status::NotFound : close(g_focused);",
        "        return g_focused == INVALID_WINDOW ? Status::NotFound : dismiss(g_focused);",
    )
    replace_once(
        "kernel/ui/window_manager.cpp",
        "                if (rect_contains(chrome.dismiss_control, event.x, event.y)) return close(target);",
        "                if (rect_contains(chrome.dismiss_control, event.x, event.y)) return dismiss(target);",
    )

    replace_once(
        "userspace/gui/launcher/main.c",
        '    (void)kui_flow_label(&root, 3U,\n'
        '        "ARROWS: SELECT  ENTER: OPEN  P: PIN/UNPIN DESKTOP");',
        '    (void)kui_flow_label(&root, 3U,\n'
        '        "ARROWS: SELECT  ENTER: OPEN  P: PIN/UNPIN  L: LOG OUT");',
    )
    replace_once(
        "userspace/gui/launcher/main.c",
        "        } else if (event.character == 'a' || event.character == 'A') {\n"
        "            select_and_launch(6U);\n"
        "        } else if (gui_key_cancel(&event)) {",
        "        } else if (event.character == 'a' || event.character == 'A') {\n"
        "            select_and_launch(6U);\n"
        "        } else if (event.character == 'l' || event.character == 'L') {\n"
        "            puts(\"[TEST] desktop_logout_requested: PASS\");\n"
        "            break;\n"
        "        } else if (gui_key_cancel(&event)) {",
    )

    replace_once(
        "tests/test_window_manager.cpp",
        "    if (close(capture_replacement) != Status::Ok) return 56;\n    return 0;\n}",
        '''    if (close(capture_replacement) != Status::Ok) return 56;\n\n    // HOME has separate user-dismiss and process-lifecycle semantics.  A UI\n    // dismiss minimizes the persistent surface, while lifecycle close must\n    // release the generation and retained bytes immediately.\n    WindowId home = INVALID_WINDOW;\n    if (create_window("RED FLUX HOME", 17U, {100, 100, 360, 260},\n                      draw, receive, nullptr, &home) != Status::Ok ||\n        query(home, &info) != Status::Ok ||\n        info.state != WindowState::Minimized) return 57;\n    uint8_t home_payload[16]{};\n    home_payload[0] = UINT8_C(0x5a);\n    if (present_surface(home, 4U, 4U, 4U, home_payload, sizeof(home_payload)) !=\n            Status::Ok ||\n        restore(home) != Status::Ok || query(home, &info) != Status::Ok ||\n        info.state != WindowState::Normal) return 58;\n    if (dismiss(home) != Status::Ok || query(home, &info) != Status::Ok ||\n        info.state != WindowState::Minimized) return 59;\n    SurfaceView home_surface{};\n    if (read_surface(home, &home_surface) != Status::Ok ||\n        home_surface.size != sizeof(home_payload) || home_surface.data == nullptr ||\n        home_surface.data[0] != UINT8_C(0x5a)) return 60;\n    const WindowId stale_home = home;\n    if (close(home) != Status::Ok || query(stale_home, &info) != Status::NotFound ||\n        read_surface(stale_home, &home_surface) != Status::NotFound) return 61;\n    WindowId next_home_slot = INVALID_WINDOW;\n    if (create_window("AfterHome", 18U, {110, 110, 300, 220},\n                      draw, receive, nullptr, &next_home_slot) != Status::Ok ||\n        next_home_slot == stale_home ||\n        read_surface(next_home_slot, &home_surface) != Status::InvalidState) return 62;\n    if (close(next_home_slot) != Status::Ok) return 63;\n    return 0;\n}''',
    )

    print("[dev-apply-flux-lifecycle] applied destroy/dismiss split and HOME logout")


if __name__ == "__main__":
    main()
