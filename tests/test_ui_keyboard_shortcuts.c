#include <kurogane/ui.h>
#include <assert.h>

int main(void) {
#ifdef __cplusplus
    ku_ui_event e{};
#else
    ku_ui_event e = {0};
#endif
    e.structure_size = sizeof(e);
    e.type = KU_UI_EVENT_KEY;
    e.modifiers = KU_UI_MOD_CONTROL;
    e.key = KU_UI_KEY_C;
    e.character = 3U;
    assert(ku_ui_clipboard_shortcut(&e) == 'c');
    e.key = KU_UI_KEY_V;
    e.character = 22U;
    assert(ku_ui_clipboard_shortcut(&e) == 'v');
    e.key = KU_UI_KEY_A;
    e.character = 1U;
    assert(ku_ui_clipboard_shortcut(&e) == 'a');
    e.key = KU_UI_KEY_X;
    e.character = 24U;
    assert(ku_ui_clipboard_shortcut(&e) == 'x');
    e.modifiers = KU_UI_MOD_SHIFT;
    assert(ku_ui_clipboard_shortcut(&e) == 0);
    e.modifiers = KU_UI_MOD_CONTROL | KU_UI_MOD_ALT;
    assert(ku_ui_clipboard_shortcut(&e) == 'x');
    e.type = KU_UI_EVENT_SCROLL;
    e.wheel_y = -1;
    assert(ku_ui_clipboard_shortcut(&e) == 0);
    assert(ku_ui_clipboard_shortcut((const ku_ui_event*)0) == 0);
    return 0;
}
