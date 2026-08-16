#include "../common.h"
#include "../../../common/version.h"

#define INPUT_CAPACITY 48U
#define OUTPUT_LINES 5U
#define OUTPUT_CAPACITY 64U
#define JOB_CAPACITY 6U

static char g_output[OUTPUT_LINES][OUTPUT_CAPACITY];
static uint64_t g_jobs[JOB_CAPACITY];

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static void push_line(const char* text) {
    size_t line = 1U;
    while (line < OUTPUT_LINES) {
        (void)strlcpy(g_output[line - 1U], g_output[line], OUTPUT_CAPACITY);
        ++line;
    }
    (void)strlcpy(g_output[OUTPUT_LINES - 1U], text, OUTPUT_CAPACITY);
}

static void push_pid(const char* prefix, uint64_t value) {
    char line[OUTPUT_CAPACITY];
    char number[24];
    line[0] = '\0';
    gui_u64(number, sizeof(number), value);
    (void)strlcpy(line, prefix, sizeof(line));
    append_text(line, sizeof(line), number);
    push_line(line);
}

static void reap_jobs(void) {
    size_t index = 0U;
    while (index < JOB_CAPACITY) {
        if (g_jobs[index] != 0U) {
            int32_t status = 0;
            const ku_status_t result = ku_process_wait(g_jobs[index], &status);
            if (result == KU_STATUS_OK) {
                char line[OUTPUT_CAPACITY] = "job ";
                char number[24];
                gui_u64(number, sizeof(number), g_jobs[index]);
                append_text(line, sizeof(line), number);
                append_text(line, sizeof(line), " finished");
                push_line(line);
                g_jobs[index] = 0U;
            } else if (result != KU_STATUS_WOULD_BLOCK) {
                g_jobs[index] = 0U;
            }
        }
        ++index;
    }
}

static int job_slot_available(void) {
    size_t index = 0U;
    reap_jobs();
    while (index < JOB_CAPACITY) {
        if (g_jobs[index] == 0U) return 1;
        ++index;
    }
    return 0;
}

static int remember_job(uint64_t pid) {
    size_t index = 0U;
    reap_jobs();
    while (index < JOB_CAPACITY) {
        if (g_jobs[index] == 0U) {
            g_jobs[index] = pid;
            return 1;
        }
        ++index;
    }
    return 0;
}

static void launch_path(const char* path) {
    if (!job_slot_available()) {
        push_line("job table full");
        return;
    }
    const ku_result_t result = ku_process_spawn(path, strlen(path));
    if (result <= 0) {
        push_line("launch failed");
        return;
    }
    if (!remember_job((uint64_t)result)) {
        push_line("job tracking failure");
        return;
    }
    push_pid("launched pid=", (uint64_t)result);
}

static void launch_named(const char* root, const char* name) {
    char path[96];
    if (name == NULL || name[0] == '\0') {
        push_line("missing application name");
        return;
    }
    if (name[0] == '/') {
        (void)strlcpy(path, name, sizeof(path));
    } else {
        (void)strlcpy(path, root, sizeof(path));
        append_text(path, sizeof(path), name);
    }
    launch_path(path);
}

static void render(ku_window_t window, const char* input) {
    ku_ui_frame frame;
    char prompt[64] = "KRG::FLUX > ";
    kui_frame_initialize(&frame);
    frame.background_rgb = UINT32_C(0x0D1017);
    frame.foreground_rgb = UINT32_C(0xF0F4FC);
    frame.accent_rgb = UINT32_C(0x3EDCB5);
    (void)kui_frame_set_line(&frame, 0U, "KUROGANE FLUX // TERMINAL SURFACE");
    size_t index = 0U;
    while (index < OUTPUT_LINES) {
        (void)kui_frame_set_line(&frame, 2U + (uint32_t)index, g_output[index]);
        ++index;
    }
    append_text(prompt, sizeof(prompt), input);
    (void)kui_frame_set_line(&frame, 8U, prompt);
    (void)kui_frame_set_line(&frame, 9U, "help | run <app> | gui <surface> | jobs");
    (void)kui_present(window, &frame);
}

static void execute(char* input) {
    char* arguments = input;
    while (*arguments != '\0' && *arguments != ' ' && *arguments != '\t') ++arguments;
    if (*arguments != '\0') {
        *arguments++ = '\0';
        while (*arguments == ' ' || *arguments == '\t') ++arguments;
    }

    if (input[0] == '\0') {
        return;
    } else if (strcmp(input, "help") == 0) {
        push_line("help version pid apps run gui jobs echo about clear");
    } else if (strcmp(input, "version") == 0 || strcmp(input, "uname") == 0) {
        push_line(KUROGANE_PRODUCT_STRING " / Ring 3");
    } else if (strcmp(input, "pid") == 0) {
        push_pid("terminal pid=", ku_process_id());
    } else if (strcmp(input, "apps") == 0) {
        push_line("apps: shell hello external files monitor about");
        push_line("gui: terminal files sysmon settings about");
    } else if (strcmp(input, "run") == 0) {
        launch_named("/apps/", arguments);
    } else if (strcmp(input, "gui") == 0) {
        launch_named("/gui/", arguments);
    } else if (strcmp(input, "jobs") == 0) {
        size_t index = 0U;
        size_t active = 0U;
        reap_jobs();
        while (index < JOB_CAPACITY) {
            if (g_jobs[index] != 0U) {
                push_pid("running pid=", g_jobs[index]);
                ++active;
            }
            ++index;
        }
        if (active == 0U) push_line("jobs: none");
    } else if (strcmp(input, "echo") == 0) {
        push_line(arguments);
    } else if (strcmp(input, "about") == 0) {
        push_line("KuroganeOS " KUROGANE_VERSION_STRING " Desktop Developer Preview");
        push_line("Kurogane Flux / isolated Ring 3 terminal");
    } else if (strcmp(input, "clear") == 0) {
        size_t index = 0U;
        while (index < OUTPUT_LINES) g_output[index++][0] = '\0';
    } else {
        push_line("unknown command; type help");
    }
}

int main(void) {
    const ku_window_t window = gui_open("FLUX TERMINAL", 70, 360, 520, 320);
    if (window == KU_INVALID_WINDOW) return 1;
    puts("[TEST] desktop_terminal_ring3: PASS");

    size_t index = 0U;
    while (index < OUTPUT_LINES) g_output[index++][0] = '\0';
    push_line("Flux Terminal online.");
    push_line("Commands execute through the public Ring-3 ABI.");

    char input[INPUT_CAPACITY] = {0};
    size_t length = 0U;
    render(window, input);
    for (;;) {
        ku_ui_event event;
        reap_jobs();
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;
        const char character = (char)event.character;
        if (character == '\r' || character == '\n') {
            input[length] = '\0';
            execute(input);
            length = 0U;
            input[0] = '\0';
        } else if ((character == '\b' || character == 127) && length != 0U) {
            input[--length] = '\0';
        } else if (character == 0x15) {
            length = 0U;
            input[0] = '\0';
        } else if (character >= 32 && character <= 126 && length + 1U < sizeof(input)) {
            input[length++] = character;
            input[length] = '\0';
        }
        render(window, input);
    }
    (void)ku_ui_close(window);
    return 0;
}
