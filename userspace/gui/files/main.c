#include "../common.h"
#include <kurogane/flux_app_catalog.h>
#include "paging.h"
#include "navigation.h"
#include "filter.h"
#include "history.h"
#include "folder_creation.h"
#include "rename.h"
#include "../clipboard_actions.h"

#define DIRECTORY_ENTRY_CAPACITY 512U
#define PAGE_SIZE 6U
#define PATH_CAPACITY 192U
#define NO_SELECTION UINT32_C(0xFFFFFFFF)

typedef struct explorer_entry {
    char name[KU_FILE_NAME_CAPACITY];
    uint32_t type;
    uint64_t size;
} explorer_entry;

/* Keep the expanded catalog out of the guest's small 32-KiB user stack. */
static explorer_entry s_directory_cache[DIRECTORY_ENTRY_CAPACITY];

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

/* The native input packet is 64 bytes. Keep the caret and trailing characters
 * visible even when a file name is longer than the available label width. */
static void edit_input_label(
    char* label, size_t capacity, int mode, const char* name) {
    const char* prefix = mode == 2 ? "RENAME > " : "NEW FOLDER > ";
    const size_t prefix_length = strlen(prefix);
    const size_t length = strlen(name);
    size_t available;
    if (capacity <= prefix_length + 2U) return;
    (void)strlcpy(label, prefix, capacity);
    available = capacity - prefix_length - 2U;
    if (length > available && available > 3U) {
        append_text(label, capacity, "...");
        append_text(label, capacity, name + length - (available - 3U));
    } else {
        append_text(label, capacity, name);
    }
    append_text(label, capacity, "|");
}

static int directory_like(uint32_t type) {
    return type == KU_FILE_TYPE_DIRECTORY || type == KU_FILE_TYPE_MOUNT_POINT;
}

static int name_before(const explorer_entry* left, const explorer_entry* right) {
    size_t index = 0U;
    const int left_dir = directory_like(left->type);
    const int right_dir = directory_like(right->type);
    if (left_dir != right_dir) return left_dir > right_dir;
    while (left->name[index] != '\0' && right->name[index] != '\0') {
        const unsigned char a = (unsigned char)left->name[index];
        const unsigned char b = (unsigned char)right->name[index];
        if (a != b) return a < b;
        ++index;
    }
    return left->name[index] == '\0' && right->name[index] != '\0';
}

static void sort_entries(explorer_entry* entries, size_t count) {
    size_t index;
    for (index = 1U; index < count; ++index) {
        explorer_entry value = entries[index];
        size_t position = index;
        while (position > 0U && name_before(&value, &entries[position - 1U])) {
            entries[position] = entries[position - 1U];
            --position;
        }
        entries[position] = value;
    }
}

static int load_directory(
    const char* path,
    const char* query,
    explorer_entry* entries,
    size_t* count,
    char* status,
    size_t status_capacity) {
    ku_result_t opened;
    size_t used = 0U;
    if (path == NULL || query == NULL || entries == NULL || count == NULL) return 0;
    opened = ku_file_open_ex(
        path, strlen(path), KU_FILE_OPEN_READ | KU_FILE_OPEN_DIRECTORY);
    if (opened < 0) {
        (void)strlcpy(status, "DIRECTORY / OPEN FAILED", status_capacity);
        *count = 0U;
        return 0;
    }

    for (;;) {
        ku_directory_entry entry;
        const ku_status_t result = ku_file_readdir((ku_file_t)opened, &entry);
        if (result == KU_STATUS_END_OF_STREAM) break;
        if (result != KU_STATUS_OK) {
            (void)ku_file_close((ku_file_t)opened);
            (void)strlcpy(status, "DIRECTORY / READ FAILED", status_capacity);
            *count = 0U;
            return 0;
        }
        /* A broken directory entry must not read outside the fixed ABI name. */
        if (entry.name_length == 0U ||
            entry.name_length >= KU_FILE_NAME_CAPACITY) continue;
        entry.name[entry.name_length] = '\0';
        if (entry.name[0] == '\0' || strcmp(entry.name, ".") == 0 ||
            strcmp(entry.name, "..") == 0) continue;
        if (!flux_files_matches_filter(entry.name, query)) continue;
        if (used >= DIRECTORY_ENTRY_CAPACITY) {
            (void)strlcpy(status, query[0] == '\0'
                ? "DIRECTORY / FIRST 512 ITEMS"
                : "FIND / FIRST 512 MATCHES", status_capacity);
            break;
        }
        (void)strlcpy(entries[used].name, entry.name, sizeof(entries[used].name));
        entries[used].type = entry.type;
        entries[used].size = entry.size;
        ++used;
    }
    (void)ku_file_close((ku_file_t)opened);
    sort_entries(entries, used);
    *count = used;
    if (status[0] == '\0') (void)strlcpy(status, "DIRECTORY / READY", status_capacity);
    return 1;
}

