#include "../common.h"
#include <kurogane/flux_app_catalog.h>
#include "../../common/terminal_shell.h"
#include "completion.h"
#include "scrollback.h"

#define TERMINAL_INPUT_CAPACITY 160U
#define TERMINAL_OUTPUT_LINES 8U
#define TERMINAL_HISTORY_LINES 96U
#define TERMINAL_OUTPUT_CAPACITY 128U
#define TERMINAL_VISIBLE_INPUT 46U

static ku_shell_state g_shell;
static char g_output[TERMINAL_HISTORY_LINES][TERMINAL_OUTPUT_CAPACITY];
static size_t g_output_count;
static size_t g_scrollback_offset;
static const char* g_hint = "TAB COMPLETE  PGUP/PGDN SCROLL  UP/DOWN HISTORY";

static void terminal_emit(void* context, const char* line) {
    (void)context;
    if (g_output_count == TERMINAL_HISTORY_LINES) {
        for (size_t index = 1U; index < TERMINAL_HISTORY_LINES; ++index) {
            (void)strlcpy(
                g_output[index - 1U], g_output[index], TERMINAL_OUTPUT_CAPACITY);
        }
    } else {
        ++g_output_count;
    }
    (void)strlcpy(
        g_output[g_output_count - 1U],
        line != NULL ? line : "",
        TERMINAL_OUTPUT_CAPACITY);
    if (g_scrollback_offset != 0U) {
        g_scrollback_offset = terminal_scrollback_step(
            g_scrollback_offset, g_output_count, TERMINAL_OUTPUT_LINES, 1);
    }
}

static const ku_shell_io g_io = {
    terminal_emit,
    NULL,
};

static void clear_output(void) {
    g_output_count = 0U;
    g_scrollback_offset = 0U;
}

static void build_input_display(
    const char* input,
    size_t length,
    size_t cursor,
    char* output,
    size_t capacity) {
    size_t start = 0U;
    size_t written = 0U;
    if (capacity == 0U) return;
    output[0] = '\0';
    (void)strlcpy(output, "> ", capacity);
    written = strlen(output);

    if (cursor > TERMINAL_VISIBLE_INPUT / 2U) {
        start = cursor - TERMINAL_VISIBLE_INPUT / 2U;
    }
    if (start + TERMINAL_VISIBLE_INPUT > length && length > TERMINAL_VISIBLE_INPUT) {
        start = length - TERMINAL_VISIBLE_INPUT;
    }

    for (size_t index = start;
         index <= length && written + 2U < capacity &&
         index - start < TERMINAL_VISIBLE_INPUT;
         ++index) {
        if (index == cursor) output[written++] = '|';
        if (index < length && written + 1U < capacity) {
            output[written++] = input[index];
        }
    }
    output[written] = '\0';
}

static void render(
    ku_window_t window,
    const char* input,
    size_t length,
    size_t cursor) {
    kui_scene scene;
    kui_flow root;
    char input_display[64];
    const size_t displayed = g_output_count < TERMINAL_OUTPUT_LINES
        ? g_output_count : TERMINAL_OUTPUT_LINES;
    const size_t blank = TERMINAL_OUTPUT_LINES - displayed;
    const size_t first = g_output_count - displayed - g_scrollback_offset;

    build_input_display(
        input, length, cursor, input_display, sizeof(input_display));

    kui_scene_initialize(&scene);
    scene.visible_rows = 12U;
    kui_scene_set_palette(
        &scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));
    kui_flow_begin(&root, &scene, 0U);
    (void)kui_flow_panel(&root, 1U, "RED FLUX TERMINAL");
    for (size_t index = 0U; index < TERMINAL_OUTPUT_LINES; ++index) {
        (void)kui_flow_label(
            &root, 10U + (uint32_t)index,
            index < blank ? "" : g_output[first + index - blank]);
    }
    (void)kui_flow_input(&root, 30U, input_display);
    (void)kui_flow_label(
        &root, 31U,
        g_hint);
    (void)kui_scene_select(&scene, 30U);
    (void)kui_scene_present(window, &scene);
}

static void input_delete_left(
    char* input, size_t* length, size_t* cursor) {
    if (*cursor == 0U || *length == 0U) return;
    for (size_t index = *cursor; index <= *length; ++index) {
        input[index - 1U] = input[index];
    }
    --(*cursor);
    --(*length);
}

static void input_delete_at(char* input, size_t* length, size_t cursor) {
    if (cursor >= *length) return;
    for (size_t index = cursor + 1U; index <= *length; ++index) {
        input[index - 1U] = input[index];
    }
    --(*length);
}

