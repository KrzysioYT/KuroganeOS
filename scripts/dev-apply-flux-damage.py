#!/usr/bin/env python3
"""Apply the bounded damage-region qualification slice for Flux 3.6."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one guarded match, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> None:
    header = "kernel/ui/window_manager.hpp"
    source = "kernel/ui/window_manager.cpp"
    tests = "tests/test_window_manager.cpp"

    replace_once(
        header,
        "struct WorkspaceGeometry {",
        "struct DamageSnapshot {\n"
        "    bool full;\n"
        "    size_t count;\n"
        "    ui::Rect regions[MAX_DAMAGE_REGIONS];\n"
        "};\n\n"
        "struct WorkspaceGeometry {",
    )
    replace_once(
        header,
        "void invalidate_region(const ui::Rect& region);\nStatus present_surface(",
        "void invalidate_region(const ui::Rect& region);\n"
        "Status damage_snapshot(DamageSnapshot* out_snapshot);\n"
        "Status present_surface(",
    )

    replace_once(
        source,
        "void invalidate_region(const ui::Rect& region) {\n    add_damage_region(region);\n}\n\nbool render_if_needed() {",
        "void invalidate_region(const ui::Rect& region) {\n"
        "    add_damage_region(region);\n"
        "}\n\n"
        "Status damage_snapshot(DamageSnapshot* out_snapshot) {\n"
        "    if (!g_initialized) return Status::NotInitialized;\n"
        "    if (out_snapshot == nullptr) return Status::InvalidArgument;\n"
        "    *out_snapshot = {};\n"
        "    out_snapshot->full = g_dirty == DirtyMode::Full;\n"
        "    if (g_dirty != DirtyMode::Regions) return Status::Ok;\n"
        "    out_snapshot->count = g_damage_count;\n"
        "    for (size_t index = 0U; index < g_damage_count; ++index) {\n"
        "        out_snapshot->regions[index] = g_damage_regions[index];\n"
        "    }\n"
        "    return Status::Ok;\n"
        "}\n\n"
        "bool render_if_needed() {",
    )

    replace_once(
        tests,
        "    if (close(replacement) != Status::Ok) return 29;\n    return 0;",
        "    if (close(replacement) != Status::Ok) return 29;\n\n"
        "    // Damage inspection reads the production Window Core state; it does\n"
        "    // not duplicate clipping/merge logic in the test.\n"
        "    if (!render_if_needed()) return 30;\n"
        "    DamageSnapshot damage{};\n"
        "    if (damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 0U) {\n"
        "        return 31;\n"
        "    }\n\n"
        "    WindowId damage_window = INVALID_WINDOW;\n"
        "    if (create_window(\"Damage\", 14U, {80, 90, 300, 220},\n"
        "                      draw, receive, nullptr, &damage_window) != Status::Ok ||\n"
        "        !render_if_needed()) return 32;\n"
        "    uint8_t damage_payload[16]{};\n"
        "    if (present_surface(damage_window, 4U, 4U, 4U,\n"
        "                        damage_payload, sizeof(damage_payload)) != Status::Ok ||\n"
        "        damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1U ||\n"
        "        damage.regions[0].x != 80 || damage.regions[0].y != 90 ||\n"
        "        damage.regions[0].width != 300 || damage.regions[0].height != 220) return 33;\n\n"
        "    if (!render_if_needed()) return 34;\n"
        "    invalidate_region({10, 10, 20, 20});\n"
        "    invalidate_region({30, 10, 10, 20});\n"
        "    if (damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1U ||\n"
        "        damage.regions[0].x != 10 || damage.regions[0].y != 10 ||\n"
        "        damage.regions[0].width != 30 || damage.regions[0].height != 20) return 35;\n\n"
        "    if (!render_if_needed()) return 36;\n"
        "    invalidate_region({-5, -7, 12, 11});\n"
        "    invalidate_region({200, 200, 0, 10});\n"
        "    invalidate_region({200, 200, -4, 10});\n"
        "    if (damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1U ||\n"
        "        damage.regions[0].x != 0 || damage.regions[0].y != 0 ||\n"
        "        damage.regions[0].width != 7 || damage.regions[0].height != 4) return 37;\n\n"
        "    if (!render_if_needed()) return 38;\n"
        "    for (size_t index = 0U; index <= MAX_DAMAGE_REGIONS; ++index) {\n"
        "        invalidate_region({10 + static_cast<int32_t>(index * 4U), 300, 1, 1});\n"
        "    }\n"
        "    if (damage_snapshot(&damage) != Status::Ok || !damage.full || damage.count != 0U) {\n"
        "        return 39;\n"
        "    }\n\n"
        "    if (!render_if_needed()) return 40;\n"
        "    if (focus(second) != Status::Ok || damage_snapshot(&damage) != Status::Ok ||\n"
        "        !damage.full || damage.count != 0U) return 41;\n"
        "    if (close(damage_window) != Status::Ok) return 42;\n"
        "    return 0;",
    )

    print("[dev-apply-flux-damage] applied bounded damage qualification patch")


if __name__ == "__main__":
    main()