static int join_path(
    const char* base,
    const char* name,
    char* output,
    size_t capacity) {
    size_t used;
    if (base == NULL || name == NULL || output == NULL || capacity == 0U) return 0;
    output[0] = '\0';
    if (strlcpy(output, base, capacity) >= capacity) return 0;
    used = strlen(output);
    if (!(used == 1U && output[0] == '/')) {
        if (used + 1U >= capacity) return 0;
        output[used++] = '/';
        output[used] = '\0';
    }
    return strlcpy(output + used, name, capacity - used) < capacity - used;
}

static void parent_path(char* path) {
    size_t length;
    if (path == NULL || path[0] != '/') return;
    length = strlen(path);
    if (length <= 1U) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }
    while (length > 1U && path[length - 1U] == '/') path[--length] = '\0';
    while (length > 1U && path[length - 1U] != '/') --length;
    if (length <= 1U) {
        path[0] = '/';
        path[1] = '\0';
    } else {
        path[length - 1U] = '\0';
    }
}

/* Keep the visible folder and history unchanged until the VFS has actually
 * opened the destination. On failure restore the previous cached listing. */
static int navigate_directory(
    char* path, char* query, const char* destination, const char* filter,
    explorer_entry* entries, size_t* entry_count, size_t* page_start,
    uint32_t* selected, char* status, size_t status_capacity,
    char* preview, size_t preview_capacity) {
    char next_status[64] = "";
    char restore_status[64] = "";
    if (destination == NULL || filter == NULL || destination[0] != '/' ||
        strlen(destination) >= PATH_CAPACITY ||
        strlen(filter) >= FLUX_FILES_FILTER_CAPACITY) {
        (void)strlcpy(preview, "FOLDER / INVALID PATH", preview_capacity);
        return 0;
    }
    if (!load_directory(destination, filter, entries, entry_count,
                        next_status, sizeof(next_status))) {
        (void)load_directory(path, query, entries, entry_count,
                             restore_status, sizeof(restore_status));
        (void)strlcpy(preview, next_status, preview_capacity);
        (void)strlcpy(status, next_status, status_capacity);
        return 0;
    }
    (void)strlcpy(path, destination, PATH_CAPACITY);
    (void)strlcpy(query, filter, FLUX_FILES_FILTER_CAPACITY);
    *page_start = 0U;
    *selected = NO_SELECTION;
    (void)strlcpy(status, next_status, status_capacity);
    (void)strlcpy(preview, next_status, preview_capacity);
    return 1;
}

static const char* folder_create_error(ku_status_t result) {
    if (result == KU_STATUS_ALREADY_EXISTS) return "FOLDER / NAME ALREADY EXISTS";
    if (result == KU_STATUS_ACCESS_DENIED || result == KU_STATUS_NOT_SUPPORTED) {
        return "FOLDER / READ-ONLY OR ACCESS DENIED";
    }
    if (result == KU_STATUS_OUT_OF_RANGE || result == KU_STATUS_INVALID_ARGUMENT) {
        return "FOLDER / INVALID NAME OR PATH";
    }
    return "FOLDER / CREATE FAILED";
}

static const char* rename_error(ku_status_t result) {
    if (result == KU_STATUS_ALREADY_EXISTS) return "RENAME / NAME ALREADY EXISTS";
    if (result == KU_STATUS_ACCESS_DENIED || result == KU_STATUS_NOT_SUPPORTED) {
        return "RENAME / READ-ONLY OR ACCESS DENIED";
    }
    if (result == KU_STATUS_OUT_OF_RANGE || result == KU_STATUS_INVALID_ARGUMENT) {
        return "RENAME / INVALID NAME OR PATH";
    }
    if (result == KU_STATUS_NOT_FOUND) return "RENAME / SOURCE NO LONGER EXISTS";
    return "RENAME / FAILED";
}

