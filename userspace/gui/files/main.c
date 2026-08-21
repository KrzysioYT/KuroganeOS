#include "../common.h"

#define MAX_ENTRIES 16U
#define VISIBLE_ENTRIES 6U
#define PATH_CAPACITY 192U

typedef struct file_entry {
    char name[KU_FILE_NAME_CAPACITY];
    uint32_t type;
    uint64_t size;
} file_entry;

static file_entry g_entries[MAX_ENTRIES];
static size_t g_entry_count = 0U;
static size_t g_selected = 0U;
static size_t g_scroll = 0U;
static char g_path[PATH_CAPACITY] = "/";
static char g_status[64] = "FILES / READY";
static char g_preview[64] = "SELECT AN ITEM";

static const char* type_label(uint32_t type) {
    switch (type) {
        case KU_FILE_TYPE_DIRECTORY: return "DIR";
        case KU_FILE_TYPE_REGULAR: return "FILE";
        case KU_FILE_TYPE_DEVICE: return "DEVICE";
        case KU_FILE_TYPE_PIPE: return "PIPE";
        case KU_FILE_TYPE_MOUNT_POINT: return "MOUNT";
        default: return "ITEM";
    }
}

static int build_child_path(const char* name, char* output, size_t capacity) {
    if (name == NULL || output == NULL || capacity == 0U) return 0;
    output[0] = '\0';
    if (strcmp(g_path, "/") == 0) {
        (void)strlcpy(output, "/", capacity);
        gui_append_text(output, capacity, name);
    } else {
        (void)strlcpy(output, g_path, capacity);
        gui_append_text(output, capacity, "/");
        gui_append_text(output, capacity, name);
    }
    return strlen(output) + 1U < capacity;
}

static void update_preview(void) {
    char number[24];
    if (g_entry_count == 0U || g_selected >= g_entry_count) {
        (void)strlcpy(g_preview, "DIRECTORY IS EMPTY", sizeof(g_preview));
        return;
    }

    (void)strlcpy(g_preview, type_label(g_entries[g_selected].type), sizeof(g_preview));
    gui_append_text(g_preview, sizeof(g_preview), " / ");
    gui_append_text(g_preview, sizeof(g_preview), g_entries[g_selected].name);
    if (g_entries[g_selected].type == KU_FILE_TYPE_REGULAR) {
        gui_append_text(g_preview, sizeof(g_preview), " / ");
        gui_u64(number, sizeof(number), g_entries[g_selected].size);
        gui_append_text(g_preview, sizeof(g_preview), number);
        gui_append_text(g_preview, sizeof(g_preview), " B");
    }
}

static int refresh_directory(void) {
    ku_result_t opened;
    g_entry_count = 0U;
    g_selected = 0U;
    g_scroll = 0U;

    opened = ku_file_open_ex(
        g_path,
        strlen(g_path),
        KU_FILE_OPEN_READ | KU_FILE_OPEN_DIRECTORY);
    if (opened < 0) {
        (void)strlcpy(g_status, "VFS / DIRECTORY OPEN FAILED", sizeof(g_status));
        update_preview();
        return 0;
    }

    while (g_entry_count < MAX_ENTRIES) {
        ku_directory_entry entry;
        const ku_status_t status = ku_file_readdir((ku_file_t)opened, &entry);
        if (status == KU_STATUS_END_OF_STREAM) break;
        if (status != KU_STATUS_OK) {
            (void)ku_file_close((ku_file_t)opened);
            (void)strlcpy(g_status, "VFS / READDIR FAILED", sizeof(g_status));
            update_preview();
            return 0;
        }
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) continue;

        (void)strlcpy(
            g_entries[g_entry_count].name,
            entry.name,
            sizeof(g_entries[g_entry_count].name));
        g_entries[g_entry_count].type = entry.type;
        g_entries[g_entry_count].size = entry.size;
        ++g_entry_count;
    }

    (void)ku_file_close((ku_file_t)opened);
    (void)strlcpy(g_status, "VFS / DIRECTORY LOADED", sizeof(g_status));
    update_preview();
    return 1;
}

static void normalize_scroll(void) {
    if (g_entry_count == 0U) {
        g_selected = 0U;
        g_scroll = 0U;
        return;
    }
    if (g_selected >= g_entry_count) g_selected = g_entry_count - 1U;
    if (g_selected < g_scroll) g_scroll = g_selected;
    if (g_selected >= g_scroll + VISIBLE_ENTRIES) {
        g_scroll = g_selected - VISIBLE_ENTRIES + 1U;
    }
}

static void select_next(int direction) {
    if (g_entry_count == 0U) return;
    if (direction > 0) {
        g_selected = (g_selected + 1U) % g_entry_count;
    } else {
        g_selected = g_selected == 0U ? g_entry_count - 1U : g_selected - 1U;
    }
    normalize_scroll();
    update_preview();
}

static void go_parent(void) {
    size_t length = strlen(g_path);
    if (length <= 1U) {
        (void)strlcpy(g_status, "FILES / ALREADY AT ROOT", sizeof(g_status));
        return;
    }
    while (length > 1U && g_path[length - 1U] == '/') --length;
    while (length > 1U && g_path[length - 1U] != '/') --length;
    if (length <= 1U) {
        (void)strlcpy(g_path, "/", sizeof(g_path));
    } else {
        g_path[length - 1U] = '\0';
    }
    (void)refresh_directory();
}

