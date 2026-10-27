#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../userspace/gui/files/history.h"

static void test_back_forward_and_search(void) {
    flux_files_history history;
    const flux_files_history_entry* previous;
    assert(flux_files_history_init(&history, "/"));
    assert(history.count == 1U && history.current == 0U);
    assert(!flux_files_history_can_move(&history, -1));
    assert(!flux_files_history_can_move(&history, 1));

    assert(flux_files_history_filter(&history, "design"));
    assert(flux_files_history_visit(&history, "/apps"));
    assert(flux_files_history_visit(&history, "/apps"));
    assert(history.count == 2U && history.current == 1U);
    assert(flux_files_history_can_move(&history, -1));
    assert(!flux_files_history_can_move(&history, 1));
    previous = flux_files_history_peek(&history, -1);
    assert(previous != NULL && strcmp(previous->path, "/") == 0);
    assert(strcmp(previous->query, "design") == 0);
    assert(flux_files_history_move(&history, -1));
    assert(flux_files_history_can_move(&history, 1));
    previous = flux_files_history_peek(&history, 1);
    assert(previous != NULL && strcmp(previous->path, "/apps") == 0);

    assert(flux_files_history_visit(&history, "/etc"));
    assert(history.current == 1U && history.count == 2U);
    assert(strcmp(history.entries[1].path, "/etc") == 0);
    assert(!flux_files_history_can_move(&history, 1));
    assert(!flux_files_history_move(&history, 1));

    assert(!flux_files_history_visit(&history, "relative"));
    assert(history.count == 2U && history.current == 1U);
    {
        char too_long[FLUX_FILES_HISTORY_PATH_CAPACITY + 2U];
        memset(too_long, 'a', sizeof(too_long));
        too_long[0] = '/';
        too_long[sizeof(too_long) - 1U] = '\0';
        assert(!flux_files_history_visit(&history, too_long));
        assert(history.count == 2U);
    }
}

static void test_bounded_history(void) {
    flux_files_history history;
    char path[40];
    assert(flux_files_history_init(&history, "/"));
    for (size_t index = 0U; index < 26U; ++index) {
        (void)snprintf(path, sizeof(path), "/folder%u", (unsigned)index);
        assert(flux_files_history_visit(&history, path));
    }
    assert(history.count == FLUX_FILES_HISTORY_CAPACITY);
    assert(history.current == FLUX_FILES_HISTORY_CAPACITY - 1U);
    assert(strcmp(history.entries[0].path, "/folder10") == 0);
    assert(strcmp(history.entries[history.current].path, "/folder25") == 0);

    for (size_t index = 0U; index + 1U < FLUX_FILES_HISTORY_CAPACITY; ++index) {
        assert(flux_files_history_move(&history, -1));
    }
    assert(history.current == 0U);
    assert(!flux_files_history_move(&history, -1));
    assert(strcmp(history.entries[0].path, "/folder10") == 0);
}

int main(void) {
    test_back_forward_and_search();
    test_bounded_history();
    puts("Flux Files bounded Back/Forward navigation: PASS");
    return 0;
}
