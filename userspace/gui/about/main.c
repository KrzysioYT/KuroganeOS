#include "../common.h"
#include "../../../common/version.h"

int main(void) {
    const ku_window_t window = gui_open("ABOUT KUROGANEOS", 330, 205, 520, 310);
    if (window == KU_INVALID_WINDOW) return 1;

    kui_scene scene;
    kui_flow root;
    kui_flow details;
    char version_line[64] = "KUROGANEOS ";
    (void)strlcpy(version_line + strlen(version_line), KUROGANE_VERSION_STRING,
        sizeof(version_line) - strlen(version_line));
    (void)strlcpy(version_line + strlen(version_line), " / RED FLUX",
        sizeof(version_line) - strlen(version_line));

    kui_scene_initialize(&scene);
    kui_scene_set_palette(
        &scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));
    kui_flow_begin(&root, &scene, 0U);
    (void)kui_flow_panel(&root, 1U, "ABOUT / KUROGANEOS");
    (void)kui_flow_label(&root, 2U, version_line);
    (void)kui_flow_separator(&root, 3U);

    kui_flow_begin(&details, &scene, 1U);
    (void)kui_flow_label(&details, 10U, "X86-64 UEFI / PRIVATE ADDRESS SPACES");
    (void)kui_flow_label(&details, 11U, "PREEMPTIVE RING 3 / PID 1 USERSPACE");
    (void)kui_flow_label(&details, 12U, "FAT32 / AHCI / NATIVE APPLICATIONS");
    (void)kui_flow_label(&details, 13U, "RED FLUX / LAUNCHER / SHARED SHELL CORE");
    (void)kui_flow_label(&details, 14U, "INDEPENDENT KERNEL AND DESKTOP STACK");

    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] desktop_about_ring3: PASS");
    puts("[TEST] flux_scene_about: PASS");
    puts("[TEST] red_flux_about: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
    }
    (void)ku_ui_close(window);
    return 0;
}
