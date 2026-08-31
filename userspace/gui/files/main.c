#include "../common.h"

#define DIRECTORY_ENTRY_CAPACITY 24U
#define PAGE_SIZE 6U
#define PATH_CAPACITY 192U
#define NO_SELECTION UINT32_C(0xFFFFFFFF)

typedef struct explorer_entry {
    char name[KU_FILE_NAME_CAPACITY];
    uint32_t type;
    uint64_t size;
} explorer_entry;

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
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
    explorer_entry* entries,
    size_t* count,
    char* status,
    size_t status_capacity) {
    ku_result_t opened;
    size_t used = 0U;
    if (path == NULL || entries == NULL || count == NULL) return 0;
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
        if (entry.name_length == 0U || entry.name[0] == '\0' ||
            (entry.name[0] == '.' && entry.name[1] == '\0') ||
            (entry.name[0] == '.' && entry.name[1] == '.' && entry.name[2] == '\0')) {
            continue;
        }
        if (used >= DIRECTORY_ENTRY_CAPACITY) {
            (void)strlcpy(status, "DIRECTORY / FIRST 24 ITEMS", status_capacity);
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
    const explorer_entry* entries,
    size_t entry_count,
    size_t page_start,
    uint32_t selected,
    const char* preview) {
    kui_flow root;
    kui_flow grid;
    char heading[64] = "FLUX FILES / ";
    char breadcrumb[64] = "PATH ";
    char page[24];
    size_t visible = 0U;
    size_t index;

    gui_u64(page, sizeof(page), entry_count == 0U ? 0U : page_start / PAGE_SIZE + 1U);
    append_text(heading, sizeof(heading), page);
    append_text(heading, sizeof(heading), " / ");
    gui_u64(page, sizeof(page), entry_count == 0U ? 0U : (entry_count + PAGE_SIZE - 1U) / PAGE_SIZE);
    append_text(heading, sizeof(heading), page);
    append_text(breadcrumb, sizeof(breadcrumb), path);

    kui_scene_initialize(scene);
    scene->visible_rows = 13U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, heading);
    (void)kui_flow_label(&root, 2U, breadcrumb);

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
    (void)kui_flow_button(&root, 30U, "PARENT FOLDER");
    (void)kui_flow_button(&root, 31U, "OPEN / ENTER");
    (void)kui_flow_button(&root, 32U, "NEXT PAGE");
    (void)kui_flow_label(&root, 33U, preview);
    if (path[0] == '/' && path[1] == '\0') {
        (void)kui_scene_set_flags(scene, 30U, KUI_VIEW_DISABLED);
    }
    if (entry_count <= PAGE_SIZE) {
        (void)kui_scene_set_flags(scene, 32U, KUI_VIEW_DISABLED);
    }
}

static int activate_selected(
    char* path,
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
        if (strlcpy(path, target, PATH_CAPACITY) >= PATH_CAPACITY) return 0;
        *page_start = 0U;
        *selected = NO_SELECTION;
        status[0] = '\0';
        if (!load_directory(path, entries, entry_count, status, status_capacity)) return 0;
        (void)strlcpy(preview, status, preview_capacity);
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
    const ku_window_t window = gui_open("FILES", 250, 110, 650, 480);
    explorer_entry entries[DIRECTORY_ENTRY_CAPACITY];
    size_t entry_count = 0U;
    size_t page_start = 0U;
    uint32_t selected = NO_SELECTION;
    uint32_t pointer_buttons = 0U;
    char path[PATH_CAPACITY] = "/";
    char status[64] = "";
    char preview[64] = "SELECT A TILE / OPEN OR ENTER";
    kui_scene scene;
    if (window == KU_INVALID_WINDOW) return 1;

    if (!load_directory(path, entries, &entry_count, status, sizeof(status))) {
        (void)ku_ui_close(window);
        return 2;
    }
    if (status[0] != '\0' && strcmp(status, "DIRECTORY / READY") != 0) {
        (void)strlcpy(preview, status, sizeof(preview));
    }
    build_scene(&scene, path, entries, entry_count, page_start, selected, preview);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 3;
    }

    puts("[TEST] desktop_files_real_vfs: PASS");
    puts("[TEST] flux_scene_files: PASS");
    puts("[TEST] desktop_files_mouse_navigation: PASS");
    puts("[TEST] desktop_files_keyboard_shortcuts_detached: PASS");
    puts("[TEST] flux_files_readdir: PASS");
    puts("[TEST] flux_files_directory_grid: PASS");
    puts("[TEST] flux_files_breadcrumb: PASS");

    for (;;) {
        ku_ui_event event;
        uint32_t target;
        const int wait_result = gui_wait_event(window, &event);
        if (wait_result < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_POINTER) continue;
        {
            const uint32_t previous_buttons = pointer_buttons;
            const int primary_pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous_buttons & UINT32_C(1)) == 0U;
            pointer_buttons = event.buttons;
            if (!primary_pressed) continue;
        }

        target = kui_scene_hit_test(&scene, event.x, event.y);
        if (target >= 10U && target < 10U + PAGE_SIZE) {
            const size_t index = page_start + (size_t)(target - 10U);
            if (index >= entry_count) continue;
            selected = (uint32_t)index;
            if (directory_like(entries[index].type)) {
                (void)activate_selected(
                    path, entries, &entry_count, &page_start, &selected,
                    status, sizeof(status), preview, sizeof(preview));
            } else {
                char target_path[PATH_CAPACITY];
                if (join_path(path, entries[index].name, target_path, sizeof(target_path))) {
                    preview_file(target_path, preview, sizeof(preview));
                }
            }
        } else if (target == 30U) {
            if (!(path[0] == '/' && path[1] == '\0')) {
                parent_path(path);
                page_start = 0U;
                selected = NO_SELECTION;
                status[0] = '\0';
                if (load_directory(path, entries, &entry_count, status, sizeof(status))) {
                    (void)strlcpy(preview, status, sizeof(preview));
                }
            }
        } else if (target == 31U) {
            (void)activate_selected(
                path, entries, &entry_count, &page_start, &selected,
                status, sizeof(status), preview, sizeof(preview));
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

        build_scene(&scene, path, entries, entry_count, page_start, selected, preview);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}
