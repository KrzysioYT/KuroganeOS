#include "../kernel/ui/window_manager.hpp"

namespace {
size_t g_draws = 0U;
size_t g_inputs = 0U;

void draw(windowing::WindowId, const ui::Rect&, bool, void*) { ++g_draws; }
struct RedrawFixture { unsigned mode = 0; bool nested_render = false;
    windowing::Status resize_status = windowing::Status::Ok; };
void invalidate_during_draw(windowing::WindowId, const ui::Rect&, bool, void* context) {
    auto& fixture = *static_cast<RedrawFixture*>(context);
    const unsigned mode = fixture.mode;
    fixture.mode = 0;
    if (mode == 1) windowing::invalidate_region({12, 15, 20, 25});
    if (mode == 2) windowing::invalidate();
    if (mode == 3) fixture.resize_status = windowing::set_screen_size(640, 480);
    if (mode != 0) fixture.nested_render = windowing::render_if_needed();
}
struct MutationFixture {
    size_t mutator_draws = 0U;
    size_t survivor_draws = 0U;
    size_t replacement_draws = 0U;
    bool closed = false;
    bool recreated = false;
    windowing::WindowId replacement = windowing::INVALID_WINDOW;
};

void replacement_draw(windowing::WindowId, const ui::Rect&, bool, void* context) {
    ++static_cast<MutationFixture*>(context)->replacement_draws;
}

void survivor_draw(windowing::WindowId, const ui::Rect&, bool, void* context) {
    ++static_cast<MutationFixture*>(context)->survivor_draws;
}

void mutate_during_draw(windowing::WindowId id, const ui::Rect&, bool, void* context) {
    auto& fixture = *static_cast<MutationFixture*>(context);
    if (++fixture.mutator_draws != 1U) return;
    fixture.closed = windowing::close(id) == windowing::Status::Ok;
    if (!fixture.closed) return;
    fixture.recreated = windowing::create_window(
        "Replacement", 43U, {90, 100, 300, 220},
        replacement_draw, nullptr, &fixture, &fixture.replacement) ==
        windowing::Status::Ok;
}

void receive(windowing::WindowId, const input::Event&, void*) { ++g_inputs; }

struct WindowListFixture {
    windowing::WindowId first = windowing::INVALID_WINDOW;
    windowing::WindowId second = windowing::INVALID_WINDOW;
    windowing::WindowId replacement = windowing::INVALID_WINDOW;
    windowing::WindowId seen[3]{};
    uint8_t positions[3]{};
    bool focused[3]{};
    size_t count = 0U;
    bool mutate = true;
    bool changed = false;
    bool retained_first_metadata = false;
};

bool list_during_mutation(const windowing::WindowInfo& info, void* context) {
    auto& fixture = *static_cast<WindowListFixture*>(context);
    if (fixture.count >= 3U) return false;
    const size_t index = fixture.count++;
    fixture.seen[index] = info.id;
    fixture.positions[index] = info.z_order;
    fixture.focused[index] = info.focused;
    if (fixture.mutate && index == 0U) {
        const windowing::WindowId original_id = info.id;
        const uint8_t original_z = info.z_order;
        if (windowing::close(fixture.first) != windowing::Status::Ok) return false;
        if (windowing::create_window(
                "Enumerated Replacement", 64U, {140, 150, 330, 220},
                draw, receive, nullptr, &fixture.replacement) !=
            windowing::Status::Ok) return false;
        fixture.changed = windowing::focus(fixture.replacement) ==
            windowing::Status::Ok;
        fixture.retained_first_metadata =
            info.id == original_id && info.z_order == original_z &&
            info.owner_pid == 62U;
    }
    return true;
}

bool stop_during_list(const windowing::WindowInfo&, void* context) {
    ++*static_cast<size_t*>(context);
    return false;
}

struct PointerRoutingProbe {
    size_t moves = 0U;
    size_t downs = 0U;
    size_t ups = 0U;
    size_t wheels = 0U;
    size_t keys = 0U;
    bool close_on_down = false;
};
void receive_pointer_route(windowing::WindowId id, const input::Event& event,
                           void* context) {
    auto& probe = *static_cast<PointerRoutingProbe*>(context);
    switch (event.type) {
        case input::EventType::MouseMove: ++probe.moves; break;
        case input::EventType::MouseButtonDown: ++probe.downs; break;
        case input::EventType::MouseButtonUp: ++probe.ups; break;
        case input::EventType::MouseWheel: ++probe.wheels; break;
        case input::EventType::KeyDown: ++probe.keys; break;
        case input::EventType::KeyUp: break;
    }
    if (probe.close_on_down && event.type == input::EventType::MouseButtonDown) {
        (void)windowing::close(id);
    }
}

} // namespace

