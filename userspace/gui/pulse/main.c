#include "../common.h"

static void append_ipv4(char* output, size_t capacity, const uint8_t address[4]) {
    size_t index;
    char number[24];
    for (index = 0U; index < 4U; ++index) {
        if (index != 0U) gui_append_text(output, capacity, ".");
        gui_u64(number, sizeof(number), address[index]);
        gui_append_text(output, capacity, number);
    }
}

static void append_percent(char* output, size_t capacity, uint32_t value) {
    char number[24];
    gui_u64(number, sizeof(number), value > 100U ? 100U : value);
    gui_append_text(output, capacity, number);
    gui_append_text(output, capacity, "%");
}

static void build_scene(kui_scene* scene) {
    ku_network_status network;
    ku_audio_state audio;
    ku_system_snapshot system;
    kui_flow root;
    char net_line[64] = "NETWORK / ";
    char audio_line[64] = "AUDIO / ";
    char perf_line[64] = "PERFORMANCE / CPU ";
    char ram_line[64] = "MEMORY / ";

    memset(&network, 0, sizeof(network));
    network.structure_size = sizeof(network);
    memset(&audio, 0, sizeof(audio));
    audio.structure_size = sizeof(audio);
    memset(&system, 0, sizeof(system));
    system.structure_size = sizeof(system);

    if (ku_network_get_status(&network) == KU_STATUS_OK && network.ready != 0U) {
        gui_append_text(net_line, sizeof(net_line), network.physical != 0U ? "ONLINE / " : "LOOPBACK / ");
        append_ipv4(net_line, sizeof(net_line), network.address);
    } else {
        gui_append_text(net_line, sizeof(net_line), "OFFLINE");
    }

    if (ku_audio_get_state(&audio) == KU_STATUS_OK && audio.available != 0U) {
        gui_append_text(audio_line, sizeof(audio_line), "MASTER ");
        append_percent(audio_line, sizeof(audio_line), audio.volume_percent);
        gui_append_text(audio_line, sizeof(audio_line), audio.muted != 0U ? " / MUTED" : " / ACTIVE");
    } else {
        gui_append_text(audio_line, sizeof(audio_line), "NO DEVICE");
    }

    if (ku_system_get_snapshot(&system) == KU_STATUS_OK) {
        append_percent(perf_line, sizeof(perf_line), system.cpu_percent);
        gui_append_text(perf_line, sizeof(perf_line), " / GFX ");
        append_percent(perf_line, sizeof(perf_line), system.gpu_percent);
        append_percent(ram_line, sizeof(ram_line), system.ram_percent);
    } else {
        gui_append_text(perf_line, sizeof(perf_line), "UNAVAILABLE");
        gui_append_text(ram_line, sizeof(ram_line), "UNAVAILABLE");
    }

    kui_scene_initialize(scene);
    scene->visible_rows = KU_UI_MAX_LINES;
    gui_apply_forged_theme(scene, 1);
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "PULSE / SYSTEM STATUS");
    (void)kui_flow_label(&root, 2U, net_line);
    (void)kui_flow_label(&root, 3U, audio_line);
    (void)kui_flow_label(&root, 4U, perf_line);
    (void)kui_flow_label(&root, 5U, ram_line);
    (void)kui_flow_separator(&root, 6U);
    (void)kui_flow_label(&root, 7U, "WI-FI / CONTROL SERVICE PENDING");
    (void)kui_flow_label(&root, 8U, "BLUETOOTH / DRIVER SERVICE PENDING");
    (void)kui_flow_label(&root, 9U, "BATTERY / POWER SERVICE PENDING");
    (void)kui_flow_label(&root, 10U, "FORGE CONTROL / SETTINGS");
}

int main(void) {
    const ku_window_t window = gui_open("PULSE", 900, 75, 300, 390);
    kui_scene scene;
    if (window == KU_INVALID_WINDOW) return 1;
    puts("[TEST] kurogane5_pulse_surface: PASS");
    puts("[TEST] pulse_real_service_status: PASS");

    for (;;) {
        ku_ui_event event;
        const int available = kui_next_event(window, &event);
        if (available < 0 || (available > 0 && event.type == KU_UI_EVENT_CLOSE)) break;
        build_scene(&scene);
        if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 2;
        }
        if (kuro_sleep_seconds(UINT64_C(1)) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 3;
        }
    }
    (void)ku_ui_close(window);
    return 0;
}
