#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../userspace/gui/terminal/completion.h"
#include "../userspace/gui/terminal/scrollback.h"

static void test_command_completion(void) {
    char line[40] = "cle";
    size_t caret = 3U;
    assert(terminal_complete_command(line, sizeof(line), &caret) ==
           TERMINAL_COMPLETION_APPLIED);
    assert(strcmp(line, "clear ") == 0);
    assert(caret == 6U);

    strcpy(line, "he");
    caret = 2U;
    assert(terminal_complete_command(line, sizeof(line), &caret) ==
           TERMINAL_COMPLETION_APPLIED);
    assert(strcmp(line, "hel") == 0);
    assert(caret == 3U);
    assert(terminal_complete_command(line, sizeof(line), &caret) ==
           TERMINAL_COMPLETION_AMBIGUOUS);

    strcpy(line, "  who");
    caret = strlen(line);
    assert(terminal_complete_command(line, sizeof(line), &caret) ==
           TERMINAL_COMPLETION_APPLIED);
    assert(strcmp(line, "  whoami ") == 0);

    strcpy(line, "ls /apps");
    caret = 2U;
    assert(terminal_complete_command(line, sizeof(line), &caret) ==
           TERMINAL_COMPLETION_APPLIED);
    assert(strcmp(line, "ls /apps") == 0);
    assert(caret == 2U);

    strcpy(line, "unknown");
    caret = strlen(line);
    assert(terminal_complete_command(line, sizeof(line), &caret) ==
           TERMINAL_COMPLETION_NONE);
    assert(strcmp(line, "unknown") == 0);

    strcpy(line, "cle");
    caret = 2U;
    assert(terminal_complete_command(line, sizeof(line), &caret) ==
           TERMINAL_COMPLETION_NONE);
    assert(strcmp(line, "cle") == 0);

    {
        char tight[6] = "cle";
        caret = 3U;
        assert(terminal_complete_command(tight, sizeof(tight), &caret) ==
               TERMINAL_COMPLETION_NONE);
        assert(strcmp(tight, "cle") == 0);
    }
    {
        char no_terminator[3] = {'c', 'l', 'e'};
        caret = 3U;
        assert(terminal_complete_command(
                   no_terminator, sizeof(no_terminator), &caret) ==
               TERMINAL_COMPLETION_NONE);
    }
}

static void test_scrollback_bounds(void) {
    assert(terminal_scrollback_step(0U, 20U, 8U, 7) == 7U);
    assert(terminal_scrollback_step(7U, 20U, 8U, 70) == 12U);
    assert(terminal_scrollback_step(7U, 20U, 8U, -3) == 4U);
    assert(terminal_scrollback_step(7U, 20U, 8U, INT64_MIN) == 0U);
    assert(terminal_scrollback_step(7U, 20U, 8U, INT64_MAX) == 12U);
    assert(terminal_scrollback_step(6U, 3U, 8U, 1) == 0U);
    assert(terminal_scrollback_step(2U, 9U, 8U, 0) == 1U);
}

int main(void) {
    test_command_completion();
    test_scrollback_bounds();
    puts("Terminal Tab completion and scrollback bounds: PASS");
    return 0;
}