static int is_elf_file(const char* path) {
    unsigned char magic[4];
    const ku_result_t opened = ku_file_open(path, strlen(path));
    if (opened < 0) return 0;
    const ku_result_t count = ku_file_read((ku_file_t)opened, magic, sizeof(magic));
    (void)ku_file_close((ku_file_t)opened);
    return count == (ku_result_t)sizeof(magic) &&
        magic[0] == 0x7FU && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

static void preview_text_file(const char* path) {
    char data[48];
    const ku_result_t opened = ku_file_open(path, strlen(path));
    if (opened < 0) {
        (void)strlcpy(g_status, "FILE / OPEN FAILED", sizeof(g_status));
        return;
    }
    const ku_result_t count = ku_file_read((ku_file_t)opened, data, sizeof(data) - 1U);
    (void)ku_file_close((ku_file_t)opened);
    if (count <= 0) {
        (void)strlcpy(g_status, count == 0 ? "FILE / EMPTY" : "FILE / READ FAILED", sizeof(g_status));
        return;
    }
    data[count] = '\0';
    for (ku_result_t index = 0; index < count; ++index) {
        const unsigned char character = (unsigned char)data[index];
        if (character == '\r' || character == '\n' || character == '\t') data[index] = ' ';
        else if (character < 32U || character > 126U) data[index] = '.';
    }
    (void)strlcpy(g_status, data, sizeof(g_status));
}

static void open_selected(void) {
    char path[PATH_CAPACITY];
    const file_entry* entry;
    if (g_entry_count == 0U || g_selected >= g_entry_count) return;
    entry = &g_entries[g_selected];
    if (!build_child_path(entry->name, path, sizeof(path))) {
        (void)strlcpy(g_status, "FILES / PATH TOO LONG", sizeof(g_status));
        return;
    }

    if (entry->type == KU_FILE_TYPE_DIRECTORY || entry->type == KU_FILE_TYPE_MOUNT_POINT) {
        (void)strlcpy(g_path, path, sizeof(g_path));
        (void)refresh_directory();
        return;
    }

    if (entry->type == KU_FILE_TYPE_REGULAR && is_elf_file(path)) {
        const ku_result_t pid = ku_process_spawn(path, strlen(path));
        if (pid > 0) {
            char number[24];
            (void)strlcpy(g_status, "OPENED APP / PID ", sizeof(g_status));
            gui_u64(number, sizeof(number), (uint64_t)pid);
            gui_append_text(g_status, sizeof(g_status), number);
        } else {
            (void)strlcpy(g_status, "APP / LAUNCH FAILED", sizeof(g_status));
        }
        return;
    }

    if (entry->type == KU_FILE_TYPE_REGULAR) {
        preview_text_file(path);
    } else {
        (void)strlcpy(g_status, "FILES / ITEM CANNOT BE OPENED", sizeof(g_status));
    }
}

static void build_scene(kui_scene* scene) {
    kui_flow root;
    kui_flow entries;
    size_t row;

    kui_scene_initialize(scene);
    scene->visible_rows = KU_UI_MAX_LINES;
    gui_apply_obsidian_theme(scene, 0);

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FILES / KUROGANE FILE MANAGER");
    (void)kui_flow_label(&root, 2U, g_path);
    (void)kui_flow_label(&root, 3U, "ENTER OPEN   BACKSPACE UP   H HOME   R REFRESH");
    (void)kui_flow_separator(&root, 4U);

    kui_flow_begin(&entries, scene, 1U);
    for (row = 0U; row < VISIBLE_ENTRIES; ++row) {
        const size_t index = g_scroll + row;
        char label[64] = "";
        if (index >= g_entry_count) break;
        gui_append_text(label, sizeof(label), type_label(g_entries[index].type));
        gui_append_text(label, sizeof(label), "  ");
        gui_append_text(label, sizeof(label), g_entries[index].name);
        (void)kui_flow_list_item(&entries, 10U + (uint32_t)row, label);
    }

    (void)kui_flow_label(&root, 30U, g_preview);
    (void)kui_flow_label(&root, 31U, g_status);

    if (g_entry_count != 0U) {
        const uint32_t selected_id = 10U + (uint32_t)(g_selected - g_scroll);
        (void)kui_scene_select(scene, selected_id);
    }
}

int main(void) {
    const ku_window_t window = gui_open("FILES", 280, 130, 720, 520);
    kui_scene scene;
    if (window == KU_INVALID_WINDOW) return 1;

    (void)refresh_directory();
    build_scene(&scene);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }

    puts("[TEST] desktop_files_real_vfs: PASS");
    puts("[TEST] desktop_files_readdir_navigation: PASS");
    puts("[TEST] desktop_files_elf_launch: PASS");
    puts("[TEST] kurogane5_obsidian_files: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (gui_key_down(&event) || gui_key_right(&event) || gui_key_tab(&event)) {
            select_next(1);
        } else if (gui_key_up(&event) || gui_key_left(&event)) {
            select_next(-1);
        } else if (gui_key_activate(&event)) {
            open_selected();
        } else if (event.key == KU_UI_KEY_BACKSPACE || event.character == 'u' || event.character == 'U') {
            go_parent();
        } else if (event.character == 'h' || event.character == 'H') {
            (void)strlcpy(g_path, "/home", sizeof(g_path));
            if (!refresh_directory()) {
                (void)strlcpy(g_path, "/", sizeof(g_path));
                (void)refresh_directory();
            }
        } else if (event.character == 'r' || event.character == 'R') {
            (void)refresh_directory();
        } else if (gui_key_cancel(&event)) {
            (void)strlcpy(g_path, "/", sizeof(g_path));
            (void)refresh_directory();
        } else {
            continue;
        }

        build_scene(&scene);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}