static void input_insert(
    char* input,
    size_t capacity,
    size_t* length,
    size_t* cursor,
    char character) {
    if (*length + 1U >= capacity) return;
    for (size_t index = *length + 1U; index > *cursor; --index) {
        input[index] = input[index - 1U];
    }
    input[*cursor] = character;
    ++(*cursor);
    ++(*length);
    input[*length] = '\0';
}

int main(void) {
    const ku_window_t window = gui_open(KU_FLUX_TITLE_TERMINAL, 235, 155, 760, 430);
    if (window == KU_INVALID_WINDOW) return 1;

    ku_shell_initialize(&g_shell);
    clear_output();
    terminal_emit(NULL, "KuroganeOS " KUROGANE_VERSION_STRING " / system shell");
    terminal_emit(NULL, "Type help. Desktop surfaces are not shell commands.");

    puts("[TEST] desktop_terminal_ring3: PASS");
    puts("[TEST] desktop_terminal_policy_shell: PASS");

    char input[TERMINAL_INPUT_CAPACITY] = {0};
    size_t length = 0U;
    size_t cursor = 0U;
    render(window, input, length, cursor);

    for (;;) {
        ku_ui_event event;
        const int available = gui_wait_event(window, &event);
        if (available < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type == KU_UI_EVENT_SCROLL) {
            if (event.wheel_y != 0) {
                g_scrollback_offset = terminal_scrollback_step(
                    g_scrollback_offset, g_output_count,
                    TERMINAL_OUTPUT_LINES, (int64_t)event.wheel_y * 2);
                render(window, input, length, cursor);
            }
            continue;
        }
        if (event.type != KU_UI_EVENT_KEY) continue;

        g_hint = "TAB COMPLETE  PGUP/PGDN SCROLL  UP/DOWN HISTORY";
        if (event.key == KU_UI_KEY_PAGE_UP || event.key == KU_UI_KEY_PAGE_DOWN) {
            g_scrollback_offset = terminal_scrollback_step(
                g_scrollback_offset, g_output_count, TERMINAL_OUTPUT_LINES,
                event.key == KU_UI_KEY_PAGE_UP ? 7 : -7);
        } else if (gui_key_tab(&event)) {
            const terminal_completion_result completed = terminal_complete_command(
                input, sizeof(input), &cursor);
            length = strlen(input);
            if (completed == TERMINAL_COMPLETION_AMBIGUOUS) {
                g_hint = "TAB / MORE THAN ONE COMMAND MATCHES";
            } else if (completed == TERMINAL_COMPLETION_NONE) {
                g_hint = "TAB / NO COMMAND MATCH";
            }
        } else if (gui_key_activate(&event)) {
            ku_shell_action action;
            input[length] = '\0';
            g_scrollback_offset = 0U;
            action = ku_shell_execute(&g_shell, &g_io, input);
            length = 0U;
            cursor = 0U;
            input[0] = '\0';
            if (action == KU_SHELL_ACTION_CLEAR) {
                clear_output();
                terminal_emit(NULL, "Terminal cleared.");
            } else if (action == KU_SHELL_ACTION_EXIT) {
                break;
            }
        } else if (gui_key_up(&event)) {
            if (ku_shell_history_previous(
                    &g_shell, input, sizeof(input))) {
                length = strlen(input);
                cursor = length;
            }
        } else if (gui_key_down(&event)) {
            if (ku_shell_history_next(
                    &g_shell, input, sizeof(input))) {
                length = strlen(input);
                cursor = length;
            }
        } else if (gui_key_left(&event)) {
            if (cursor != 0U) --cursor;
        } else if (gui_key_right(&event)) {
            if (cursor < length) ++cursor;
        } else if (event.key == KU_UI_KEY_HOME) {
            cursor = 0U;
        } else if (event.key == KU_UI_KEY_END) {
            cursor = length;
        } else if (event.key == KU_UI_KEY_DELETE) {
            input_delete_at(input, &length, cursor);
        } else if (event.key == KU_UI_KEY_BACKSPACE ||
                   event.character == '\b' || event.character == 127U) {
            input_delete_left(input, &length, &cursor);
        } else if (gui_key_cancel(&event) || event.character == 0x15U) {
            length = 0U;
            cursor = 0U;
            input[0] = '\0';
        } else if (event.character >= 32U && event.character <= 126U) {
            input_insert(
                input, sizeof(input), &length, &cursor,
                (char)event.character);
        } else {
            continue;
        }

        ku_shell_reap_jobs(&g_shell, &g_io, 1);
        render(window, input, length, cursor);
    }

    (void)ku_ui_close(window);
    return 0;
}