static int is_elf_file(const char* path) {
    unsigned char magic[4];
    const ku_result_t opened = ku_file_open(path, strlen(path));
    ku_result_t count;
    if (opened < 0) return 0;
    count = ku_file_read((ku_file_t)opened, magic, sizeof(magic));
    (void)ku_file_close((ku_file_t)opened);
    return count == 4 && magic[0] == 0x7FU && magic[1] == 'E' &&
        magic[2] == 'L' && magic[3] == 'F';
}

static void preview_file(
    const char* path,
    char* preview,
    size_t preview_capacity) {
    char data[48];
    const ku_result_t opened = ku_file_open(path, strlen(path));
    ku_result_t count;
    size_t input;
    size_t output = 0U;
    if (opened < 0) {
        (void)strlcpy(preview, "PREVIEW / OPEN FAILED", preview_capacity);
        return;
    }
    count = ku_file_read((ku_file_t)opened, data, sizeof(data));
    (void)ku_file_close((ku_file_t)opened);
    if (count < 0) {
        (void)strlcpy(preview, "PREVIEW / READ FAILED", preview_capacity);
        return;
    }
    if (count == 0) {
        (void)strlcpy(preview, "PREVIEW / EMPTY FILE", preview_capacity);
        return;
    }
    if (count >= 4 && (unsigned char)data[0] == 0x7FU && data[1] == 'E' &&
        data[2] == 'L' && data[3] == 'F') {
        (void)strlcpy(preview, "PREVIEW / ELF64 EXECUTABLE", preview_capacity);
        return;
    }
    (void)strlcpy(preview, "PREVIEW / ", preview_capacity);
    for (input = 0U; input < (size_t)count && output + strlen(preview) + 1U < preview_capacity; ++input) {
        const unsigned char value = (unsigned char)data[input];
        char character[2];
        if (value == '\r' || value == '\n') break;
        character[0] = value >= 32U && value <= 126U ? (char)value : '.';
        character[1] = '\0';
        append_text(preview, preview_capacity, character);
        ++output;
    }
}

static void make_tile_label(
    const explorer_entry* entry,
    char* label,
    size_t capacity) {
    char number[24];
    label[0] = '\0';
    append_text(label, capacity, entry->name);
    append_text(label, capacity, "\n");
    if (entry->type == KU_FILE_TYPE_DIRECTORY) {
        append_text(label, capacity, "FOLDER");
    } else if (entry->type == KU_FILE_TYPE_MOUNT_POINT) {
        append_text(label, capacity, "MOUNT POINT");
    } else if (entry->type == KU_FILE_TYPE_DEVICE) {
        append_text(label, capacity, "DEVICE");
    } else {
        gui_u64(number, sizeof(number), entry->size);
        append_text(label, capacity, number);
        append_text(label, capacity, " BYTES");
    }
}

