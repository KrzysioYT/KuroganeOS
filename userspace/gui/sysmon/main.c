#include "../common.h"

#define MONITOR_REFRESH_TICKS 100U

int main(void) {
    const ku_window_t window = gui_open("SYSTEM MONITOR", 30, 65, 470, 280);
    if (window == KU_INVALID_WINDOW) return 1;
    puts("[TEST] desktop_sysmon_ring3: PASS");
    uint32_t heartbeat = 0U;
    for (;;) {
        ku_ui_frame frame;
        char pid[24];
        char tid[24];
        char line[64] = "PID ";
        gui_u64(pid, sizeof(pid), ku_process_id());
        gui_u64(tid, sizeof(tid), ku_thread_id());
        (void)strlcpy(line + strlen(line), pid, sizeof(line) - strlen(line));
        (void)strlcpy(line + strlen(line), "  TID ", sizeof(line) - strlen(line));
        (void)strlcpy(line + strlen(line), tid, sizeof(line) - strlen(line));
        kui_frame_initialize(&frame);
        (void)kui_frame_set_line(&frame, 0U, "SYSTEM MONITOR - USERSPACE");
        (void)kui_frame_set_line(&frame, 2U, line);
        (void)kui_frame_set_line(&frame, 3U, "Scheduler heartbeat is active");
        (void)kui_frame_set_line(&frame, 5U, "Processes are isolated Ring 3 ELF64 images.");
        frame.progress_value = heartbeat;
        frame.progress_maximum = 100U;
        (void)kui_present(window, &frame);
        for (uint32_t tick = 0U; tick < MONITOR_REFRESH_TICKS; ++tick) {
            ku_ui_event event;
            const int available = kui_next_event(window, &event);
            if (available < 0 || (available > 0 && event.type == KU_UI_EVENT_CLOSE)) {
                (void)ku_ui_close(window);
                return 0;
            }
            (void)kuro_sleep(1U);
        }
        heartbeat = (heartbeat + 5U) % 101U;
    }
}
