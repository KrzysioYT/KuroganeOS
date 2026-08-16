#include "../common.h"
#include "../../../common/version.h"

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static void append_percent(char* line, size_t capacity, uint32_t value) {
    char number[24];
    gui_u64(number, sizeof(number), value > 100U ? 100U : value);
    append_text(line, capacity, number);
    append_text(line, capacity, "%");
}

static void append_mib(char* line, size_t capacity, uint64_t bytes) {
    char number[24];
    gui_u64(number, sizeof(number), bytes / UINT64_C(1048576));
    append_text(line, capacity, number);
    append_text(line, capacity, " MiB");
}

static void build_scene(kui_scene* scene, const ku_system_snapshot* snapshot) {
    kui_flow root;
    char cpu[64] = "CPU            ";
    char gpu[64] = "GPU / GFX      ";
    char ram[64] = "RAM            ";
    char disk[64] = "DISK ACTIVITY  ";
    char memory[64] = "MEMORY         ";
    char uptime[64] = "UPTIME TICKS   ";
    char value[24];

    append_percent(cpu, sizeof(cpu), snapshot->cpu_percent);
    append_percent(gpu, sizeof(gpu), snapshot->gpu_percent);
    append_percent(ram, sizeof(ram), snapshot->ram_percent);
    append_percent(disk, sizeof(disk), snapshot->disk_percent);

    append_mib(memory, sizeof(memory),
               snapshot->memory_total_bytes - snapshot->memory_free_bytes);
    append_text(memory, sizeof(memory), " / ");
    append_mib(memory, sizeof(memory), snapshot->memory_total_bytes);

    gui_u64(value, sizeof(value), snapshot->uptime_ticks);
    append_text(uptime, sizeof(uptime), value);

    kui_scene_initialize(scene);
    scene->visible_rows = 12U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "PERFORMANCE / LIVE");
    (void)kui_flow_label(&root, 2U, KUROGANE_PRODUCT_STRING " / SYSTEM ACTIVITY");
    (void)kui_flow_separator(&root, 3U);
    (void)kui_flow_label(&root, 4U, cpu);
    (void)kui_flow_label(&root, 5U, gpu);
    (void)kui_flow_label(&root, 6U, ram);
    (void)kui_flow_label(&root, 7U, disk);
    (void)kui_flow_separator(&root, 8U);
    (void)kui_flow_label(&root, 9U, memory);
    (void)kui_flow_label(&root, 10U, uptime);
    (void)kui_flow_label(&root, 11U, "GPU = GOP/COMPOSITOR ACTIVITY, NOT GPU CORE LOAD");
    (void)kui_flow_progress(&root, 12U, snapshot->cpu_percent, 100U);
}

int main(void) {
    const ku_window_t window = gui_open("PERFORMANCE", 620, 190, 360, 350);
    if (window == KU_INVALID_WINDOW) return 1;

    puts("[TEST] desktop_performance_live: PASS");

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

        for (uint32_t tick = 0U; tick < 100U; ++tick) {
            const int available = kui_next_event(window, &event);
            if (available < 0 ||
                (available > 0 && event.type == KU_UI_EVENT_CLOSE)) {
                (void)ku_ui_close(window);
                return 0;
            }
            (void)kuro_sleep(1U);
        }
    }
}
