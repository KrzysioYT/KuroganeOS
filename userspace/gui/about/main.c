#include "../common.h"
#include "../../../common/version.h"

int main(void) {
    const ku_window_t window = gui_open("ABOUT KUROGANEOS", 330, 205, 500, 300);
    if (window == KU_INVALID_WINDOW) return 1;

    kui_scene scene;
    kui_flow root;
    kui_flow details;
    char version_line[64] = "KUROGANEOS ";
    (void)strlcpy(version_line + strlen(version_line), KUROGANE_VERSION_STRING,
        sizeof(version_line) - strlen(version_line));
    (void)strlcpy(version_line + strlen(version_line), " // FLUX DESKTOP",
        sizeof(version_line) - strlen(version_line));

    kui_scene_initialize(&scene);
    kui_flow_begin(&root, &scene, 0U);
    (void)kui_flow_panel(&root, 1U, "ABOUT // KUROGANEOS");
    (void)kui_flow_label(&root, 2U, version_line);
    (void)kui_flow_separator(&root, 3U);

    kui_flow_begin(&details, &scene, 1U);
    (void)kui_flow_label(&details, 10U, "x86-64 UEFI // private address spaces");
    (void)kui_flow_label(&details, 11U, "preemptive Ring 3 tasks // PID 1 userspace");
    (void)kui_flow_label(&details, 12U, "persistent FAT32 // AHCI // native applications");
    (void)kui_flow_label(&details, 13U, "Flux Window Core // Launcher // libui scenes");
    (void)kui_flow_label(&details, 14U, "Independent kernel and desktop stack.");

    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] desktop_about_ring3: PASS");
    puts("[TEST] flux_scene_about: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
    }
    (void)ku_ui_close(window);
    return 0;
}
