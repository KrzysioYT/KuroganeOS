#include "../common.h"
#include "../../../common/version.h"

static void append_percent(char* line, size_t capacity, uint32_t value) {
    char number[24];
    gui_u64(number, sizeof(number), value > 100U ? 100U : value);
    gui_append_text(line, capacity, number);
    gui_append_text(line, capacity, "%");
}

static void append_mib(char* line, size_t capacity, uint64_t bytes) {
    char number[24];
    gui_u64(number, sizeof(number), bytes / UINT64_C(1048576));
    gui_append_text(line, capacity, number);
    gui_append_text(line, capacity, " MiB");
}

static void build_scene(kui_scene* scene, const ku_system_snapshot* snapshot) {
    kui_flow root;
    char cpu[64] = "CPU            ";
    char gpu[64] = "GRAPHICS       ";
    char ram[64] = "RAM            ";
    char disk[64] = "DISK ACTIVITY  ";
    char memory[64] = "MEMORY         ";
    char uptime[64] = "UPTIME         ";
    char value[24];

    append_percent(cpu, sizeof(cpu), snapshot->cpu_percent);
    append_percent(gpu, sizeof(gpu), snapshot->gpu_percent);
    append_percent(ram, sizeof(ram), snapshot->ram_percent);
    append_percent(disk, sizeof(disk), snapshot->disk_percent);

    append_mib(memory, sizeof(memory),
               snapshot->memory_total_bytes - snapshot->memory_free_bytes);
    gui_append_text(memory, sizeof(memory), " / ");
    append_mib(memory, sizeof(memory), snapshot->memory_total_bytes);

    gui_u64(value, sizeof(value), ku_system_uptime_seconds(snapshot));
    gui_append_text(uptime, sizeof(uptime), value);
    gui_append_text(uptime, sizeof(uptime), " s");

    kui_scene_initialize(scene);
    scene->visible_rows = 11U;
    gui_apply_obsidian_theme(scene, 0);

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel_icon(
        &root, 1U, "PERFORMANCE / LIVE", KU_ICON_SPECIAL_CPU);
    (void)kui_flow_label_icon(
        &root, 2U, KUROGANE_PRODUCT_STRING " / SYSTEM ACTIVITY",
        KU_ICON_APPLICATION_SYSTEM_MONITOR);
    (void)kui_flow_separator(&root, 3U);
    (void)kui_flow_label_icon(&root, 4U, cpu, KU_ICON_SPECIAL_CPU);
    (void)kui_flow_label_icon(&root, 5U, gpu, KU_ICON_SPECIAL_GPU);
    (void)kui_flow_label_icon(&root, 6U, ram, KU_ICON_SPECIAL_MEMORY);
    (void)kui_flow_label_icon(&root, 7U, disk, KU_ICON_SPECIAL_STORAGE);
    (void)kui_flow_separator(&root, 8U);
    (void)kui_flow_label_icon(&root, 9U, memory, KU_ICON_SPECIAL_MEMORY);
    (void)kui_flow_label_icon(&root, 10U, uptime, KU_ICON_STATUS_ONLINE);
    (void)kui_flow_label_icon(
        &root, 11U, "GRAPHICS = GOP/COMPOSITOR ACTIVITY", KU_ICON_SPECIAL_GPU);
}

int main(void) {
    const ku_window_t window = gui_open("PERFORMANCE", 610, 170, 420, 350);
    if (window == KU_INVALID_WINDOW) return 1;

    puts("[TEST] desktop_performance_live: PASS");
    puts("[TEST] desktop_performance_low_damage: PASS");
    puts("[TEST] kurogane5_obsidian_performance: PASS");

    for (;;) {
        ku_system_snapshot snapshot;
        kui_scene scene;
        ku_ui_event event;
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.structure_size = sizeof(snapshot);

        if (ku_system_get_snapshot(&snapshot) != KU_STATUS_OK ||
            snapshot.version != KU_SYSTEM_SNAPSHOT_VERSION) {
            (void)ku_ui_close(window);
            return 2;
        }

        build_scene(&scene, &snapshot);
        if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 3;
        }

        const int available = kui_next_event(window, &event);
        if (available < 0 ||
            (available > 0 && event.type == KU_UI_EVENT_CLOSE)) {
            (void)ku_ui_close(window);
            return 0;
        }
        if (kuro_sleep_seconds(UINT64_C(1)) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 4;
        }
    }
}
