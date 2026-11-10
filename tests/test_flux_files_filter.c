#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../userspace/gui/files/filter.h"

static ku_ui_event key(uint32_t physical, uint32_t character, uint32_t modifiers) {
    ku_ui_event event;
    memset(&event, 0, sizeof(event));
    event.type = KU_UI_EVENT_KEY;
    event.key = physical;
    event.character = character;
    event.modifiers = modifiers;
    return event;
}

int main(void) {
    char query[FLUX_FILES_FILTER_CAPACITY] = "";
    ku_ui_event event = key(KU_UI_KEY_A, 'R', 0U);

    assert(flux_files_matches_filter("ReadMe.TXT", "read"));
    assert(flux_files_matches_filter("project/Design", "DESIGN"));
    assert(flux_files_matches_filter("Polski_zażółć.txt", "txt"));
    assert(flux_files_matches_filter("whatever", ""));
    assert(!flux_files_matches_filter("README.md", "image"));
    assert(!flux_files_matches_filter("", "not-empty"));
    assert(!flux_files_matches_filter(NULL, ""));

    assert(flux_files_filter_edit(query, sizeof(query), &event));
    assert(strcmp(query, "R") == 0);
    event = key(KU_UI_KEY_A, 'e', 0U);
    assert(flux_files_filter_edit(query, sizeof(query), &event));
    assert(strcmp(query, "Re") == 0);

    event = key(KU_UI_KEY_C, 'c', KU_UI_MOD_CONTROL);
    assert(!flux_files_filter_edit(query, sizeof(query), &event));
    assert(strcmp(query, "Re") == 0);
    event = key(KU_UI_KEY_A, 'a', KU_UI_MOD_ALT);
    assert(!flux_files_filter_edit(query, sizeof(query), &event));
    event = key(KU_UI_KEY_BACKSPACE, 0U, KU_UI_MOD_CONTROL);
    assert(!flux_files_filter_edit(query, sizeof(query), &event));
    assert(strcmp(query, "Re") == 0);
    event = key(KU_UI_KEY_UNKNOWN, 'f', 0U);
    assert(!flux_files_filter_edit(query, sizeof(query), &event));
    event = key(KU_UI_KEY_A, 0x105U, 0U);
    assert(!flux_files_filter_edit(query, sizeof(query), &event));

    event = key(KU_UI_KEY_BACKSPACE, 0U, 0U);
    assert(flux_files_filter_edit(query, sizeof(query), &event));
    assert(strcmp(query, "R") == 0);
    event = key(KU_UI_KEY_ESCAPE, 0U, 0U);
    assert(flux_files_filter_edit(query, sizeof(query), &event));
    assert(strcmp(query, "") == 0);
    assert(!flux_files_filter_edit(query, sizeof(query), &event));
    event = key(KU_UI_KEY_BACKSPACE, 0U, 0U);
    assert(!flux_files_filter_edit(query, sizeof(query), &event));

    {
        char tight[3] = "ab";
        event = key(KU_UI_KEY_A, 'c', 0U);
        assert(!flux_files_filter_edit(tight, sizeof(tight), &event));
        assert(strcmp(tight, "ab") == 0);
    }
    puts("Flux Files instant folder filtering: PASS");
    return 0;
}
