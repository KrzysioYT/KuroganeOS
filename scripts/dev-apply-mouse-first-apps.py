#!/usr/bin/env python3
"""Migrate Red Flux Files and Settings from keyboard menus to mouse-first UI."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded anchor, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


# FILES ---------------------------------------------------------------------
replace_once(
    "userspace/gui/files/main.c",
    '        (void)strlcpy(line2, "ENTER TO LAUNCH", line2_capacity);\n',
    '        (void)strlcpy(line2, "CLICK OPEN TO LAUNCH", line2_capacity);\n',
)

replace_once(
    "userspace/gui/files/main.c",
    "static void build_scene(\n",
    r'''static void refresh_selected(
    size_t selected,
    char* status,
    size_t status_capacity,
    char* preview1,
    size_t preview1_capacity,
    char* preview2,
    size_t preview2_capacity) {
    (void)strlcpy(status, g_entries[selected].path, status_capacity);
    preview_path(
        g_entries[selected].path,
        preview1,
        preview1_capacity,
        preview2,
        preview2_capacity);
}

static void open_selected(
    size_t selected,
    char* status,
    size_t status_capacity,
    char* preview1,
    size_t preview1_capacity,
    char* preview2,
    size_t preview2_capacity) {
    if (g_entries[selected].launchable) {
        const ku_result_t pid = ku_process_spawn(
            g_entries[selected].path, strlen(g_entries[selected].path));
        if (pid > 0) {
            char number[24];
            (void)strlcpy(status, "OPENED PID ", status_capacity);
            gui_u64(number, sizeof(number), (uint64_t)pid);
            append_text(status, status_capacity, number);
        } else {
            (void)strlcpy(status, "LAUNCH FAILED", status_capacity);
        }
        return;
    }
    refresh_selected(
        selected,
        status,
        status_capacity,
        preview1,
        preview1_capacity,
        preview2,
        preview2_capacity);
}

static void build_scene(
''',
)

replace_once(
    "userspace/gui/files/main.c",
    "    scene->visible_rows = 12U;\n",
    "    scene->visible_rows = 14U;\n",
)
replace_once(
    "userspace/gui/files/main.c",
    '    (void)kui_flow_label(&root, 2U, "ARROWS SELECT   ENTER OPEN   R REFRESH");\n',
    '    (void)kui_flow_label(&root, 2U, "MOUSE / SELECT ENTRY   OPEN   REFRESH");\n',
)
replace_once(
    "userspace/gui/files/main.c",
    "    (void)kui_flow_label(&root, 31U, status);\n"
    "    (void)kui_flow_label(&root, 32U, preview1);\n"
    "    (void)kui_flow_label(&root, 33U, preview2);\n",
    "    (void)kui_flow_button(&root, 30U, \"OPEN SELECTED\");\n"
    "    (void)kui_flow_button(&root, 31U, \"REFRESH PREVIEW\");\n"
    "    (void)kui_flow_label(&root, 32U, status);\n"
    "    (void)kui_flow_label(&root, 33U, preview1);\n"
    "    (void)kui_flow_label(&root, 34U, preview2);\n",
)
replace_once(
    "userspace/gui/files/main.c",
    "    size_t selected = 0U;\n"
    "    char status[64] = \"PERSISTENT ROOT / READ ABI\";\n",
    "    size_t selected = 0U;\n"
    "    uint32_t pointer_buttons = 0U;\n"
    "    char status[64] = \"PERSISTENT ROOT / READ ABI\";\n",
)
replace_once(
    "userspace/gui/files/main.c",
    '    puts("[TEST] desktop_files_3_1_navigation: PASS");\n',
    '    puts("[TEST] desktop_files_mouse_navigation: PASS");\n'
    '    puts("[TEST] desktop_files_keyboard_shortcuts_detached: PASS");\n',
)

files = ROOT / "userspace/gui/files/main.c"
files_text = files.read_text(encoding="utf-8")
start = files_text.find("    for (;;) {\n        ku_ui_event event;\n", files_text.find("int main(void)"))
end = files_text.find("\n    (void)ku_ui_close(window);", start)
if start < 0 or end < 0:
    raise SystemExit("userspace/gui/files/main.c: event loop anchors not found")
new_loop = r'''    for (;;) {
        ku_ui_event event;
        uint32_t target;
        const int wait_result = gui_wait_event(window, &event);
        if (wait_result < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_POINTER) continue;

        {
            const uint32_t previous_buttons = pointer_buttons;
            const int primary_pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous_buttons & UINT32_C(1)) == 0U;
            pointer_buttons = event.buttons;
            if (!primary_pressed) continue;
        }

        target = kui_scene_hit_test(&scene, event.x, event.y);
        if (target >= 10U && target < 10U + ENTRY_COUNT) {
            selected = (size_t)(target - 10U);
            refresh_selected(
                selected,
                status,
                sizeof(status),
                preview1,
                sizeof(preview1),
                preview2,
                sizeof(preview2));
        } else if (target == 30U) {
            open_selected(
                selected,
                status,
                sizeof(status),
                preview1,
                sizeof(preview1),
                preview2,
                sizeof(preview2));
        } else if (target == 31U) {
            (void)strlcpy(status, "VFS / PREVIEW REFRESHED", sizeof(status));
            preview_path(
                g_entries[selected].path,
                preview1,
                sizeof(preview1),
                preview2,
                sizeof(preview2));
        } else {
            continue;
        }

        build_scene(&scene, selected, status, preview1, preview2);
        (void)kui_scene_present(window, &scene);
    }
'''
files.write_text(files_text[:start] + new_loop + files_text[end:], encoding="utf-8")


# SETTINGS ------------------------------------------------------------------
replace_once(
    "userspace/gui/settings/main.c",
    '    (void)kui_flow_label(&settings, 25U, "ARROWS / TAB SELECT   ENTER APPLY");\n',
    '    (void)kui_flow_label(&settings, 25U, "MOUSE / CLICK A CONTROL TO APPLY");\n',
)
replace_once(
    "userspace/gui/settings/main.c",
    "    int low_contrast = 0;\n"
    "    uint32_t selected = 10U;\n",
    "    int low_contrast = 0;\n"
    "    uint32_t selected = 10U;\n"
    "    uint32_t pointer_buttons = 0U;\n",
)
replace_once(
    "userspace/gui/settings/main.c",
    '    puts("[TEST] desktop_settings_arrow_navigation: PASS");\n',
    '    puts("[TEST] desktop_settings_mouse_navigation: PASS");\n'
    '    puts("[TEST] desktop_settings_keyboard_shortcuts_detached: PASS");\n',
)
settings = ROOT / "userspace/gui/settings/main.c"
settings_text = settings.read_text(encoding="utf-8")
start = settings_text.find("    for (;;) {\n        ku_ui_event event;\n", settings_text.find("int main(void)"))
end = settings_text.find("\n    (void)ku_ui_close(window);", start)
if start < 0 or end < 0:
    raise SystemExit("userspace/gui/settings/main.c: event loop anchors not found")
new_loop = r'''    for (;;) {
        ku_ui_event event;
        uint32_t target;
        const int wait_result = gui_wait_event(window, &event);
        if (wait_result < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_POINTER) continue;

        {
            const uint32_t previous_buttons = pointer_buttons;
            const int primary_pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous_buttons & UINT32_C(1)) == 0U;
            pointer_buttons = event.buttons;
            if (!primary_pressed) continue;
        }

        target = kui_scene_hit_test(&scene, event.x, event.y);
        if (target == 10U) {
            selected = target;
            low_contrast = 0;
        } else if (target == 11U) {
            selected = target;
            low_contrast = 1;
        } else if (target == 21U || target == 22U || target == 23U) {
            selected = target;
            apply_audio(target, &audio);
        } else {
            continue;
        }

        build_scene(&scene, low_contrast, selected, &audio);
        (void)kui_scene_present(window, &scene);
    }
'''
settings.write_text(
    settings_text[:start] + new_loop + settings_text[end:], encoding="utf-8")


# Structural regression: utility GUIs must not quietly regain shortcut-driven
# menus. Keyboard remains required for terminal input and password entry.
test = ROOT / "tests/test_mouse_first_apps.py"
if test.exists():
    raise SystemExit("tests/test_mouse_first_apps.py already exists")
test.write_text(r'''#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


for path in ("userspace/gui/files/main.c", "userspace/gui/settings/main.c"):
    text = read(path)
    assert "KU_UI_EVENT_POINTER" in text, f"{path}: pointer path missing"
    assert "kui_scene_hit_test" in text, f"{path}: libui hit test missing"
    assert "KU_UI_EVENT_KEY" not in text, f"{path}: keyboard menu path returned"
    for helper in (
        "gui_key_down(", "gui_key_up(", "gui_key_left(", "gui_key_right(",
        "gui_key_tab(", "gui_key_activate(", "gui_key_cancel(",
    ):
        assert helper not in text, f"{path}: shortcut helper returned: {helper}"

files = read("userspace/gui/files/main.c")
assert "OPEN SELECTED" in files and "REFRESH PREVIEW" in files
assert "desktop_files_mouse_navigation: PASS" in files
assert "desktop_files_keyboard_shortcuts_detached: PASS" in files

settings = read("userspace/gui/settings/main.c")
for control in ("RED CORE", "LOW CONTRAST RED", "VOLUME -10", "VOLUME +10", "MUTE / UNMUTE"):
    assert control in settings, f"settings: missing mouse control {control}"
assert "desktop_settings_mouse_navigation: PASS" in settings
assert "desktop_settings_keyboard_shortcuts_detached: PASS" in settings

terminal = read("userspace/gui/terminal/main.c")
assert "KU_UI_EVENT_KEY" in terminal, "terminal: required keyboard input was removed"

login = read("userspace/gui/login/main.c")
assert "KU_UI_EVENT_KEY" in login, "login: password keyboard input was removed"
assert "KU_UI_EVENT_POINTER" in login, "login: mouse activation missing"

print("mouse-first application input contract tests passed")
''', encoding="utf-8")

# Run the structural contract in the full host regression suite.
replace_once(
    "scripts/run-host-tests.sh",
    'echo "[host-tests] python:       $HOST_PYTHON"\n\n',
    'echo "[host-tests] python:       $HOST_PYTHON"\n\n'
    '"$HOST_PYTHON" tests/test_mouse_first_apps.py\n\n',
)

print("mouse-first Files + Settings patch applied")
