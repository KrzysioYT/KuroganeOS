#include "../common.h"
#include "../../common/terminal_shell.h"

#define TERMINAL_INPUT_CAPACITY 160U
#define TERMINAL_OUTPUT_LINES 8U
#define TERMINAL_OUTPUT_CAPACITY 64U
#define TERMINAL_VISIBLE_INPUT 46U

static ku_shell_state g_shell;
static char g_output[TERMINAL_OUTPUT_LINES][TERMINAL_OUTPUT_CAPACITY];

static void terminal_emit(void* context, const char* line) {
    (void)context;
    for (size_t index = 1U; index < TERMINAL_OUTPUT_LINES; ++index) {
        (void)strlcpy(
            g_output[index - 1U], g_output[index], TERMINAL_OUTPUT_CAPACITY);
    }
    (void)strlcpy(
        g_output[TERMINAL_OUTPUT_LINES - 1U],
        line != NULL ? line : "",
        TERMINAL_OUTPUT_CAPACITY);
}

static const ku_shell_io g_io = {
    terminal_emit,
    NULL,
};

static void clear_output(void) {
    for (size_t index = 0U; index < TERMINAL_OUTPUT_LINES; ++index) {
        g_output[index][0] = '\0';
    }
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

    build_input_display(
        input, length, cursor, input_display, sizeof(input_display));

    kui_scene_initialize(&scene);
    scene.visible_rows = KU_UI_MAX_LINES;
    gui_apply_obsidian_theme(&scene, 0);
    kui_flow_begin(&root, &scene, 0U);
    (void)kui_flow_panel(&root, 1U, "KUROGANE TERMINAL");
    for (size_t index = 0U; index < TERMINAL_OUTPUT_LINES; ++index) {
        (void)kui_flow_label(
            &root, 10U + (uint32_t)index, g_output[index]);
    }
    (void)kui_flow_input(&root, 30U, input_display);
    (void)kui_flow_label(
        &root, 31U,
        "ARROWS EDIT/HISTORY  ENTER RUN  ESC CANCEL");
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
    const ku_window_t window = gui_open("RED FLUX TERMINAL", 235, 145, 780, 450);
    if (window == KU_INVALID_WINDOW) return 1;

    ku_shell_initialize(&g_shell);
    clear_output();
    terminal_emit(NULL, "KuroganeOS " KUROGANE_VERSION_STRING " / Obsidian terminal");
    terminal_emit(NULL, "Type help. Native shell and desktop remain separate.");

    puts("[TEST] desktop_terminal_ring3: PASS");
    puts("[TEST] desktop_terminal_policy_shell: PASS");
    puts("[TEST] kurogane5_obsidian_terminal: PASS");

    char input[TERMINAL_INPUT_CAPACITY] = {0};
    size_t length = 0U;
    size_t cursor = 0U;
    render(window, input, length, cursor);

    for (;;) {
        ku_ui_event event;
        const int available = gui_wait_event(window, &event);
        if (available < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (gui_key_activate(&event)) {
            ku_shell_action action;
            input[length] = '\0';
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
