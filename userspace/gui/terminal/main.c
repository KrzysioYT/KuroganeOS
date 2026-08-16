#include "../common.h"

#define INPUT_CAPACITY 40U

static void render(
    ku_window_t window, const char* history, const char* input) {
    ku_ui_frame frame;
    char prompt[64] = "kurogane:gui$ ";
    kui_frame_initialize(&frame);
    (void)kui_frame_set_line(&frame, 0U, "TERMINAL - RING 3");
    (void)kui_frame_set_line(&frame, 1U, "Commands: help pid about clear");
    (void)kui_frame_set_line(&frame, 3U, history);
    (void)strlcpy(prompt + strlen(prompt), input, sizeof(prompt) - strlen(prompt));
    (void)kui_frame_set_line(&frame, 5U, prompt);
    (void)kui_frame_set_line(&frame, 7U, "This terminal uses only the public SDK ABI.");
    (void)kui_present(window, &frame);
}

int main(void) {
    const ku_window_t window = gui_open("TERMINAL", 70, 380, 480, 300);
    if (window == KU_INVALID_WINDOW) return 1;
    puts("[TEST] desktop_terminal_ring3: PASS");
    char input[INPUT_CAPACITY] = {0};
    size_t length = 0U;
    char history[64] = "Ready.";
    render(window, history, input);
    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 ||
            event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;
        const char character = (char)event.character;
        if (character == '\r' || character == '\n') {
            input[length] = '\0';
            if (strcmp(input, "help") == 0) {
                (void)strlcpy(history, "help pid about clear", sizeof(history));
            } else if (strcmp(input, "pid") == 0) {
                char number[24];
                gui_u64(number, sizeof(number), ku_process_id());
                (void)strlcpy(history, "Terminal PID: ", sizeof(history));
                (void)strlcpy(history + strlen(history), number,
                              sizeof(history) - strlen(history));
            } else if (strcmp(input, "about") == 0) {
                (void)strlcpy(history, "KuroganeOS 2.0 Desktop Alpha", sizeof(history));
            } else if (strcmp(input, "clear") == 0) {
                history[0] = '\0';
            } else if (length != 0U) {
                (void)strlcpy(history, "Command not found", sizeof(history));
            }
            length = 0U;
            input[0] = '\0';
        } else if ((character == '\b' || character == 127) && length != 0U) {
            input[--length] = '\0';
        } else if (character >= 32 && character <= 126 &&
                   length + 1U < sizeof(input)) {
            input[length++] = character;
            input[length] = '\0';
        }
        render(window, history, input);
    }
    (void)ku_ui_close(window);
    return 0;
}
