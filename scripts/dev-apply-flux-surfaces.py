#!/usr/bin/env python3
"""Apply the bounded retained-surface slice of Flux Stabilization.

This is a guarded development patcher used by the dedicated self-hosted runner.
Every edit is anchored to an exact production fragment.  If the source has
moved, the script aborts rather than guessing or leaving a partial change.
"""

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
    runtime = "kernel/user/runtime_base.inc"
    tests = "tests/test_window_manager.cpp"
    host = "scripts/run-host-tests.sh"

    replace_once(
        header,
        "constexpr size_t MAX_DAMAGE_REGIONS = 16U;\nusing WindowId = uint32_t;",
        "constexpr size_t MAX_DAMAGE_REGIONS = 16U;\n"
        "// Retained surfaces are kernel-owned and deliberately bounded.  The\n"
        "// current public ku_ui_frame compatibility payload is 800 bytes.\n"
        "constexpr size_t MAX_SURFACE_PAYLOAD_BYTES = 4096U;\n"
        "using WindowId = uint32_t;",
    )
    replace_once(
        header,
        "    CapacityReached,\n    InvalidState,",
        "    CapacityReached,\n    PayloadTooLarge,\n    ArithmeticOverflow,\n    InvalidState,",
    )
    replace_once(
        header,
        "struct WorkspaceGeometry {",
        "struct SurfaceView {\n"
        "    size_t width;\n"
        "    size_t height;\n"
        "    size_t stride;\n"
        "    const uint8_t* data;\n"
        "    size_t size;\n"
        "};\n\n"
        "struct WorkspaceGeometry {",
    )
    replace_once(
        header,
        "void invalidate_region(const ui::Rect& region);\nbool render_if_needed();",
        "void invalidate_region(const ui::Rect& region);\n"
        "Status present_surface(\n"
        "    WindowId id,\n"
        "    size_t width,\n"
        "    size_t height,\n"
        "    size_t stride,\n"
        "    const void* payload,\n"
        "    size_t payload_size);\n"
        "Status read_surface(WindowId id, SurfaceView* out_surface);\n"
        "bool render_if_needed();",
    )

    replace_once(
        source,
        "struct Slot {\n    WindowInfo info;",
        "struct SurfaceState {\n"
        "    size_t width;\n"
        "    size_t height;\n"
        "    size_t stride;\n"
        "    size_t size;\n"
        "    bool valid;\n"
        "    uint8_t payload[MAX_SURFACE_PAYLOAD_BYTES];\n"
        "};\n\n"
        "struct Slot {\n"
        "    WindowInfo info;\n"
        "    SurfaceState surface;",
    )
    replace_once(
        source,
        "#endif\n\nvoid mark_full_dirty() {",
        "#endif\n\n"
        "void clear_surface(SurfaceState& surface) {\n"
        "    for (size_t index = 0U; index < MAX_SURFACE_PAYLOAD_BYTES; ++index) {\n"
        "        surface.payload[index] = 0U;\n"
        "    }\n"
        "    surface.width = 0U;\n"
        "    surface.height = 0U;\n"
        "    surface.stride = 0U;\n"
        "    surface.size = 0U;\n"
        "    surface.valid = false;\n"
        "}\n\n"
        "void release_slot(Slot& slot) {\n"
        "    clear_surface(slot.surface);\n"
        "    slot.info = {};\n"
        "    slot.draw = nullptr;\n"
        "    slot.input_callback = nullptr;\n"
        "    slot.context = nullptr;\n"
        "    slot.occupied = false;\n"
        "}\n\n"
        "void mark_full_dirty() {",
    )
    replace_once(
        source,
        "        slot.occupied = false;\n        slot.draw = nullptr;\n        slot.input_callback = nullptr;\n        slot.context = nullptr;",
        "        release_slot(slot);",
    )
    replace_once(
        source,
        "    slot.input_callback = input_callback;\n    slot.context = context;\n    g_order[g_count++]",
        "    slot.input_callback = input_callback;\n"
        "    slot.context = context;\n"
        "    clear_surface(slot.surface);\n"
        "    g_order[g_count++]",
    )
    replace_once(
        source,
        "    --g_count;\n    slot->occupied = false;\n    if (g_dragged == id)",
        "    --g_count;\n    release_slot(*slot);\n    if (g_dragged == id)",
    )
    replace_once(
        source,
        "Status focus(WindowId id) {",
        "Status present_surface(\n"
        "    WindowId id,\n"
        "    size_t width,\n"
        "    size_t height,\n"
        "    size_t stride,\n"
        "    const void* payload,\n"
        "    size_t payload_size) {\n"
        "    if (!g_initialized) return Status::NotInitialized;\n"
        "    Slot* slot = find(id);\n"
        "    if (slot == nullptr) return Status::NotFound;\n"
        "    if (payload == nullptr || width == 0U || height == 0U || stride == 0U ||\n"
        "        width > stride) {\n"
        "        return Status::InvalidArgument;\n"
        "    }\n"
        "    const size_t maximum_size = static_cast<size_t>(-1);\n"
        "    if (stride > maximum_size / height) return Status::ArithmeticOverflow;\n"
        "    const size_t required = stride * height;\n"
        "    if (required != payload_size) return Status::InvalidArgument;\n"
        "    if (required > MAX_SURFACE_PAYLOAD_BYTES) return Status::PayloadTooLarge;\n"
        "\n"
        "    const auto* source_bytes = static_cast<const uint8_t*>(payload);\n"
        "    for (size_t index = 0U; index < required; ++index) {\n"
        "        slot->surface.payload[index] = source_bytes[index];\n"
        "    }\n"
        "    for (size_t index = required; index < slot->surface.size; ++index) {\n"
        "        slot->surface.payload[index] = 0U;\n"
        "    }\n"
        "    slot->surface.width = width;\n"
        "    slot->surface.height = height;\n"
        "    slot->surface.stride = stride;\n"
        "    slot->surface.size = required;\n"
        "    slot->surface.valid = true;\n"
        "    invalidate_window(id);\n"
        "    return Status::Ok;\n"
        "}\n\n"
        "Status read_surface(WindowId id, SurfaceView* out_surface) {\n"
        "    if (!g_initialized) return Status::NotInitialized;\n"
        "    if (out_surface == nullptr) return Status::InvalidArgument;\n"
        "    Slot* slot = find(id);\n"
        "    if (slot == nullptr) return Status::NotFound;\n"
        "    if (!slot->surface.valid) return Status::InvalidState;\n"
        "    out_surface->width = slot->surface.width;\n"
        "    out_surface->height = slot->surface.height;\n"
        "    out_surface->stride = slot->surface.stride;\n"
        "    out_surface->data = slot->surface.payload;\n"
        "    out_surface->size = slot->surface.size;\n"
        "    return Status::Ok;\n"
        "}\n\n"
        "Status focus(WindowId id) {",
    )
    replace_once(
        source,
        "        case Status::CapacityReached: return \"window table full\";\n        case Status::InvalidState:",
        "        case Status::CapacityReached: return \"window table full\";\n"
        "        case Status::PayloadTooLarge: return \"window surface payload too large\";\n"
        "        case Status::ArithmeticOverflow: return \"window surface size overflow\";\n"
        "        case Status::InvalidState:",
    )
    replace_once(
        source,
        "size_t focused_position() {\n"
        "    size_t exposed_position = 0U;\n"
        "    for (size_t position = 0U; position < g_count; ++position) {\n"
        "        const Slot& slot = g_slots[g_order[position]];\n"
        "        if (!exposed(slot) || is_home_surface(slot.info.title)) continue;\n"
        "        if (slot.info.id == g_focused) return exposed_position;\n"
        "        ++exposed_position;\n"
        "    }\n"
        "    return exposed_window_count();\n"
        "}\n\n"
        "bool title_hit",
        "#ifndef KUROGANE_HOST_TEST\n"
        "size_t focused_position() {\n"
        "    size_t exposed_position = 0U;\n"
        "    for (size_t position = 0U; position < g_count; ++position) {\n"
        "        const Slot& slot = g_slots[g_order[position]];\n"
        "        if (!exposed(slot) || is_home_surface(slot.info.title)) continue;\n"
        "        if (slot.info.id == g_focused) return exposed_position;\n"
        "        ++exposed_position;\n"
        "    }\n"
        "    return exposed_window_count();\n"
        "}\n"
        "#endif\n\n"
        "bool title_hit",
    )
    replace_once(
        source,
        "    const DirtyMode pending_mode = g_dirty;\n    const size_t pending_count = g_damage_count;",
        "#ifndef KUROGANE_HOST_TEST\n"
        "    const DirtyMode pending_mode = g_dirty;\n"
        "#endif\n"
        "    const size_t pending_count = g_damage_count;",
    )

    replace_once(
        runtime,
        "    const ku_ui_frame& frame = context->ui.frame;",
        "    ku_ui_frame frame = context->ui.frame;\n"
        "    windowing::SurfaceView retained{};\n"
        "    if (windowing::read_surface(context->ui.window, &retained) == windowing::Status::Ok &&\n"
        "        retained.data != nullptr && retained.size == sizeof(frame)) {\n"
        "        auto* destination = reinterpret_cast<uint8_t*>(&frame);\n"
        "        for (size_t index = 0U; index < sizeof(frame); ++index) {\n"
        "            destination[index] = retained.data[index];\n"
        "        }\n"
        "    }",
    )
    replace_once(
        runtime,
        "            context->ui.frame = *user_frame;\n            system_metrics::record_graphics_work(\n                UINT64_C(1) + static_cast<uint64_t>(user_frame->line_count));\n            windowing::invalidate_window(context->ui.window);",
        "            const windowing::Status surface_status = windowing::present_surface(\n"
        "                context->ui.window, sizeof(ku_ui_frame), 1U, sizeof(ku_ui_frame),\n"
        "                user_frame, sizeof(ku_ui_frame));\n"
        "            if (surface_status != windowing::Status::Ok) {\n"
        "                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);\n"
        "                return;\n"
        "            }\n"
        "            context->ui.frame = *user_frame;\n"
        "            system_metrics::record_graphics_work(\n"
        "                UINT64_C(1) + static_cast<uint64_t>(user_frame->line_count));",
    )

    replace_once(
        tests,
        "    if (!render_if_needed() || render_if_needed()) return 20;\n    return 0;",
        "    if (!render_if_needed() || render_if_needed()) return 20;\n\n"
        "    // A retained surface is copied into kernel-owned bounded storage.\n"
        "    WindowId surface_window = INVALID_WINDOW;\n"
        "    if (create_window(\"Surface\", 12U, {60, 70, 300, 220},\n"
        "                      draw, receive, nullptr, &surface_window) != Status::Ok) return 21;\n"
        "    uint8_t payload[32]{};\n"
        "    for (size_t index = 0U; index < sizeof(payload); ++index) {\n"
        "        payload[index] = static_cast<uint8_t>(index + 1U);\n"
        "    }\n"
        "    if (present_surface(surface_window, 8U, 4U, 8U, payload, sizeof(payload)) != Status::Ok) {\n"
        "        return 22;\n"
        "    }\n"
        "    payload[0] = UINT8_C(0xff);\n"
        "    SurfaceView retained{};\n"
        "    if (read_surface(surface_window, &retained) != Status::Ok ||\n"
        "        retained.width != 8U || retained.height != 4U || retained.stride != 8U ||\n"
        "        retained.size != sizeof(payload) || retained.data == nullptr ||\n"
        "        retained.data[0] != UINT8_C(1) || retained.data[31] != UINT8_C(32)) return 23;\n\n"
        "    if (present_surface(surface_window, 8U, 4U, 7U, payload, 28U) != Status::InvalidArgument ||\n"
        "        present_surface(surface_window, 8U, 4U, 8U, payload, 31U) != Status::InvalidArgument) {\n"
        "        return 24;\n"
        "    }\n"
        "    if (present_surface(surface_window, 1U, 2U, static_cast<size_t>(-1),\n"
        "                        payload, sizeof(payload)) != Status::ArithmeticOverflow) return 25;\n"
        "    uint8_t oversized[MAX_SURFACE_PAYLOAD_BYTES + 1U]{};\n"
        "    if (present_surface(surface_window, 1U, sizeof(oversized), 1U,\n"
        "                        oversized, sizeof(oversized)) != Status::PayloadTooLarge) return 26;\n\n"
        "    const WindowId stale_surface = surface_window;\n"
        "    if (close(surface_window) != Status::Ok ||\n"
        "        read_surface(stale_surface, &retained) != Status::NotFound) return 27;\n"
        "    WindowId replacement = INVALID_WINDOW;\n"
        "    if (create_window(\"Replacement\", 13U, {70, 80, 300, 220},\n"
        "                      draw, receive, nullptr, &replacement) != Status::Ok ||\n"
        "        replacement == stale_surface ||\n"
        "        read_surface(replacement, &retained) != Status::InvalidState ||\n"
        "        read_surface(stale_surface, &retained) != Status::NotFound) return 28;\n"
        "    if (close(replacement) != Status::Ok) return 29;\n"
        "    return 0;",
    )

    replace_once(
        host,
        "\"$OUT_DIR/test_diagnostic_event_ring\"\n\n# Exercise the production TCP client",
        "\"$OUT_DIR/test_diagnostic_event_ring\"\n\n"
        "# Exercise the production Flux Window Core, including retained surface\n"
        "# ownership and generation-safe cleanup, without framebuffer hardware.\n"
        "\"$HOST_CXX\" \\\n"
        "  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \\\n"
        "  -DKUROGANE_HOST_TEST \\\n"
        "  tests/test_window_manager.cpp \\\n"
        "  kernel/ui/window_manager.cpp \\\n"
        "  -o \"$OUT_DIR/test_window_manager\"\n\n"
        "\"$OUT_DIR/test_window_manager\"\n\n"
        "# Exercise the production TCP client",
    )

    print("[dev-apply-flux-surfaces] applied retained-surface production patch")


if __name__ == "__main__":
    main()
