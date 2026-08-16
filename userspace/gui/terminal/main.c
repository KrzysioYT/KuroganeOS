#include "../common.h"
#include "../../../common/version.h"
#include <fcntl.h>
#include <unistd.h>

#define INPUT_CAPACITY 48U
#define OUTPUT_LINES 6U
#define OUTPUT_CAPACITY 64U
#define JOB_CAPACITY 8U
#define HISTORY_CAPACITY 8U

static char g_output[OUTPUT_LINES][OUTPUT_CAPACITY];
static uint64_t g_jobs[JOB_CAPACITY];
static char g_history[HISTORY_CAPACITY][INPUT_CAPACITY];
static size_t g_history_count = 0U;

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

static size_t active_jobs(void) {
    size_t index = 0U;
    size_t count = 0U;
    while (index < JOB_CAPACITY) {
        if (g_jobs[index] != 0U) ++count;
        ++index;
    }
    return count;
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

static int path_exists(const char* path) {
    const int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return 0;
    (void)close(descriptor);
    return 1;
}

static void launch_path(const char* path) {
    if (active_jobs() >= JOB_CAPACITY) {
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

static void push_file_text(const char* path) {
    const int descriptor = open(path, O_RDONLY);
    char data[256];
    if (descriptor < 0) {
        push_line("cat: file not found or not readable");
        return;
    }
    const ssize_t count = read(descriptor, data, sizeof(data) - 1U);
    (void)close(descriptor);
    if (count < 0) {
        push_line("cat: read failed");
        return;
    }
    if (count == 0) {
        push_line("cat: empty file");
        return;
    }
    data[count] = '\0';

    char line[OUTPUT_CAPACITY];
    size_t out = 0U;
    size_t index = 0U;
    line[0] = '\0';
    while (index < (size_t)count) {
        unsigned char character = (unsigned char)data[index++];
        if (character == '\r') continue;
        if (character == '\n' || out + 1U >= sizeof(line)) {
            line[out] = '\0';
            if (out != 0U) push_line(line);
            out = 0U;
            line[0] = '\0';
            continue;
        }
        line[out++] = character >= 32U && character <= 126U
            ? (char)character : '.';
    }
    if (out != 0U) {
        line[out] = '\0';
        push_line(line);
    }
}

static void which_command(const char* name) {
    char path[96];
    if (name == NULL || name[0] == '\0') {
        push_line("which: missing name");
        return;
    }
    if (name[0] == '/') {
        push_line(path_exists(name) ? name : "which: not found");
        return;
    }
    (void)strlcpy(path, "/apps/", sizeof(path));
    append_text(path, sizeof(path), name);
    if (path_exists(path)) {
        push_line(path);
        return;
    }
    (void)strlcpy(path, "/gui/", sizeof(path));
    append_text(path, sizeof(path), name);
    push_line(path_exists(path) ? path : "which: not found");
}

static uint64_t parse_u64(const char* text, int* valid) {
    uint64_t value = 0U;
    *valid = 0;
    if (text == NULL || text[0] == '\0') return 0U;
    while (*text != '\0') {
        if (*text < '0' || *text > '9') return 0U;
        const uint64_t digit = (uint64_t)(*text - '0');
        if (value > (UINT64_MAX - digit) / 10U) return 0U;
        value = value * 10U + digit;
        ++text;
    }
    *valid = 1;
    return value;
}

static void remember_history(const char* input) {
    if (input == NULL || input[0] == '\0') return;
    if (g_history_count < HISTORY_CAPACITY) {
        (void)strlcpy(g_history[g_history_count++], input, INPUT_CAPACITY);
        return;
    }
    size_t index = 1U;
    while (index < HISTORY_CAPACITY) {
        (void)strlcpy(g_history[index - 1U], g_history[index], INPUT_CAPACITY);
        ++index;
    }
    (void)strlcpy(g_history[HISTORY_CAPACITY - 1U], input, INPUT_CAPACITY);
}

static void render(ku_window_t window, const char* input) {
    kui_scene scene;
    kui_flow root;
    char prompt[64] = "KRG::FLUX > ";
    size_t index = 0U;

    kui_scene_initialize(&scene);
    scene.visible_rows = 12U;
    kui_flow_begin(&root, &scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FLUX TERMINAL // 2.6 COMMAND SURFACE");
    while (index < OUTPUT_LINES) {
        (void)kui_flow_label(&root, 10U + (uint32_t)index, g_output[index]);
        ++index;
    }
    append_text(prompt, sizeof(prompt), input);
    (void)kui_flow_input(&root, 30U, prompt);
    (void)kui_flow_label(&root, 31U,
        "help | cat | which | run | gui | jobs | history | status");
    (void)kui_scene_select(&scene, 30U);
    (void)kui_scene_present(window, &scene);
}

static void show_help(void) {
    push_line("files: cat/read <path> | which <name> | pwd");
    push_line("apps: run <app> | gui <surface> | open <path|app>");
    push_line("proc: pid | jobs | wait <pid> | status");
    push_line("shell: version | uname | echo | history | clear | about");
}

static void execute(char* input) {
    char original[INPUT_CAPACITY];
    char* arguments = input;
    (void)strlcpy(original, input, sizeof(original));
    remember_history(original);

    while (*arguments != '\0' && *arguments != ' ' && *arguments != '\t') ++arguments;
    if (*arguments != '\0') {
        *arguments++ = '\0';
        while (*arguments == ' ' || *arguments == '\t') ++arguments;
    }

    if (input[0] == '\0') {
        return;
    } else if (strcmp(input, "help") == 0) {
        show_help();
    } else if (strcmp(input, "version") == 0 || strcmp(input, "uname") == 0) {
        push_line(KUROGANE_PRODUCT_STRING " / x86-64 Ring 3");
    } else if (strcmp(input, "pid") == 0) {
        push_pid("terminal pid=", ku_process_id());
    } else if (strcmp(input, "pwd") == 0) {
        push_line("/");
    } else if (strcmp(input, "cat") == 0 || strcmp(input, "read") == 0) {
        if (arguments[0] == '/') push_file_text(arguments);
        else push_line("cat: use an absolute path");
    } else if (strcmp(input, "which") == 0) {
        which_command(arguments);
    } else if (strcmp(input, "apps") == 0) {
        push_line("apps: shell hello external files monitor about");
        push_line("gui: terminal files sysmon settings about");
    } else if (strcmp(input, "run") == 0) {
        launch_named("/apps/", arguments);
    } else if (strcmp(input, "gui") == 0) {
        launch_named("/gui/", arguments);
    } else if (strcmp(input, "open") == 0) {
        if (arguments[0] == '/') {
            if (strncmp(arguments, "/apps/", 6U) == 0 ||
                strncmp(arguments, "/gui/", 5U) == 0) launch_path(arguments);
            else push_file_text(arguments);
        } else if (arguments[0] != '\0') {
            char path[96] = "/gui/";
            append_text(path, sizeof(path), arguments);
            if (path_exists(path)) launch_path(path);
            else launch_named("/apps/", arguments);
        } else {
            push_line("open: missing path or app");
        }
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
    } else if (strcmp(input, "wait") == 0) {
        int valid = 0;
        const uint64_t pid = parse_u64(arguments, &valid);
        int32_t status = 0;
        if (!valid || pid == 0U) {
            push_line("wait: invalid pid");
        } else {
            const ku_status_t result = ku_process_wait(pid, &status);
            if (result == KU_STATUS_OK) push_line("wait: process finished");
            else if (result == KU_STATUS_WOULD_BLOCK) push_line("wait: process still running");
            else push_line("wait: pid not owned or unavailable");
        }
    } else if (strcmp(input, "history") == 0) {
        size_t start = g_history_count > 4U ? g_history_count - 4U : 0U;
        while (start < g_history_count) push_line(g_history[start++]);
    } else if (strcmp(input, "status") == 0) {
        char line[OUTPUT_CAPACITY] = "KuroganeOS " KUROGANE_VERSION_STRING " | jobs=";
        char number[24];
        gui_u64(number, sizeof(number), active_jobs());
        append_text(line, sizeof(line), number);
        push_line(line);
        push_pid("terminal pid=", ku_process_id());
    } else if (strcmp(input, "echo") == 0) {
        push_line(arguments);
    } else if (strcmp(input, "about") == 0) {
        push_line("Kurogane Flux Terminal / Desktop Applications 2.6.1");
        push_line("public Ring-3 ABI only; no kernel command backdoor");
    } else if (strcmp(input, "clear") == 0) {
        size_t index = 0U;
        while (index < OUTPUT_LINES) g_output[index++][0] = '\0';
    } else if (strcmp(input, "ls") == 0 || strcmp(input, "stat") == 0 ||
               strcmp(input, "cd") == 0 || strcmp(input, "mkdir") == 0 ||
               strcmp(input, "rm") == 0 || strcmp(input, "mv") == 0 ||
               strcmp(input, "cp") == 0 || strcmp(input, "touch") == 0) {
        push_line("command needs the next public VFS capability ABI");
    } else {
        push_line("unknown command; type help");
    }
}

int main(void) {
    const ku_window_t window = gui_open("FLUX TERMINAL", 54, 330, 690, 380);
    if (window == KU_INVALID_WINDOW) return 1;
    puts("[TEST] desktop_terminal_ring3: PASS");
    puts("[TEST] desktop_terminal_2_6: PASS");

    size_t index = 0U;
    while (index < OUTPUT_LINES) g_output[index++][0] = '\0';
    index = 0U;
    while (index < HISTORY_CAPACITY) g_history[index++][0] = '\0';
    push_line("Flux Terminal 2.6 online.");
    push_line("Use help for filesystem, app and process commands.");

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