int main() {
    using namespace windowing;
    if (set_screen_size(640, 480) != Status::NotInitialized) return 126;
    if (initialize(0U, 0U) != Status::InvalidArgument ||
        initialize(800U, 600U) != Status::Ok) return 1;

    const WorkspaceGeometry workspace = workspace_geometry();
    if (workspace.work_area.x <= 0 || workspace.work_area.y <= 0 ||
        workspace.work_area.width >= 800 || workspace.work_area.height >= 600 ||
        workspace.signal_spine.width <= 0 || workspace.pulse_ribbon.height <= 0) {
        return 2;
    }

    WindowId first = INVALID_WINDOW;
    WindowId second = INVALID_WINDOW;
    if (create_window("First", 10U, {40, 50, 300, 220},
                      draw, receive, nullptr, &first) != Status::Ok ||
        create_window("Second", 11U, {100, 100, 320, 240},
                      draw, receive, nullptr, &second) != Status::Ok ||
        first == second || window_count() != 2U ||
        focused_window() != second) return 3;

    // Floating dock must not consume a full desktop taskbar reservation.
    if (workspace.work_area.height < 520 ||
        workspace.pulse_ribbon.height > 64 ||
        workspace.pulse_ribbon.width >= 800 ||
        !dock_visible()) return 155;
    input::Event dock_probe{};
    dock_probe.type = input::EventType::MouseMove;
    dock_probe.x = 400; dock_probe.y = 180;
    if (dispatch(dock_probe) != Status::Ok || dock_visible()) return 156;
    dock_probe.x = workspace.pulse_ribbon.x + workspace.pulse_ribbon.width / 2;
    dock_probe.y = workspace.pulse_ribbon.y + workspace.pulse_ribbon.height / 2;
    if (dispatch(dock_probe) != Status::Ok || !dock_visible()) return 157;

    WindowInfo info{};
    if (query(second, &info) != Status::Ok || !info.focused ||
        info.owner_pid != 11U || info.z_order != 1U) return 4;
    if (focus(first) != Status::Ok || focused_window() != first ||
        query(first, &info) != Status::Ok || info.z_order != 1U) return 5;

    ChromeGeometry chrome{};
    ui::Rect content{};
    if (chrome_geometry(first, &chrome) != Status::Ok ||
        content_geometry(first, &content) != Status::Ok ||
        chrome.header.height <= 30 || chrome.resize_grip.width <= 0 ||
        chrome.minimize_control.x >= chrome.expand_control.x ||
        chrome.expand_control.x >= chrome.dismiss_control.x ||
        content.x != info.bounds.x + 4 ||
        content.y != chrome.header.y + chrome.header.height ||
        content.width != info.bounds.width - 8 ||
        content.height != info.bounds.height - chrome.header.height - 4) return 6;

    if (move(first, -100, 10000) != Status::Ok ||
        query(first, &info) != Status::Ok ||
        info.bounds.x != workspace.work_area.x ||
        info.bounds.y != workspace.work_area.y + workspace.work_area.height - info.bounds.height) {
        return 7;
    }

    const ui::Rect restored_bounds = info.bounds;
    if (maximize(first) != Status::Ok ||
        query(first, &info) != Status::Ok ||
        info.state != WindowState::Maximized ||
        info.bounds.x != workspace.work_area.x ||
        info.bounds.y != workspace.work_area.y ||
        info.bounds.width != workspace.work_area.width ||
        info.bounds.height != workspace.work_area.height) return 8;
    if (restore(first) != Status::Ok ||
        query(first, &info) != Status::Ok ||
        info.bounds.x != restored_bounds.x || info.bounds.y != restored_bounds.y) return 9;

    if (minimize(first) != Status::Ok || focused_window() != second) return 10;

    // Focus(first) moved it to the top of z-order, so its Pulse Ribbon item is
    // the second item. Clicking that item must restore and focus it.
    ui::Rect pulse_first{};
    if (pulse_item_geometry(1U, &pulse_first) != Status::Ok ||
        pulse_first.width <= 0 || pulse_first.height <= 0) return 11;
    input::Event event{};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = pulse_first.x + pulse_first.width / 2;
    event.y = pulse_first.y + pulse_first.height / 2;
    if (dispatch(event) != Status::Ok || focused_window() != first ||
        query(first, &info) != Status::Ok || info.state != WindowState::Normal) {
        return 12;
    }

    // Header drag still routes through the same chrome geometry.
    if (chrome_geometry(first, &chrome) != Status::Ok) return 13;
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = chrome.header.x + 10;
    event.y = chrome.header.y + 10;
    if (dispatch(event) != Status::Ok) return 14;
    event.type = input::EventType::MouseMove;
    event.x = 200;
    event.y = 150;
    if (dispatch(event) != Status::Ok || query(first, &info) != Status::Ok ||
        info.bounds.x != 190 || info.bounds.y != 140) return 15;
    event.type = input::EventType::MouseButtonUp;
    event.buttons = 0U;
    // System chrome drag/release must not synthesize app callbacks.
    if (dispatch(event) != Status::Ok || g_inputs != 0U) return 16;

    // Exercise the geometric expand control instead of a textual [] button.
    if (chrome_geometry(first, &chrome) != Status::Ok) return 17;
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = chrome.expand_control.x + chrome.expand_control.width / 2;
    event.y = chrome.expand_control.y + chrome.expand_control.height / 2;
    if (dispatch(event) != Status::Ok || query(first, &info) != Status::Ok ||
        info.state != WindowState::Maximized) return 18;

    // Physical desktop/window shortcuts are detached in mouse-first Flux.
    const WindowId shortcut_focus = focused_window();
    event = {};
    event.type = input::EventType::KeyDown;
    event.alt = true;
    event.key = drivers::keyboard::KeyCode::F4;
    if (dispatch(event) != Status::Ok || window_count() != 2U ||
        focused_window() != shortcut_focus || query(first, &info) != Status::Ok) return 19;
    event.key = drivers::keyboard::KeyCode::Tab;
    if (dispatch(event) != Status::Ok || window_count() != 2U ||
        focused_window() != shortcut_focus) return 19;
    event = {};
    event.type = input::EventType::KeyDown;
    event.key = drivers::keyboard::KeyCode::LeftGui;
    if (dispatch(event) != Status::Ok || window_count() != 2U ||
        focused_window() != shortcut_focus) return 19;
    if (chrome_geometry(first, &chrome) != Status::Ok) return 19;
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = chrome.dismiss_control.x + chrome.dismiss_control.width / 2;
    event.y = chrome.dismiss_control.y + chrome.dismiss_control.height / 2;
    if (dispatch(event) != Status::Ok || window_count() != 1U ||
        query(first, &info) != Status::NotFound) return 19;
    if (!render_if_needed() || render_if_needed()) return 20;

    // A retained surface is copied into kernel-owned bounded storage.
    WindowId surface_window = INVALID_WINDOW;
    if (create_window("Surface", 12U, {60, 70, 300, 220},
                      draw, receive, nullptr, &surface_window) != Status::Ok) return 21;
    uint8_t payload[32]{};
    for (size_t index = 0U; index < sizeof(payload); ++index) {
        payload[index] = static_cast<uint8_t>(index + 1U);
    }
    if (present_surface(surface_window, 8U, 4U, 8U, payload, sizeof(payload)) != Status::Ok) {
        return 22;
    }
    payload[0] = UINT8_C(0xff);
    SurfaceView retained{};
    if (read_surface(surface_window, &retained) != Status::Ok ||
        retained.width != 8U || retained.height != 4U || retained.stride != 8U ||
        retained.size != sizeof(payload) || retained.data == nullptr ||
        retained.data[0] != UINT8_C(1) || retained.data[31] != UINT8_C(32)) return 23;

    if (present_surface(surface_window, 8U, 4U, 7U, payload, 28U) != Status::InvalidArgument ||
        present_surface(surface_window, 8U, 4U, 8U, payload, 31U) != Status::InvalidArgument) {
        return 24;
    }
    if (present_surface(surface_window, 1U, 2U, static_cast<size_t>(-1),
                        payload, sizeof(payload)) != Status::ArithmeticOverflow) return 25;
    uint8_t oversized[MAX_SURFACE_PAYLOAD_BYTES + 1U]{};
    if (present_surface(surface_window, 1U, sizeof(oversized), 1U,
                        oversized, sizeof(oversized)) != Status::PayloadTooLarge) return 26;

    const WindowId stale_surface = surface_window;
    if (close(surface_window) != Status::Ok ||
        read_surface(stale_surface, &retained) != Status::NotFound) return 27;
    WindowId replacement = INVALID_WINDOW;
    if (create_window("Replacement", 13U, {70, 80, 300, 220},
                      draw, receive, nullptr, &replacement) != Status::Ok ||
        replacement == stale_surface ||
        read_surface(replacement, &retained) != Status::InvalidState ||
        read_surface(stale_surface, &retained) != Status::NotFound) return 28;
    if (close(replacement) != Status::Ok) return 29;

    // Damage inspection reads the production Window Core state; it does
    // not duplicate clipping/merge logic in the test.
    if (!render_if_needed()) return 30;
    DamageSnapshot damage{};
    if (damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 0U) {
        return 31;
    }

    WindowId damage_window = INVALID_WINDOW;
    if (create_window("Damage", 14U, {80, 90, 300, 220},
                      draw, receive, nullptr, &damage_window) != Status::Ok ||
        !render_if_needed()) return 32;
    uint8_t damage_payload[16]{};
    if (present_surface(damage_window, 4U, 4U, 4U,
                        damage_payload, sizeof(damage_payload)) != Status::Ok ||
        damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1U ||
        damage.regions[0].x != 80 || damage.regions[0].y != 90 ||
        damage.regions[0].width != 300 || damage.regions[0].height != 220) return 33;

    if (!render_if_needed()) return 34;
    invalidate_region({10, 10, 20, 20});
    invalidate_region({30, 10, 10, 20});
    if (damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1U ||
        damage.regions[0].x != 10 || damage.regions[0].y != 10 ||
        damage.regions[0].width != 30 || damage.regions[0].height != 20) return 35;

    if (!render_if_needed()) return 36;
    invalidate_region({-5, -7, 12, 11});
    invalidate_region({200, 200, 0, 10});
    invalidate_region({200, 200, -4, 10});
    if (damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1U ||
        damage.regions[0].x != 0 || damage.regions[0].y != 0 ||
        damage.regions[0].width != 7 || damage.regions[0].height != 4) return 37;

    if (!render_if_needed()) return 38;
    for (size_t index = 0U; index <= MAX_DAMAGE_REGIONS; ++index) {
        invalidate_region({10 + static_cast<int32_t>(index * 4U), 300, 1, 1});
    }
    if (damage_snapshot(&damage) != Status::Ok || !damage.full || damage.count != 0U) {
        return 39;
    }

    if (!render_if_needed()) return 40;
    if (focus(second) != Status::Ok || damage_snapshot(&damage) != Status::Ok ||
        !damage.full || damage.count != 0U) return 41;
    if (close(damage_window) != Status::Ok) return 42;

    // Capture is owned by the live generation and must be cancelled by
    // every state transition that makes drag/resize invalid.
    WindowId capture = INVALID_WINDOW;
    if (create_window("Capture", 15U, {120, 120, 300, 220},
                      draw, receive, nullptr, &capture) != Status::Ok ||
        chrome_geometry(capture, &chrome) != Status::Ok) return 43;
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = chrome.header.x + 10;
    event.y = chrome.header.y + 10;
    if (dispatch(event) != Status::Ok) return 44;
    InteractionSnapshot interaction{};
    if (interaction_snapshot(&interaction) != Status::Ok ||
        interaction.dragged != capture || interaction.resized != INVALID_WINDOW) return 45;
    if (query(capture, &info) != Status::Ok) return 46;
    const ui::Rect before_minimize = info.bounds;
    if (minimize(capture) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.dragged != INVALID_WINDOW ||
        interaction.resized != INVALID_WINDOW ||
        interaction.focused == capture) return 47;
    event = {};
    event.type = input::EventType::MouseMove;
    event.buttons = drivers::mouse::Left;
    event.x = 700;
    event.y = 500;
    if (dispatch(event) != Status::Ok || query(capture, &info) != Status::Ok ||
        info.bounds.x != before_minimize.x || info.bounds.y != before_minimize.y ||
        info.bounds.width != before_minimize.width ||
        info.bounds.height != before_minimize.height) return 48;
    if (restore(capture) != Status::Ok || chrome_geometry(capture, &chrome) != Status::Ok) {
        return 49;
    }
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = chrome.resize_grip.x + chrome.resize_grip.width / 2;
    event.y = chrome.resize_grip.y + chrome.resize_grip.height / 2;
    if (dispatch(event) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.resized != capture || interaction.dragged != INVALID_WINDOW) return 50;
    if (maximize(capture) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.resized != INVALID_WINDOW || interaction.dragged != INVALID_WINDOW) {
        return 51;
    }
    if (restore(capture) != Status::Ok || chrome_geometry(capture, &chrome) != Status::Ok) {
        return 52;
    }
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = chrome.resize_grip.x + 1;
    event.y = chrome.resize_grip.y + 1;
    if (dispatch(event) != Status::Ok || close(capture) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.dragged != INVALID_WINDOW || interaction.resized != INVALID_WINDOW) {
        return 53;
    }
    WindowId capture_replacement = INVALID_WINDOW;
    if (create_window("Capture2", 16U, {130, 130, 300, 220},
                      draw, receive, nullptr, &capture_replacement) != Status::Ok ||
        capture_replacement == capture || query(capture_replacement, &info) != Status::Ok) {
        return 54;
    }
    const ui::Rect replacement_bounds = info.bounds;
    event = {};
    event.type = input::EventType::MouseMove;
    event.buttons = drivers::mouse::Left;
    event.x = 700;
    event.y = 500;
    if (dispatch(event) != Status::Ok || query(capture_replacement, &info) != Status::Ok ||
        info.bounds.x != replacement_bounds.x || info.bounds.y != replacement_bounds.y ||
        info.bounds.width != replacement_bounds.width ||
        info.bounds.height != replacement_bounds.height) return 55;
    if (close(capture_replacement) != Status::Ok) return 56;

    // HOME has separate user-dismiss and process-lifecycle semantics.  A UI
    // dismiss minimizes the persistent surface, while lifecycle close must
    // release the generation and retained bytes immediately.
    WindowId home = INVALID_WINDOW;
    if (create_window("RED FLUX HOME", 17U, {100, 100, 360, 260},
                      draw, receive, nullptr, &home) != Status::Ok ||
        query(home, &info) != Status::Ok ||
        info.state != WindowState::Normal || focused_window() != home ||
        home_surface_ready()) return 57;
    bool reported_ready = true;
    if (desktop_pin(0U, 3U, false, &reported_ready) != Status::Ok ||
        reported_ready ||
        desktop_pin(2U, 3U, false, &reported_ready) != Status::InvalidArgument)
        return 162;
    uint8_t home_payload[16]{};
    home_payload[0] = UINT8_C(0x5a);
    if (present_surface(home, 4U, 4U, 4U, home_payload, sizeof(home_payload)) !=
            Status::Ok || query(home, &info) != Status::Ok ||
        info.state != WindowState::Normal || !home_surface_ready() ||
        desktop_pin(0U, 3U, false, &reported_ready) != Status::Ok ||
        !reported_ready) return 58;
    // Clicking the APPS item on an already-focused Home must NOT minimize it.
    const WorkspaceGeometry dock_at_home = workspace_geometry();
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = dock_at_home.pulse_ribbon.x + 22;
    event.y = dock_at_home.pulse_ribbon.y + dock_at_home.pulse_ribbon.height / 2;
    if (dispatch(event) != Status::Ok || query(home, &info) != Status::Ok ||
        info.state != WindowState::Normal) return 158;
    if (dismiss(home) != Status::Ok || query(home, &info) != Status::Ok ||
        info.state != WindowState::Minimized || home_surface_ready()) return 59;
    // Keyboard escape hatch works even with completely unavailable pointer.
    event = {};
    event.type = input::EventType::KeyDown;
    event.control = true;
    event.alt = true;
    event.key = drivers::keyboard::KeyCode::F1;
    if (dispatch(event) != Status::Ok ||
        query(home, &info) != Status::Ok ||
        info.state != WindowState::Normal ||
        focused_window() != home || !home_surface_ready()) return 159;
    if (dismiss(home) != Status::Ok || query(home, &info) != Status::Ok ||
        info.state != WindowState::Minimized) return 160;
    SurfaceView home_surface{};
    if (read_surface(home, &home_surface) != Status::Ok ||
        home_surface.size != sizeof(home_payload) || home_surface.data == nullptr ||
        home_surface.data[0] != UINT8_C(0x5a)) return 60;
    const WindowId stale_home = home;
    if (close(home) != Status::Ok || query(stale_home, &info) != Status::NotFound ||
        read_surface(stale_home, &home_surface) != Status::NotFound ||
        home_surface_ready()) return 61;
    WindowId next_home_slot = INVALID_WINDOW;
    if (create_window("AfterHome", 18U, {110, 110, 300, 220},
                      draw, receive, nullptr, &next_home_slot) != Status::Ok ||
        next_home_slot == stale_home ||
        read_surface(next_home_slot, &home_surface) != Status::InvalidState) return 62;
    if (close(next_home_slot) != Status::Ok) return 63;

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

    // Floating Dock transitions are intentionally full-desktop repaints;
    // keep them distinct from the bounded per-window hover damage contract.
    // Prime the collapsed state outside all windows before inspecting damage.
    event = {};
    event.type = input::EventType::MouseMove;
    event.x = 700; event.y = 400;
    if (dispatch(event) != Status::Ok) return 161;
    static_cast<void>(render_if_needed());

    // Native pointer feedback is owned by the topmost live WindowId. Pointer
    // movement and button transitions invalidate only window regions so hover
    // and pressed visuals never require a full compositor repaint.
    WindowId hover_window = INVALID_WINDOW;
    if (create_window("Hover", UINT64_C(5151), {140, 150, 300, 220},
                      draw, receive, nullptr, &hover_window) != Status::Ok ||
        !render_if_needed()) return 92;
    event = {};
    event.type = input::EventType::MouseMove;
    event.x = 180;
    event.y = 210;
    if (dispatch(event) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.hovered != hover_window) return 93;
    if (damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1U ||
        damage.regions[0].x != 140 || damage.regions[0].y != 150 ||
        damage.regions[0].width != 300 || damage.regions[0].height != 220) return 94;
    if (!render_if_needed()) return 95;

    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    if (dispatch(event) != Status::Ok || damage_snapshot(&damage) != Status::Ok ||
        damage.full || damage.count != 1U) return 96;
    if (!render_if_needed()) return 97;
    event.type = input::EventType::MouseButtonUp;
    event.buttons = 0U;
    if (dispatch(event) != Status::Ok || damage_snapshot(&damage) != Status::Ok ||
        damage.full || damage.count != 1U) return 98;
    if (!render_if_needed()) return 99;

    event = {};
    event.type = input::EventType::MouseMove;
    event.x = 790;
    event.y = 590;
    if (dispatch(event) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.hovered != INVALID_WINDOW ||
        damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1U) return 100;
    if (!render_if_needed()) return 101;

    event.x = 180;
    event.y = 210;
    if (dispatch(event) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.hovered != hover_window) return 102;
    if (close(hover_window) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.hovered != INVALID_WINDOW) return 103;
    // Invalidations raised by a draw callback belong to the next frame.
    if (initialize(800U, 600U) != Status::Ok) return 104;
    RedrawFixture redraw{};
    WindowId repaint = INVALID_WINDOW;
    if (create_window("Repaint", 31U, {40, 50, 300, 220},
        invalidate_during_draw, receive, &redraw, &repaint) != Status::Ok) return 105;
    redraw.mode = 1;
    if (!render_if_needed() || redraw.nested_render ||
        damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1 ||
        damage.regions[0].x != 12 || damage.regions[0].y != 15 ||
        damage.regions[0].width != 20 || damage.regions[0].height != 25) return 106;
    if (!render_if_needed() || render_if_needed()) return 107;
    invalidate_region({30, 40, 50, 60});
    redraw.mode = 2;
    if (!render_if_needed() || redraw.nested_render ||
        damage_snapshot(&damage) != Status::Ok || !damage.full) return 108;
    if (!render_if_needed() || render_if_needed()) return 109;

    // Mutating the live z-order while drawing used to skip the surviving
    // window and draw a replacement that did not exist at frame start.
    if (initialize(800U, 600U) != Status::Ok) return 110;
    MutationFixture mutation{};
    WindowId mutator = INVALID_WINDOW;
    WindowId survivor = INVALID_WINDOW;
    if (create_window("Mutator", 41U, {40, 50, 300, 220},
            mutate_during_draw, nullptr, &mutation, &mutator) != Status::Ok ||
        create_window("Survivor", 42U, {60, 80, 300, 220},
            survivor_draw, nullptr, &mutation, &survivor) != Status::Ok) return 111;
    if (!render_if_needed() || !mutation.closed || !mutation.recreated ||
        mutation.mutator_draws != 1U || mutation.survivor_draws != 1U ||
        mutation.replacement_draws != 0U || mutation.replacement == mutator ||
        query(mutator, &info) != Status::NotFound ||
        damage_snapshot(&damage) != Status::Ok || !damage.full) return 112;
    if (!render_if_needed() || mutation.survivor_draws != 2U ||
        mutation.replacement_draws != 1U || render_if_needed()) return 113;
    // Display changes preserve identities, surfaces and restore state.
    if (initialize(1024U, 768U) != Status::Ok) return 114;
    WindowId normal = INVALID_WINDOW, large = INVALID_WINDOW;
    if (create_window("Normal", 50U, {600, 400, 300, 220},
            draw, receive, nullptr, &normal) != Status::Ok ||
        create_window("Large", 51U, {100, 100, 400, 300},
            draw, receive, nullptr, &large) != Status::Ok) return 115;
    uint8_t kept_payload[16]{};
    kept_payload[0] = 71;
    if (present_surface(normal, 4, 4, 4, kept_payload, sizeof(kept_payload)) != Status::Ok ||
        maximize(large) != Status::Ok || minimize(large) != Status::Ok) return 116;
    const WindowId kept_focus = focused_window();
    if (set_screen_size(640, 480) != Status::Ok || window_count() != 2 ||
        focused_window() != kept_focus || query(normal, &info) != Status::Ok) return 117;
    const auto smaller = workspace_geometry().work_area;
    if (info.bounds.x < smaller.x || info.bounds.y < smaller.y ||
        info.bounds.x + info.bounds.width > smaller.x + smaller.width ||
        info.bounds.y + info.bounds.height > smaller.y + smaller.height) return 118;
    SurfaceView kept{};
    if (read_surface(normal, &kept) != Status::Ok || kept.size != 16 ||
        kept.data[0] != 71 || query(large, &info) != Status::Ok ||
        info.state != WindowState::Minimized) return 119;
    if (restore(large) != Status::Ok || query(large, &info) != Status::Ok ||
        info.state != WindowState::Maximized || info.bounds.width != smaller.width ||
        info.bounds.height != smaller.height) return 120;
    if (restore(large) != Status::Ok || query(large, &info) != Status::Ok ||
        info.state != WindowState::Normal || info.bounds.width != 400) return 121;
    if (set_screen_size(0, 480) != Status::InvalidArgument ||
        workspace_geometry().work_area.width != smaller.width) return 122;
    if (set_screen_size(1280, 720) != Status::Ok ||
        damage_snapshot(&damage) != Status::Ok || !damage.full) return 123;
    if (!render_if_needed() || set_screen_size(1280, 720) != Status::Ok ||
        render_if_needed()) return 124;
    if (set_screen_size(320, 200) != Status::Ok || query(normal, &info) != Status::Ok ||
        info.bounds.height > workspace_geometry().work_area.height) return 125;
    if (set_screen_size(UINT32_MAX, 480) != Status::InvalidArgument) return 127;
    if (initialize(800, 600) != Status::Ok) return 128;
    RedrawFixture nested_resize{};
    nested_resize.mode = 3;
    WindowId resize_callback = INVALID_WINDOW;
    if (create_window("Resize callback", 52U, {40, 50, 300, 220},
        invalidate_during_draw, nullptr, &nested_resize, &resize_callback) != Status::Ok ||
        !render_if_needed() || nested_resize.resize_status != Status::InvalidState ||
        workspace_geometry().work_area.width != 754) return 129;

    // A window-list callback may destroy its own window and reuse its slot.
    // Listing must still enumerate survivors exactly once with metadata from
    // before the callback changed focus/z-order.
    if (initialize(800U, 600U) != Status::Ok) return 130;
    WindowListFixture listing{};
    if (create_window("Enumerated A", 62U, {60, 70, 300, 220},
            draw, receive, nullptr, &listing.first) != Status::Ok ||
        create_window("Enumerated B", 63U, {90, 100, 300, 220},
            draw, receive, nullptr, &listing.second) != Status::Ok ||
        list(nullptr, nullptr) != Status::InvalidArgument) return 131;
    if (list(list_during_mutation, &listing) != Status::Ok ||
        listing.count != 2U || !listing.changed ||
        !listing.retained_first_metadata ||
        listing.seen[0] != listing.first ||
        listing.seen[1] != listing.second ||
        listing.positions[0] != 0U || listing.positions[1] != 1U ||
        listing.focused[0] || !listing.focused[1] ||
        listing.replacement == listing.first ||
        query(listing.first, &info) != Status::NotFound ||
        query(listing.replacement, &info) != Status::Ok) return 132;
    listing.count = 0U;
    listing.mutate = false;
    if (list(list_during_mutation, &listing) != Status::Ok ||
        listing.count != 2U || listing.seen[0] != listing.second ||
        listing.seen[1] != listing.replacement) return 133;
    size_t stopped = 0U;
    if (list(stop_during_list, &stopped) != Status::IterationStopped ||
        stopped != 1U) return 134;

    // Flux 8 pointer ownership: hover and wheel follow client hit testing,
    // keyboard remains with focus and left-button capture survives leaving
    // a window without sending a release to the unrelated focused app.
    if (initialize(800U, 600U) != Status::Ok) return 135;
    PointerRoutingProbe pointer_a{}, pointer_b{};
    WindowId mouse_a = INVALID_WINDOW, mouse_b = INVALID_WINDOW;
    if (create_window("Pointer A", 201U, {40, 55, 300, 220},
            draw, receive_pointer_route, &pointer_a, &mouse_a) != Status::Ok ||
        create_window("Pointer B", 202U, {200, 140, 300, 220},
            draw, receive_pointer_route, &pointer_b, &mouse_b) != Status::Ok ||
        focused_window() != mouse_b) return 136;
    event = {};
    event.type = input::EventType::MouseMove;
    event.x = 80; event.y = 210;  // A content, B remains focused.
    if (dispatch(event) != Status::Ok || pointer_a.moves != 1U ||
        pointer_b.moves != 0U || focused_window() != mouse_b) return 137;
    event.type = input::EventType::MouseWheel;
    event.wheel = 1;
    if (dispatch(event) != Status::Ok || pointer_a.wheels != 1U ||
        pointer_b.wheels != 0U) return 138;
    event = {};
    event.type = input::EventType::KeyDown;
    event.key = drivers::keyboard::KeyCode::Tab;
    if (dispatch(event) != Status::Ok || pointer_b.keys != 1U ||
        pointer_a.keys != 0U) return 139;
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = 80; event.y = 210;
    if (dispatch(event) != Status::Ok || pointer_a.downs != 1U ||
        pointer_b.downs != 0U || focused_window() != mouse_a) return 140;
    event.type = input::EventType::MouseMove;
    event.x = 780; event.y = 570;
    if (dispatch(event) != Status::Ok || pointer_a.moves != 2U ||
        pointer_b.moves != 0U) return 141;
    event.type = input::EventType::MouseButtonUp;
    event.buttons = 0U;
    if (dispatch(event) != Status::Ok || pointer_a.ups != 1U ||
        pointer_b.ups != 0U) return 142;
    event = {};
    event.type = input::EventType::MouseMove;
    event.x = 780; event.y = 570;
    if (dispatch(event) != Status::Ok || pointer_a.moves != 2U) return 143;
    // Chrome header starts WM drag and cannot generate app click/release.
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = 370; event.y = 153;  // B-only header, outside raised A.
    if (dispatch(event) != Status::Ok || pointer_b.downs != 0U) return 144;
    event.type = input::EventType::MouseMove;
    event.x = 780; event.y = 570;
    if (dispatch(event) != Status::Ok || pointer_b.moves != 0U) return 145;
    event.type = input::EventType::MouseButtonUp;
    event.buttons = 0U;
    if (dispatch(event) != Status::Ok || pointer_b.ups != 0U) return 146;
    // Click the exposed client, then close it while captured: the subsequent
    // release cannot be delivered to a reused slot or underlying window.
    if (query(mouse_b, &info) != Status::Ok) return 147;
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = info.bounds.x + 40; event.y = info.bounds.y + 100;
    if (dispatch(event) != Status::Ok || pointer_b.downs != 1U) return 148;
    if (close(mouse_b) != Status::Ok) return 149;
    event.type = input::EventType::MouseButtonUp;
    event.x = 80; event.y = 210;  // A content; stale B release must not leak.
    event.buttons = 0U;
    if (dispatch(event) != Status::Ok || pointer_b.ups != 0U ||
        pointer_a.ups != 1U) return 150;
    if (initialize(800U, 600U) != Status::Ok) return 151;
    PointerRoutingProbe self_closing{};
    self_closing.close_on_down = true;
    WindowId self_id = INVALID_WINDOW;
    if (create_window("Self-close pointer", 203U, {50, 60, 300, 220},
            draw, receive_pointer_route, &self_closing, &self_id) != Status::Ok) return 152;
    event = {};
    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    event.x = 90; event.y = 160;
    if (dispatch(event) != Status::Ok || self_closing.downs != 1U ||
        query(self_id, &info) != Status::NotFound) return 153;
    event.type = input::EventType::MouseButtonUp;
    event.buttons = 0U;
    if (dispatch(event) != Status::Ok || self_closing.ups != 0U) return 154;

    return 0;
}
