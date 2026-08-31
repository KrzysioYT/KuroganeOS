#include "../kernel/ui/window_manager.hpp"

namespace {
size_t g_draws = 0U;
size_t g_inputs = 0U;

void draw(windowing::WindowId, const ui::Rect&, bool, void*) { ++g_draws; }
void receive(windowing::WindowId, const input::Event&, void*) { ++g_inputs; }
} // namespace

int main() {
    using namespace windowing;
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

    WindowInfo info{};
    if (query(second, &info) != Status::Ok || !info.focused ||
        info.owner_pid != 11U || info.z_order != 1U) return 4;
    if (focus(first) != Status::Ok || focused_window() != first ||
        query(first, &info) != Status::Ok || info.z_order != 1U) return 5;

    ChromeGeometry chrome{};
    if (chrome_geometry(first, &chrome) != Status::Ok ||
        chrome.header.height <= 30 || chrome.resize_grip.width <= 0 ||
        chrome.minimize_control.x >= chrome.expand_control.x ||
        chrome.expand_control.x >= chrome.dismiss_control.x) return 6;

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
    if (dispatch(event) != Status::Ok || g_inputs == 0U) return 16;

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

    // Alt+F4 remains a stable keyboard path regardless of Flux chrome.
    event = {};
    event.type = input::EventType::KeyDown;
    event.alt = true;
    event.key = drivers::keyboard::KeyCode::F4;
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
    return 0;
}
