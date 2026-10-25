#!/usr/bin/env python3
"""Apply process-wide Flux window/surface cleanup and lifecycle regression."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded match, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> None:
    replace_once(
        "kernel/ui/window_manager.hpp",
        "Status close(WindowId id);\n// User-facing dismissal preserves the persistent HOME surface by\n",
        "Status close(WindowId id);\n"
        "// Process-finalization cleanup: release every window and retained surface\n"
        "// owned by one non-zero PID. This is bounded by MAX_WINDOWS and uses the\n"
        "// same generation-safe lifecycle destruction path as close().\n"
        "Status close_owned_windows(uint64_t owner_pid, size_t* out_closed = nullptr);\n"
        "// User-facing dismissal preserves the persistent HOME surface by\n",
    )

    replace_once(
        "kernel/ui/window_manager.cpp",
        "Status dismiss(WindowId id) {\n",
        "Status close_owned_windows(uint64_t owner_pid, size_t* out_closed) {\n"
        "    if (!g_initialized) return Status::NotInitialized;\n"
        "    if (owner_pid == 0U) return Status::InvalidArgument;\n"
        "    if (out_closed != nullptr) *out_closed = 0U;\n\n"
        "    WindowId owned[MAX_WINDOWS]{};\n"
        "    size_t owned_count = 0U;\n"
        "    for (const Slot& slot : g_slots) {\n"
        "        if (!slot.occupied || slot.info.owner_pid != owner_pid) continue;\n"
        "        owned[owned_count++] = slot.info.id;\n"
        "    }\n\n"
        "    size_t closed = 0U;\n"
        "    for (size_t index = 0U; index < owned_count; ++index) {\n"
        "        const Status status = close(owned[index]);\n"
        "        if (status != Status::Ok) {\n"
        "            if (out_closed != nullptr) *out_closed = closed;\n"
        "            return status;\n"
        "        }\n"
        "        ++closed;\n"
        "    }\n"
        "    if (out_closed != nullptr) *out_closed = closed;\n"
        "    return Status::Ok;\n"
        "}\n\n"
        "Status dismiss(WindowId id) {\n",
    )

    replace_once(
        "kernel/user/runtime.hpp",
        "bool request_termination(uint64_t pid, int32_t exit_code);\n\n",
        "bool request_termination(uint64_t pid, int32_t exit_code);\n"
        "// Final process-level GUI reap. Normal runtime cleanup may already have\n"
        "// closed its primary handle; this additionally catches every owned window\n"
        "// after faults, abnormal exits, and future multi-window processes.\n"
        "bool reclaim_process_windows(uint64_t pid);\n\n",
    )

    replace_once(
        "kernel/user/runtime.cpp",
        "    return Status::Ok;\n}\n\n} // namespace user::runtime\n",
        "    return Status::Ok;\n"
        "}\n\n"
        "bool reclaim_process_windows(uint64_t pid) {\n"
        "    if (pid == 0U) return false;\n"
        "    size_t closed = 0U;\n"
        "    const windowing::Status status = windowing::close_owned_windows(pid, &closed);\n"
        "    static_cast<void>(closed);\n"
        "    return status == windowing::Status::Ok ||\n"
        "           status == windowing::Status::NotInitialized;\n"
        "}\n\n"
        "} // namespace user::runtime\n",
    )

    replace_once(
        "kernel/task/process.cpp",
        "    slot->exit_code = run_image(*slot);\n"
        "    g_current = INVALID_PROCESS_ID;\n"
        "    slot->state = State::Zombie;\n",
        "    slot->exit_code = run_image(*slot);\n"
        "#ifndef KUROGANE_HOST_TEST\n"
        "    // The runtime context currently tracks one primary UI handle, but a\n"
        "    // process lifecycle owns all windows created with its PID. Reap them\n"
        "    // unconditionally after normal return or isolated Ring-3 fault.\n"
        "    static_cast<void>(user::runtime::reclaim_process_windows(slot->pid));\n"
        "#endif\n"
        "    g_current = INVALID_PROCESS_ID;\n"
        "    slot->state = State::Zombie;\n",
    )

    replace_once(
        "tests/test_window_manager.cpp",
        "        if (!render_if_needed()) return 77;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        r'''        if (!render_if_needed()) return 77;
    }

    // P3 process-wide lifecycle reap: multiple windows and retained surfaces
    // owned by one PID must disappear together without touching another PID.
    ResourceSnapshot owner_baseline{};
    if (resource_snapshot(&owner_baseline) != Status::Ok) return 78;
    WindowId owner_first = INVALID_WINDOW;
    WindowId owner_second = INVALID_WINDOW;
    WindowId foreign_window = INVALID_WINDOW;
    if (create_window("OwnerA-1", UINT64_C(4242), {120, 130, 300, 220},
                      draw, receive, nullptr, &owner_first) != Status::Ok ||
        create_window("OwnerA-2", UINT64_C(4242), {180, 170, 320, 240},
                      draw, receive, nullptr, &owner_second) != Status::Ok ||
        create_window("OwnerB", UINT64_C(4343), {240, 210, 300, 220},
                      draw, receive, nullptr, &foreign_window) != Status::Ok) return 79;

    uint8_t owner_payload[64]{};
    owner_payload[0] = 0x5aU;
    owner_payload[63] = 0xa5U;
    if (present_surface(owner_first, 8U, 8U, 8U,
                        owner_payload, sizeof(owner_payload)) != Status::Ok ||
        present_surface(owner_second, 8U, 8U, 8U,
                        owner_payload, sizeof(owner_payload)) != Status::Ok ||
        present_surface(foreign_window, 8U, 8U, 8U,
                        owner_payload, sizeof(owner_payload)) != Status::Ok ||
        focus(owner_second) != Status::Ok) return 80;

    if (chrome_geometry(owner_second, &chrome) != Status::Ok) return 81;
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = chrome.resize_grip.x + 1;
    event.y = chrome.resize_grip.y + 1;
    if (dispatch(event) != Status::Ok) return 82;
    InteractionSnapshot owner_interaction{};
    if (interaction_snapshot(&owner_interaction) != Status::Ok ||
        owner_interaction.resized != owner_second) return 83;

    ResourceSnapshot owner_peak{};
    if (resource_snapshot(&owner_peak) != Status::Ok ||
        owner_peak.windows != owner_baseline.windows + 3U ||
        owner_peak.retained_surfaces != owner_baseline.retained_surfaces + 3U ||
        owner_peak.retained_bytes != owner_baseline.retained_bytes +
            3U * sizeof(owner_payload)) return 84;

    size_t closed_owned = 0U;
    if (close_owned_windows(UINT64_C(4242), &closed_owned) != Status::Ok ||
        closed_owned != 2U || query(owner_first, &info) != Status::NotFound ||
        query(owner_second, &info) != Status::NotFound ||
        query(foreign_window, &info) != Status::Ok) return 85;
    SurfaceView owner_surface{};
    if (read_surface(owner_first, &owner_surface) != Status::NotFound ||
        read_surface(owner_second, &owner_surface) != Status::NotFound ||
        read_surface(foreign_window, &owner_surface) != Status::Ok) return 86;

    if (interaction_snapshot(&owner_interaction) != Status::Ok ||
        owner_interaction.focused == owner_first ||
        owner_interaction.focused == owner_second ||
        owner_interaction.dragged != INVALID_WINDOW ||
        owner_interaction.resized != INVALID_WINDOW) return 87;

    ResourceSnapshot owner_after{};
    if (resource_snapshot(&owner_after) != Status::Ok ||
        owner_after.windows != owner_baseline.windows + 1U ||
        owner_after.retained_surfaces != owner_baseline.retained_surfaces + 1U ||
        owner_after.retained_bytes != owner_baseline.retained_bytes +
            sizeof(owner_payload)) return 88;
    if (close_owned_windows(UINT64_C(9999), &closed_owned) != Status::Ok ||
        closed_owned != 0U ||
        close_owned_windows(0U, &closed_owned) != Status::InvalidArgument) return 89;
    if (close(foreign_window) != Status::Ok) return 90;
    if (resource_snapshot(&owner_after) != Status::Ok ||
        owner_after.windows != owner_baseline.windows ||
        owner_after.retained_surfaces != owner_baseline.retained_surfaces ||
        owner_after.retained_bytes != owner_baseline.retained_bytes) return 91;
    return 0;
}
''',
    )

    print("[dev-apply-process-window-reap] applied PID-wide Flux lifecycle cleanup")


if __name__ == "__main__":
    main()
