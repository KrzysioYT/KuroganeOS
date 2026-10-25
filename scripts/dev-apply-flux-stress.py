#!/usr/bin/env python3
"""Apply bounded Window Core resource accounting and 3.6 churn stress."""

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
        "struct InteractionSnapshot {\n    WindowId focused;\n    WindowId dragged;\n    WindowId resized;\n};\n\n",
        "struct InteractionSnapshot {\n"
        "    WindowId focused;\n"
        "    WindowId dragged;\n"
        "    WindowId resized;\n"
        "};\n\n"
        "struct ResourceSnapshot {\n"
        "    size_t windows;\n"
        "    size_t retained_surfaces;\n"
        "    size_t retained_bytes;\n"
        "};\n\n",
    )
    replace_once(
        "kernel/ui/window_manager.hpp",
        "Status interaction_snapshot(InteractionSnapshot* out_snapshot);\nStatus present_surface(",
        "Status interaction_snapshot(InteractionSnapshot* out_snapshot);\n"
        "Status resource_snapshot(ResourceSnapshot* out_snapshot);\n"
        "Status present_surface(",
    )

    replace_once(
        "kernel/ui/window_manager.cpp",
        "    out_snapshot->resized = g_resized;\n    return Status::Ok;\n}\n\nbool render_if_needed() {",
        "    out_snapshot->resized = g_resized;\n"
        "    return Status::Ok;\n"
        "}\n\n"
        "Status resource_snapshot(ResourceSnapshot* out_snapshot) {\n"
        "    if (!g_initialized) return Status::NotInitialized;\n"
        "    if (out_snapshot == nullptr) return Status::InvalidArgument;\n"
        "    *out_snapshot = {};\n"
        "    for (const Slot& slot : g_slots) {\n"
        "        if (!slot.occupied) continue;\n"
        "        ++out_snapshot->windows;\n"
        "        if (!slot.surface.valid) continue;\n"
        "        ++out_snapshot->retained_surfaces;\n"
        "        out_snapshot->retained_bytes += slot.surface.size;\n"
        "    }\n"
        "    return Status::Ok;\n"
        "}\n\n"
        "bool render_if_needed() {",
    )

    replace_once(
        "tests/test_window_manager.cpp",
        "    if (close(next_home_slot) != Status::Ok) return 63;\n    return 0;\n}",
        r'''    if (close(next_home_slot) != Status::Ok) return 63;

    // P6 churn: exercise the production state machine repeatedly while
    // accounting every bounded retained resource. A leak or stale generation
    // makes the exact baseline checks fail deterministically.
    ResourceSnapshot baseline{};
    if (resource_snapshot(&baseline) != Status::Ok ||
        baseline.windows != window_count() ||
        baseline.retained_surfaces != 0U || baseline.retained_bytes != 0U) return 64;
    for (size_t iteration = 0U; iteration < 256U; ++iteration) {
        WindowId stress = INVALID_WINDOW;
        if (create_window("FluxStress", 1000U + iteration,
                          {90, 100, 300, 220}, draw, receive, nullptr, &stress) != Status::Ok ||
            stress == INVALID_WINDOW || focus(stress) != Status::Ok) return 65;

        uint8_t stress_payload[64]{};
        stress_payload[0] = static_cast<uint8_t>(iteration);
        stress_payload[63] = static_cast<uint8_t>(iteration ^ 0x5aU);
        if (present_surface(stress, 8U, 8U, 8U,
                            stress_payload, sizeof(stress_payload)) != Status::Ok) return 66;
        SurfaceView stress_surface{};
        if (read_surface(stress, &stress_surface) != Status::Ok ||
            stress_surface.size != sizeof(stress_payload) || stress_surface.data == nullptr ||
            stress_surface.data[0] != stress_payload[0] ||
            stress_surface.data[63] != stress_payload[63]) return 67;

        ResourceSnapshot peak{};
        if (resource_snapshot(&peak) != Status::Ok ||
            peak.windows != baseline.windows + 1U ||
            peak.retained_surfaces != baseline.retained_surfaces + 1U ||
            peak.retained_bytes != baseline.retained_bytes + sizeof(stress_payload) ||
            peak.windows > MAX_WINDOWS ||
            peak.retained_bytes > MAX_WINDOWS * MAX_SURFACE_PAYLOAD_BYTES) return 68;

        const int32_t target_x = workspace.work_area.x +
            static_cast<int32_t>(iteration % 37U);
        const int32_t target_y = workspace.work_area.y +
            static_cast<int32_t>(iteration % 29U);
        if (move(stress, target_x, target_y) != Status::Ok ||
            query(stress, &info) != Status::Ok) return 69;

        if (chrome_geometry(stress, &chrome) != Status::Ok) return 70;
        event = {};
        event.type = input::EventType::MouseButtonDown;
        event.button = drivers::mouse::Left;
        event.buttons = drivers::mouse::Left;
        event.x = chrome.resize_grip.x + 1;
        event.y = chrome.resize_grip.y + 1;
        if (dispatch(event) != Status::Ok) return 71;
        event.type = input::EventType::MouseMove;
        // Move beyond the bottom-right edge, not merely farther inside the
        // 18px resize grip. This exercises growth while remaining subject to
        // Window Core workspace clamps.
        event.x = chrome.resize_grip.x + chrome.resize_grip.width + 24 +
            static_cast<int32_t>(iteration % 11U);
        event.y = chrome.resize_grip.y + chrome.resize_grip.height + 20 +
            static_cast<int32_t>(iteration % 7U);
        if (dispatch(event) != Status::Ok) return 72;
        event.type = input::EventType::MouseButtonUp;
        event.buttons = 0U;
        if (dispatch(event) != Status::Ok || query(stress, &info) != Status::Ok ||
            info.bounds.width < 300 || info.bounds.height < 220) return 73;

        if (minimize(stress) != Status::Ok || restore(stress) != Status::Ok ||
            maximize(stress) != Status::Ok || restore(stress) != Status::Ok ||
            focused_window() != stress) return 74;
        invalidate_region({
            info.bounds.x - 3, info.bounds.y - 2,
            info.bounds.width + 6, info.bounds.height + 4});

        const WindowId stale_stress = stress;
        if (close(stress) != Status::Ok || query(stale_stress, &info) != Status::NotFound ||
            read_surface(stale_stress, &stress_surface) != Status::NotFound) return 75;
        ResourceSnapshot after{};
        if (resource_snapshot(&after) != Status::Ok ||
            after.windows != baseline.windows ||
            after.retained_surfaces != baseline.retained_surfaces ||
            after.retained_bytes != baseline.retained_bytes) return 76;
        if (!render_if_needed()) return 77;
    }
    return 0;
}''',
    )

    replace_once(
        "userspace/system/flux-surface-probe/main.c",
        "#define CHURN_ITERATIONS 20U",
        "#define CHURN_ITERATIONS 64U",
    )

    print("[dev-apply-flux-stress] applied resource accounting and 256-iteration Window Core stress")


if __name__ == "__main__":
    main()
