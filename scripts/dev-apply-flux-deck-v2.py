#!/usr/bin/env python3
from pathlib import Path

source_path = Path(__file__).with_name("dev-apply-flux-deck.py")
source = source_path.read_text(encoding="utf-8")

old = '''    "enum class DockIcon : uint8_t {\\n    Home,\\n    Terminal,\\n    Files,\\n    Monitor,\\n    Settings,\\n    About,\\n};\\n",'''
new = '''    "enum class DockIcon : uint8_t {\\n    Home = 0,\\n    Terminal,\\n    Files,\\n    Monitor,\\n    Settings,\\n    About,\\n};\\n",'''

if source.count(old) != 1:
    raise SystemExit(f"Flux Deck v2: DockIcon patcher anchor count={source.count(old)}")
source = source.replace(old, new, 1)

# A normal desktop session should enter its application surface directly.
# The old developer-oriented behavior minimized HOME and auto-opened the
# Performance window, which made the graphical shell feel like a diagnostic
# demo and also obscured pointer-first startup. Patch the real production
# lifecycle and launcher while preserving the guarded exact-anchor model.
contract_anchor = "# ----- Contract tests: packet remains bounded, v1 stays supported, tiles are geometric.\n"
modern_session_patch = r'''# ----- Modern session entry: visible/focused HOME, no diagnostic autostart.
path = "kernel/ui/window_manager.cpp"
text = read(path)
text = replace_once(
    text,
    "    const bool home = is_home_surface(title);\n#ifndef KUROGANE_HOST_TEST\n    const bool login = text_equals(title, \"KUROGANE LOGIN\");\n",
    "#ifndef KUROGANE_HOST_TEST\n    const bool home = is_home_surface(title);\n    const bool login = text_equals(title, \"KUROGANE LOGIN\");\n",
    "host-only HOME variable guard")
text = replace_once(
    text,
    "    slot.info.state = (owner_pid == 0U || home)\n        ? WindowState::Minimized : WindowState::Normal;\n",
    "    slot.info.state = owner_pid == 0U\n        ? WindowState::Minimized : WindowState::Normal;\n",
    "HOME visible session entry")
text = replace_once(
    text,
    "    if (owner_pid != 0U && !home) g_focused = slot.info.id;\n",
    "    if (owner_pid != 0U) g_focused = slot.info.id;\n",
    "HOME focused session entry")
write(path, text)

path = "userspace/gui/launcher/main.c"
text = read(path)
text = replace_once(
    text,
    'static char g_status[64] = "APPS MENU / PERFORMANCE AUTOSTART";\n',
    'static char g_status[64] = "FLUX DECK / READY";\n',
    "remove developer autostart status")
text = replace_once(
    text,
    '    if (launch_app(2U, 1)) {\n'
    '        puts("[TEST] desktop_performance_autostart: PASS");\n'
    '    } else {\n'
    '        puts("[TEST] desktop_performance_autostart: FAIL");\n'
    '    }\n\n',
    '',
    "remove Performance autostart")
write(path, text)

path = "tests/test_window_manager.cpp"
text = read(path)
text = replace_once(
    text,
    '    if (create_window("RED FLUX HOME", 17U, {100, 100, 360, 260},\n'
    '                      draw, receive, nullptr, &home) != Status::Ok ||\n'
    '        query(home, &info) != Status::Ok ||\n'
    '        info.state != WindowState::Minimized) return 57;\n'
    '    uint8_t home_payload[16]{};\n'
    '    home_payload[0] = UINT8_C(0x5a);\n'
    '    if (present_surface(home, 4U, 4U, 4U, home_payload, sizeof(home_payload)) !=\n'
    '            Status::Ok ||\n'
    '        restore(home) != Status::Ok || query(home, &info) != Status::Ok ||\n'
    '        info.state != WindowState::Normal) return 58;\n',
    '    if (create_window("RED FLUX HOME", 17U, {100, 100, 360, 260},\n'
    '                      draw, receive, nullptr, &home) != Status::Ok ||\n'
    '        query(home, &info) != Status::Ok ||\n'
    '        info.state != WindowState::Normal || focused_window() != home) return 57;\n'
    '    uint8_t home_payload[16]{};\n'
    '    home_payload[0] = UINT8_C(0x5a);\n'
    '    if (present_surface(home, 4U, 4U, 4U, home_payload, sizeof(home_payload)) !=\n'
    '            Status::Ok || query(home, &info) != Status::Ok ||\n'
    '        info.state != WindowState::Normal) return 58;\n',
    "HOME visible lifecycle regression")
write(path, text)

path = "tests/test_mouse_first_apps.py"
text = read(path)
text = replace_once(
    text,
    'browser = read("userspace/gui/browser/main.c")\n',
    'window_manager = read("kernel/ui/window_manager.cpp")\n'
    'assert "slot.info.state = owner_pid == 0U" in window_manager, "desktop: HOME is still minimized on session entry"\n'
    'assert "owner_pid != 0U && !home" not in window_manager, "desktop: HOME does not receive initial focus"\n'
    'launcher_session = read("userspace/gui/launcher/main.c")\n'
    'assert "desktop_performance_autostart" not in launcher_session, "launcher: diagnostic Performance autostart returned"\n'
    'assert "FLUX DECK / READY" in launcher_session\n\n'
    'browser = read("userspace/gui/browser/main.c")\n',
    "modern desktop session contract")
write(path, text)

'''
if source.count(contract_anchor) != 1:
    raise SystemExit(
        f"Flux Deck v2: contract insertion anchor count={source.count(contract_anchor)}")
source = source.replace(contract_anchor, modern_session_patch + contract_anchor, 1)

compiled = compile(source, str(source_path), "exec")
exec(compiled, {"__name__": "__main__", "__file__": str(source_path)})
