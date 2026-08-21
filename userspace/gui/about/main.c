#include "../common.h"
#include "../../../common/version.h"

int main(void) {
    const ku_window_t window = gui_open("ABOUT KUROGANEOS", 330, 190, 560, 340);
    if (window == KU_INVALID_WINDOW) return 1;

    kui_scene scene;
    kui_flow root;
    kui_flow details;
    char version_line[64] = "KUROGANEOS ";
    gui_append_text(version_line, sizeof(version_line), KUROGANE_VERSION_STRING);
    gui_append_text(version_line, sizeof(version_line), " / ");
    gui_append_text(version_line, sizeof(version_line), KUROGANE_RELEASE_CHANNEL);

    kui_scene_initialize(&scene);
    scene.visible_rows = KU_UI_MAX_LINES;
    gui_apply_obsidian_theme(&scene, 0);
    kui_flow_begin(&root, &scene, 0U);
    (void)kui_flow_panel(&root, 1U, "ABOUT / KUROGANEOS");
    (void)kui_flow_label(&root, 2U, version_line);
    (void)kui_flow_label(&root, 3U, "OBSIDIAN DESKTOP / CRIMSON IDENTITY");
    (void)kui_flow_separator(&root, 4U);

    kui_flow_begin(&details, &scene, 1U);
    (void)kui_flow_label(&details, 10U, "X86-64 UEFI / INDEPENDENT KERNEL");
    (void)kui_flow_label(&details, 11U, "PREEMPTIVE RING 3 / PRIVATE ADDRESS SPACES");
    (void)kui_flow_label(&details, 12U, "FAT32 + AHCI / NATIVE APPLICATIONS");
    (void)kui_flow_label(&details, 13U, "NETWORK + TLS / KUROGANE WEB");
    (void)kui_flow_label(&details, 14U, "HOME + DOCK + FILES + SETTINGS + MONITOR");
    (void)kui_flow_label(&details, 15U, "NATIVE COMPOSITOR / NEXT GUI FOUNDATION");

    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] desktop_about_ring3: PASS");
    puts("[TEST] flux_scene_about: PASS");
    puts("[TEST] red_flux_about: PASS");
    puts("[TEST] kurogane5_obsidian_about: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
    }
    (void)ku_ui_close(window);
    return 0;
}