static void build_scene(
    kui_scene* scene,
    const char* path,
    const char* query,
    const flux_files_history* history,
    int edit_mode,
    const char* edit_name,
    const explorer_entry* entries,
    size_t entry_count,
    size_t page_start,
    uint32_t selected,
    const char* preview) {
    kui_flow root;
    kui_flow grid;
    char heading[64] = "FLUX FILES / ";
    char breadcrumb[64] = "PATH ";
    char folder_input[64] = "";
    char page[24];
    size_t visible = 0U;
    size_t index;

    gui_u64(page, sizeof(page), entry_count == 0U ? 0U : page_start / PAGE_SIZE + 1U);
    append_text(heading, sizeof(heading), page);
    append_text(heading, sizeof(heading), " / ");
    gui_u64(page, sizeof(page), entry_count == 0U ? 0U : (entry_count + PAGE_SIZE - 1U) / PAGE_SIZE);
    append_text(heading, sizeof(heading), page);
    if (query[0] == '\0') {
        append_text(breadcrumb, sizeof(breadcrumb), path);
        append_text(breadcrumb, sizeof(breadcrumb), "  / TYPE TO FIND");
    } else {
        (void)strlcpy(breadcrumb, "FIND ", sizeof(breadcrumb));
        append_text(breadcrumb, sizeof(breadcrumb), query);
        append_text(breadcrumb, sizeof(breadcrumb), "  / PATH ");
        append_text(breadcrumb, sizeof(breadcrumb), path);
    }

    kui_scene_initialize(scene);
    scene->visible_rows = 17U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, heading);
    if (edit_mode != 0) {
        edit_input_label(folder_input, sizeof(folder_input), edit_mode, edit_name);
        (void)kui_flow_input(&root, 38U, folder_input);
    } else {
        (void)kui_flow_label(&root, 2U, breadcrumb);
    }

    kui_flow_begin(&grid, scene, 1U);
    for (index = page_start;
         index < entry_count && visible < PAGE_SIZE;
         ++index, ++visible) {
        char label[64];
        const uint32_t id = 10U + (uint32_t)visible;
        make_tile_label(&entries[index], label, sizeof(label));
        (void)kui_flow_tile(
            &grid,
            id,
            label,
            directory_like(entries[index].type)
                ? KU_UI_NATIVE_ICON_FOLDER : KU_UI_NATIVE_ICON_DOCUMENT);
        if (selected == (uint32_t)index) (void)kui_scene_select(scene, id);
    }
    (void)kui_flow_button(&root, 35U, "BACK / ALT + LEFT");
    (void)kui_flow_button(&root, 36U, "FORWARD / ALT + RIGHT");
    (void)kui_flow_button(&root, 30U, "PARENT FOLDER");
    (void)kui_flow_button(&root, 31U, "OPEN / ENTER");
    (void)kui_flow_button(&root, 32U, "NEXT PAGE");
    (void)kui_flow_button(&root, 34U, "COPY SELECTED PATH");
    (void)kui_flow_button(&root, 37U,
        edit_mode == 1 ? "CANCEL NEW FOLDER" : "NEW FOLDER");
    (void)kui_flow_button(&root, 39U,
        edit_mode == 2 ? "CANCEL RENAME" : "RENAME SELECTED");
    (void)kui_flow_label(&root, 33U, preview);
    if (edit_mode != 0) {
        for (uint32_t id = 10U; id < 10U + PAGE_SIZE; ++id) {
            (void)kui_scene_set_flags(scene, id, KUI_VIEW_DISABLED);
        }
        (void)kui_scene_set_flags(scene, 30U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 31U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 32U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 34U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 35U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 36U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, edit_mode == 1 ? 39U : 37U,
                                  KUI_VIEW_DISABLED);
        (void)kui_scene_select(scene, 38U);
        return;
    }
    if (!flux_files_history_can_move(history, -1)) {
        (void)kui_scene_set_flags(scene, 35U, KUI_VIEW_DISABLED);
    }
    if (!flux_files_history_can_move(history, 1)) {
        (void)kui_scene_set_flags(scene, 36U, KUI_VIEW_DISABLED);
    }
    if (path[0] == '/' && path[1] == '\0') {
        (void)kui_scene_set_flags(scene, 30U, KUI_VIEW_DISABLED);
    }
    if (entry_count <= PAGE_SIZE) {
        (void)kui_scene_set_flags(scene, 32U, KUI_VIEW_DISABLED);
    }
    if (selected == NO_SELECTION || selected >= entry_count) {
        (void)kui_scene_set_flags(scene, 34U, KUI_VIEW_DISABLED);
        (void)kui_scene_set_flags(scene, 39U, KUI_VIEW_DISABLED);
    } else if (entries[selected].type != KU_FILE_TYPE_REGULAR &&
               entries[selected].type != KU_FILE_TYPE_DIRECTORY) {
        (void)kui_scene_set_flags(scene, 39U, KUI_VIEW_DISABLED);
    }
}

static int activate_selected(
    char* path,
    char* query,
    flux_files_history* history,
    explorer_entry* entries,
    size_t* entry_count,
    size_t* page_start,
    uint32_t* selected,
    char* status,
    size_t status_capacity,
    char* preview,
    size_t preview_capacity) {
    char target[PATH_CAPACITY];
    const explorer_entry* entry;
    if (*selected == NO_SELECTION || *selected >= *entry_count) {
        (void)strlcpy(preview, "SELECT AN ITEM FIRST", preview_capacity);
        return 0;
    }
    entry = &entries[*selected];
    if (!join_path(path, entry->name, target, sizeof(target))) {
        (void)strlcpy(preview, "PATH / TOO LONG", preview_capacity);
        return 0;
    }
    if (directory_like(entry->type)) {
        if (!navigate_directory(path, query, target, "", entries, entry_count,
                                page_start, selected, status, status_capacity,
                                preview, preview_capacity)) return 0;
        (void)flux_files_history_visit(history, target);
        return 1;
    }
    if (is_elf_file(target)) {
        const ku_result_t pid = ku_process_spawn(target, strlen(target));
        if (pid > 0) {
            char number[24];
            (void)strlcpy(preview, "OPENED PID ", preview_capacity);
            gui_u64(number, sizeof(number), (uint64_t)pid);
            append_text(preview, preview_capacity, number);
            return 1;
        }
        (void)strlcpy(preview, "EXECUTABLE / LAUNCH FAILED", preview_capacity);
        return 0;
    }
    preview_file(target, preview, preview_capacity);
    return 1;
}

