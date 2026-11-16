#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../userspace/gui/window_profile.h"

static ku_ui_window_geometry sample_geometry(void) {
    ku_ui_window_geometry g = {0};
    g.structure_size = sizeof(g);
    g.x = 250; g.y = 75; g.width = 650; g.height = 630;
    g.state = KU_UI_WINDOW_NORMAL;
    return g;
}

int main(void) {
    ku_ui_window_geometry original = sample_geometry();
    ku_ui_window_geometry result = {0};
    char encoded[GUI_WINDOW_PROFILE_LENGTH];
    char damaged[GUI_WINDOW_PROFILE_LENGTH];

    assert(gui_window_profile_encode(&original, encoded));
    assert(encoded[0] == 'W' && encoded[1] == '1' && encoded[42] == '\0');
    assert(gui_window_profile_decode(encoded, sizeof(encoded), &result));
    assert(gui_window_same_geometry(&original, &result));

    memcpy(damaged, encoded, sizeof(damaged));
    damaged[3] = damaged[3] == '0' ? '1' : '0';
    assert(!gui_window_profile_decode(damaged, sizeof(damaged), &result));
    memcpy(damaged, encoded, sizeof(damaged));
    damaged[40] = damaged[40] == '0' ? '1' : '0';
    assert(!gui_window_profile_decode(damaged, sizeof(damaged), &result));
    memcpy(damaged, encoded, sizeof(damaged));
    damaged[0] = 'X';
    assert(!gui_window_profile_decode(damaged, sizeof(damaged), &result));
    memcpy(damaged, encoded, sizeof(damaged));
    damaged[2] = 'Z';
    assert(!gui_window_profile_decode(damaged, sizeof(damaged), &result));
    assert(!gui_window_profile_decode(encoded, sizeof(encoded) - 1U, &result));
    assert(!gui_window_profile_decode(NULL, sizeof(encoded), &result));
    assert(!gui_window_profile_decode(encoded, sizeof(encoded), NULL));

    original.width = 0;
    assert(!gui_window_profile_encode(&original, encoded));
    original = sample_geometry();
    original.height = 159;
    assert(!gui_window_profile_encode(&original, encoded));
    original = sample_geometry();
    original.x = -1;
    assert(!gui_window_profile_encode(&original, encoded));
    original = sample_geometry();
    original.width = 16385;
    assert(!gui_window_profile_encode(&original, encoded));

    original = sample_geometry();
    original.x = 1024; original.y = 768;
    original.width = 1920; original.height = 1080;
    assert(gui_window_profile_encode(&original, encoded));
    assert(gui_window_profile_decode(encoded, sizeof(encoded), &result));
    assert(gui_window_same_geometry(&original, &result));
    result.width += 1;
    assert(!gui_window_same_geometry(&original, &result));

    return 0;
}
