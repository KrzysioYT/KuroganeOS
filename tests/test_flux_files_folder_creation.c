#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../userspace/gui/files/folder_creation.h"

static ku_ui_event key(uint32_t physical, uint32_t character, uint32_t modifiers) {
    ku_ui_event event;
    memset(&event, 0, sizeof(event));
    event.type = KU_UI_EVENT_KEY;
    event.key = physical;
    event.character = character;
    event.modifiers = modifiers;
    return event;
}

static void test_valid_names(void) {
    assert(flux_files_folder_valid_name("Projects", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(flux_files_folder_valid_name("My Projects", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(flux_files_folder_valid_name(".config", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(flux_files_folder_valid_name("Dev_8-0", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(!flux_files_folder_valid_name("", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(!flux_files_folder_valid_name(".", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(!flux_files_folder_valid_name("..", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(!flux_files_folder_valid_name("../other", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(!flux_files_folder_valid_name("dir/subdir", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(!flux_files_folder_valid_name("folder\\other", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(!flux_files_folder_valid_name(" leading", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(!flux_files_folder_valid_name("trailing ", FLUX_FILES_NEW_FOLDER_CAPACITY));
    assert(!flux_files_folder_valid_name(NULL, FLUX_FILES_NEW_FOLDER_CAPACITY));
    {
        char no_terminator[4] = {'a', 'b', 'c', 'd'};
        assert(!flux_files_folder_valid_name(no_terminator, sizeof(no_terminator)));
    }
}

static void test_editing_and_confirmation(void) {
    char folder[FLUX_FILES_NEW_FOLDER_CAPACITY] = "";
    ku_ui_event event = key(KU_UI_KEY_A, 'D', 0U);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_EDITED);
    event = key(KU_UI_KEY_A, 'e', 0U);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_EDITED);
    assert(strcmp(folder, "De") == 0);

    event = key(KU_UI_KEY_A, '/', 0U);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_INVALID);
    assert(strcmp(folder, "De") == 0);
    event = key(KU_UI_KEY_UNKNOWN, 'v', 0U);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_NONE);
    event = key(KU_UI_KEY_A, 'v', KU_UI_MOD_CONTROL);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_NONE);
    event = key(KU_UI_KEY_A, 'v', KU_UI_MOD_ALT);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_NONE);
    event = key(KU_UI_KEY_A, 0x105U, 0U);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_INVALID);

    event = key(KU_UI_KEY_BACKSPACE, 0U, 0U);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_EDITED);
    assert(strcmp(folder, "D") == 0);
    event = key(KU_UI_KEY_ENTER, 0U, 0U);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_SUBMIT);
    assert(strcmp(folder, "D") == 0);
    event = key(KU_UI_KEY_ESCAPE, 0U, 0U);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_CANCEL);
}

static void test_bounded_name(void) {
    char folder[4] = "abc";
    ku_ui_event event = key(KU_UI_KEY_A, 'd', 0U);
    assert(flux_files_folder_key(folder, sizeof(folder), &event) ==
           FLUX_FILES_FOLDER_FULL);
    assert(strcmp(folder, "abc") == 0);
    assert(flux_files_folder_valid_name(folder, sizeof(folder)));
}

int main(void) {
    test_valid_names();
    test_editing_and_confirmation();
    test_bounded_name();
    puts("Flux Files explicit New Folder input policy: PASS");
    return 0;
}