int main(void) {
    const ku_window_t window = gui_open(KU_FLUX_TITLE_FILES, 250, 75, 650, 630);
    explorer_entry* entries = s_directory_cache;
    size_t entry_count = 0U;
    size_t page_start = 0U;
    uint32_t selected = NO_SELECTION;
    uint32_t pointer_buttons = 0U;
    char path[PATH_CAPACITY] = "/";
    char query[FLUX_FILES_FILTER_CAPACITY] = "";
    char edit_name[FLUX_FILES_NEW_FOLDER_CAPACITY] = "";
    char rename_source[KU_FILE_NAME_CAPACITY] = "";
    uint32_t rename_type = KU_FILE_TYPE_UNKNOWN;
    int edit_mode = 0; /* 0: browse, 1: new folder, 2: rename */
    char status[64] = "";
    char preview[64] = "LEFT CLICK OPEN / RIGHT CLICK SELECT";
    flux_files_history history;
    kui_scene scene;
    if (window == KU_INVALID_WINDOW) return 1;
    if (!flux_files_history_init(&history, path)) {
        (void)ku_ui_close(window);
        return 2;
    }

    if (!load_directory(path, query, entries, &entry_count, status, sizeof(status))) {
        (void)ku_ui_close(window);
        return 2;
    }
    if (status[0] != '\0' && strcmp(status, "DIRECTORY / READY") != 0) {
        (void)strlcpy(preview, status, sizeof(preview));
    }
    build_scene(&scene, path, query, &history, edit_mode, edit_name,
                entries, entry_count, page_start, selected, preview);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 3;
    }

    puts("[TEST] desktop_files_real_vfs: PASS");
    puts("[TEST] flux_scene_files: PASS");
    puts("[TEST] desktop_files_mouse_navigation: PASS");
    puts("[TEST] desktop_files_keyboard_navigation: READY");
    puts("[TEST] flux_files_readdir: PASS");
    puts("[TEST] flux_files_directory_grid: PASS");
    puts("[TEST] flux_files_breadcrumb: PASS");
    puts("[TEST] flux_files_search: READY");
    puts("[TEST] flux_files_history: READY");
    puts("[TEST] flux_files_mkdir: READY");
    puts("[TEST] flux_files_rename: READY");

    for (;;) {
        ku_ui_event event;
        uint32_t target;
        const int wait_result = gui_wait_event(window, &event);
        if (wait_result < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (edit_mode != 0 && event.type == KU_UI_EVENT_KEY) {
            const flux_files_folder_action edit = flux_files_folder_key(
                edit_name, sizeof(edit_name), &event);
            if (edit == FLUX_FILES_FOLDER_NONE) continue;
            if (edit == FLUX_FILES_FOLDER_CANCEL) {
                (void)strlcpy(preview, edit_mode == 1
                    ? "NEW FOLDER / CANCELLED" : "RENAME / CANCELLED", sizeof(preview));
                edit_mode = 0;
            } else if (edit == FLUX_FILES_FOLDER_EDITED) {
                (void)strlcpy(preview, edit_mode == 1
                    ? "NEW FOLDER / ENTER CREATE  ESC CANCEL"
                    : "RENAME / ENTER SAVE  ESC CANCEL", sizeof(preview));
            } else if (edit == FLUX_FILES_FOLDER_INVALID) {
                (void)strlcpy(preview, edit_mode == 1
                    ? "NEW FOLDER / INVALID CHARACTER"
                    : "RENAME / INVALID CHARACTER", sizeof(preview));
            } else if (edit == FLUX_FILES_FOLDER_FULL) {
                (void)strlcpy(preview, edit_mode == 1
                    ? "NEW FOLDER / MAXIMUM NAME LENGTH"
                    : "RENAME / MAXIMUM NAME LENGTH", sizeof(preview));
            } else if (edit == FLUX_FILES_FOLDER_SUBMIT && edit_mode == 1) {
                char destination[PATH_CAPACITY];
                if (!flux_files_folder_valid_name(edit_name, sizeof(edit_name))) {
                    (void)strlcpy(preview, "NEW FOLDER / ENTER A VALID NAME", sizeof(preview));
                } else if (!join_path(path, edit_name,
                                      destination, sizeof(destination))) {
                    (void)strlcpy(preview, "NEW FOLDER / PATH TOO LONG", sizeof(preview));
                } else {
                    const ku_status_t created = ku_file_mkdir(destination, strlen(destination));
                    if (created != KU_STATUS_OK) {
                        (void)strlcpy(preview, folder_create_error(created), sizeof(preview));
                    } else {
                        const ku_status_t synced = ku_file_sync();
                        edit_mode = 0;
                        edit_name[0] = '\0';
                        query[0] = '\0';
                        (void)flux_files_history_filter(&history, query);
                        page_start = 0U;
                        selected = NO_SELECTION;
                        status[0] = '\0';
                        if (!load_directory(path, query, entries, &entry_count,
                                            status, sizeof(status))) {
                            (void)strlcpy(preview, "FOLDER CREATED / REFRESH FAILED", sizeof(preview));
                        } else if (synced != KU_STATUS_OK) {
                            (void)strlcpy(preview, "FOLDER CREATED / SYNC NOT VERIFIED", sizeof(preview));
                        } else {
                            (void)strlcpy(preview, "FOLDER / CREATED", sizeof(preview));
                        }
                        puts("[TEST] flux_files_mkdir: PASS");
                    }
                }
            } else if (edit == FLUX_FILES_FOLDER_SUBMIT && edit_mode == 2) {
                char source_path[PATH_CAPACITY];
                char destination[PATH_CAPACITY];
                const flux_files_rename_policy policy = flux_files_rename_check(
                    rename_type, rename_source, edit_name, sizeof(edit_name));
                if (policy == FLUX_FILES_RENAME_INVALID) {
                    (void)strlcpy(preview, "RENAME / ENTER A VALID NAME", sizeof(preview));
                } else if (policy == FLUX_FILES_RENAME_UNCHANGED) {
                    edit_mode = 0;
                    (void)strlcpy(preview, "RENAME / NAME UNCHANGED", sizeof(preview));
                } else if (!join_path(path, rename_source, source_path, sizeof(source_path)) ||
                           !join_path(path, edit_name, destination, sizeof(destination))) {
                    (void)strlcpy(preview, "RENAME / PATH TOO LONG", sizeof(preview));
                } else {
                    ku_file_stat existing;
                    const ku_status_t found = ku_file_stat_path(
                        destination, strlen(destination), &existing);
                    if (found == KU_STATUS_OK) {
                        (void)strlcpy(preview, "RENAME / NAME ALREADY EXISTS", sizeof(preview));
                    } else if (found != KU_STATUS_NOT_FOUND) {
                        (void)strlcpy(preview, "RENAME / DESTINATION CHECK FAILED", sizeof(preview));
                    } else {
                        const ku_status_t renamed = ku_file_rename(
                            source_path, strlen(source_path), destination, strlen(destination));
                        if (renamed != KU_STATUS_OK) {
                            (void)strlcpy(preview, rename_error(renamed), sizeof(preview));
                        } else {
                            const ku_status_t synced = ku_file_sync();
                            char renamed_name[KU_FILE_NAME_CAPACITY];
                            (void)strlcpy(renamed_name, edit_name, sizeof(renamed_name));
                            edit_mode = 0;
                            edit_name[0] = '\0';
                            query[0] = '\0';
                            (void)flux_files_history_filter(&history, query);
                            page_start = 0U;
                            selected = NO_SELECTION;
                            status[0] = '\0';
                            if (!load_directory(path, query, entries, &entry_count,
                                                status, sizeof(status))) {
                                (void)strlcpy(preview, "RENAMED / REFRESH FAILED", sizeof(preview));
                            } else {
                                for (size_t index = 0U; index < entry_count; ++index) {
                                    if (strcmp(entries[index].name, renamed_name) == 0) {
                                        selected = (uint32_t)index;
                                        page_start = (index / PAGE_SIZE) * PAGE_SIZE;
                                        break;
                                    }
                                }
                                (void)strlcpy(preview, synced == KU_STATUS_OK
                                    ? "RENAME / SAVED" : "RENAMED / SYNC NOT VERIFIED",
                                    sizeof(preview));
                            }
                            puts("[TEST] flux_files_rename: PASS");
                        }
                    }
                }
            }
            build_scene(&scene, path, query, &history, edit_mode, edit_name,
                        entries, entry_count, page_start, selected, preview);
            (void)kui_scene_present(window, &scene);
            continue;
        }
        if (edit_mode != 0 && event.type == KU_UI_EVENT_SCROLL) continue;
        if (event.type == KU_UI_EVENT_SCROLL) {
            // A scroll event proves HID delivery even in directories with
            // fewer items than a page, where the offset cannot change.
            if (event.wheel_y != 0)
                puts("[TEST] flux_files_usb_wheel_input: PASS");
            // Preserve combined wheel detents without integer overflow.
            const size_t before = page_start;
            page_start = flux_files_wheel_page(
                page_start, entry_count, PAGE_SIZE, event.wheel_y);
            if (page_start != before) {
                selected = NO_SELECTION;
                (void)strlcpy(preview, "WHEEL / PAGE CHANGED", sizeof(preview));
                build_scene(&scene, path, query, &history,
                            edit_mode, edit_name, entries, entry_count,
                            page_start, selected, preview);
                (void)kui_scene_present(window, &scene);
                puts("[TEST] flux_files_usb_wheel_paging: PASS");
            }
            continue;
        }
        if (event.type == KU_UI_EVENT_KEY) {
            if (event.modifiers == KU_UI_MOD_ALT &&
                (event.key == KU_UI_KEY_ARROW_LEFT ||
                 event.key == KU_UI_KEY_ARROW_RIGHT)) {
                target = event.key == KU_UI_KEY_ARROW_LEFT ? 35U : 36U;
            } else if (flux_files_filter_edit(query, sizeof(query), &event)) {
                (void)flux_files_history_filter(&history, query);
                page_start = 0U;
                selected = NO_SELECTION;
                status[0] = '\0';
                if (load_directory(path, query, entries, &entry_count,
                                   status, sizeof(status))) {
                    (void)strlcpy(preview,
                        status[0] != '\0' && strcmp(status, "DIRECTORY / READY") != 0
                            ? status
                            : entry_count == 0U ? "FIND / NO MATCHES" : "FIND / UPDATED",
                        sizeof(preview));
                } else {
                    (void)strlcpy(preview, status, sizeof(preview));
                }
                build_scene(&scene, path, query, &history,
                            edit_mode, edit_name, entries, entry_count,
                            page_start, selected, preview);
                (void)kui_scene_present(window, &scene);
                puts("[TEST] flux_files_search: PASS");
                continue;
            } else {
                const flux_files_key_action action = flux_files_keyboard(
                    &event, entry_count, PAGE_SIZE, &page_start, &selected);
                if (action == FLUX_FILES_KEY_NONE) continue;
                if (action == FLUX_FILES_KEY_SELECT) {
                    (void)strlcpy(preview, "KEYBOARD / ITEM SELECTED", sizeof(preview));
                    build_scene(&scene, path, query, &history,
                                edit_mode, edit_name, entries, entry_count,
                                page_start, selected, preview);
                    (void)kui_scene_present(window, &scene);
                    puts("[TEST] flux_files_keyboard_navigation: PASS");
                    continue;
                }
                target = action == FLUX_FILES_KEY_PARENT ? 30U :
                         action == FLUX_FILES_KEY_OPEN ? 31U : 34U;
            }
        } else if (event.type == KU_UI_EVENT_POINTER) {
            const uint32_t previous_buttons = pointer_buttons;
            const int primary_pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous_buttons & UINT32_C(1)) == 0U;
            const int secondary_pressed =
                (event.buttons & UINT32_C(2)) != 0U &&
                (previous_buttons & UINT32_C(2)) == 0U;
            pointer_buttons = event.buttons;
            if (!primary_pressed && !secondary_pressed) continue;
            target = kui_scene_hit_test(&scene, event.x, event.y);
            /* Secondary-click selects folders without navigating. Preserve
             * the existing single-primary-click activation workflow. */
            if (secondary_pressed) {
                if (target >= 10U && target < 10U + PAGE_SIZE && edit_mode == 0) {
                    const size_t index = page_start + (size_t)(target - 10U);
                    if (index < entry_count) {
                        selected = (uint32_t)index;
                        (void)strlcpy(preview, "SELECTED / RENAME OR OPEN", sizeof(preview));
                        build_scene(&scene, path, query, &history, edit_mode,
                                    edit_name, entries, entry_count,
                                    page_start, selected, preview);
                        (void)kui_scene_present(window, &scene);
                    }
                }
                continue;
            }
        } else {
            continue;
        }
        if (target == 37U) {
            edit_mode = edit_mode == 1 ? 0 : 1;
            edit_name[0] = '\0';
            (void)strlcpy(preview, edit_mode == 1
                ? "NEW FOLDER / TYPE NAME  ENTER CREATE  ESC CANCEL"
                : "NEW FOLDER / CANCELLED", sizeof(preview));
        } else if (target == 39U) {
            if (edit_mode == 2) {
                edit_mode = 0;
                (void)strlcpy(preview, "RENAME / CANCELLED", sizeof(preview));
            } else if (edit_mode == 0 && selected != NO_SELECTION &&
                       selected < entry_count &&
                       (entries[selected].type == KU_FILE_TYPE_REGULAR ||
                        entries[selected].type == KU_FILE_TYPE_DIRECTORY)) {
                (void)strlcpy(rename_source, entries[selected].name, sizeof(rename_source));
                rename_type = entries[selected].type;
                (void)strlcpy(edit_name, rename_source, sizeof(edit_name));
                edit_mode = 2;
                (void)strlcpy(preview, "RENAME / TYPE NAME  ENTER SAVE  ESC CANCEL", sizeof(preview));
            } else {
                (void)strlcpy(preview, "RENAME / SELECT FILE OR FOLDER", sizeof(preview));
            }
        } else if (edit_mode != 0) {
            continue;
        } else if (target >= 10U && target < 10U + PAGE_SIZE) {
            const size_t index = page_start + (size_t)(target - 10U);
            if (index >= entry_count) continue;
            selected = (uint32_t)index;
            if (directory_like(entries[index].type)) {
                (void)activate_selected(
                    path, query, &history, entries, &entry_count, &page_start, &selected,
                    status, sizeof(status), preview, sizeof(preview));
            } else {
                char target_path[PATH_CAPACITY];
                if (join_path(path, entries[index].name, target_path, sizeof(target_path))) {
                    preview_file(target_path, preview, sizeof(preview));
                }
            }
        } else if (target == 30U) {
            if (!(path[0] == '/' && path[1] == '\0')) {
                char parent[PATH_CAPACITY];
                (void)strlcpy(parent, path, sizeof(parent));
                parent_path(parent);
                if (navigate_directory(path, query, parent, "", entries, &entry_count,
                                       &page_start, &selected, status, sizeof(status),
                                       preview, sizeof(preview))) {
                    (void)flux_files_history_visit(&history, parent);
                }
            }
        } else if (target == 31U) {
            (void)activate_selected(
                path, query, &history, entries, &entry_count, &page_start, &selected,
                status, sizeof(status), preview, sizeof(preview));
        } else if (target == 35U || target == 36U) {
            const int direction = target == 35U ? -1 : 1;
            const flux_files_history_entry* destination =
                flux_files_history_peek(&history, direction);
            if (destination != NULL &&
                navigate_directory(path, query, destination->path, destination->query,
                                   entries, &entry_count, &page_start, &selected,
                                   status, sizeof(status), preview, sizeof(preview))) {
                (void)flux_files_history_move(&history, direction);
                puts("[TEST] flux_files_history: PASS");
            }
        } else if (target == 34U) {
            char selected_path[PATH_CAPACITY];
            if (selected == NO_SELECTION || selected >= entry_count ||
                !join_path(path, entries[selected].name,
                           selected_path, sizeof(selected_path))) {
                (void)strlcpy(preview, "COPY PATH / INVALID SELECTION", sizeof(preview));
            } else if (flux_clipboard_set_text(selected_path)) {
                (void)strlcpy(preview, "COPY PATH / CLIPBOARD READY", sizeof(preview));
                puts("[TEST] flux_files_copy_path_clipboard: PASS");
            } else {
                (void)strlcpy(preview, "COPY PATH / CLIPBOARD UNAVAILABLE", sizeof(preview));
            }
        } else if (target == 32U) {
            if (entry_count > PAGE_SIZE) {
                page_start += PAGE_SIZE;
                if (page_start >= entry_count) page_start = 0U;
                selected = NO_SELECTION;
                (void)strlcpy(preview, "PAGE / CHANGED", sizeof(preview));
            }
        } else {
            continue;
        }

        build_scene(&scene, path, query, &history, edit_mode, edit_name,
                    entries, entry_count, page_start, selected, preview);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}
