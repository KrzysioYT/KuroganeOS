#include "ui_window_adapter.hpp"

namespace user::runtime::ui_adapter {
namespace {

struct AdapterSlot {
    windowing::WindowId window;
    windowing::DrawCallback draw;
    windowing::InputCallback input;
    void* context;
    ui::Rect content;
    bool has_content;
    bool active;
};

AdapterSlot g_slots[windowing::MAX_WINDOWS]{};

AdapterSlot* find_slot(windowing::WindowId window) {
    for (AdapterSlot& slot : g_slots) {
        if (slot.active && slot.window == window) return &slot;
    }
    return nullptr;
}

void reap_closed_slots() {
    for (AdapterSlot& slot : g_slots) {
        if (!slot.active) continue;
        windowing::WindowInfo info{};
        if (windowing::query(slot.window, &info) != windowing::Status::Ok) {
            slot = {};
        }
    }
}

AdapterSlot* reserve_slot() {
    reap_closed_slots();
    for (AdapterSlot& slot : g_slots) {
        if (!slot.active) return &slot;
    }
    return nullptr;
}

void draw_adapter(
    windowing::WindowId id,
    const ui::Rect& content,
    bool focused,
    void* opaque) {
    auto* slot = static_cast<AdapterSlot*>(opaque);
    if (slot == nullptr || !slot->active || slot->window != id) return;
    slot->content = content;
    slot->has_content = true;
    if (slot->draw != nullptr) slot->draw(id, content, focused, slot->context);
}

void input_adapter(
    windowing::WindowId id,
    const input::Event& event,
    void* opaque) {
    auto* slot = static_cast<AdapterSlot*>(opaque);
    if (slot == nullptr || !slot->active || slot->window != id ||
        slot->input == nullptr) return;

    input::Event local = event;
    if (slot->has_content &&
        (event.type == input::EventType::MouseMove ||
         event.type == input::EventType::MouseButtonDown ||
         event.type == input::EventType::MouseButtonUp)) {
        local.x -= slot->content.x;
        local.y -= slot->content.y;
    }
    slot->input(id, local, slot->context);
}

} // namespace

windowing::Status create_window(
    const char* title,
    uint64_t owner_pid,
    const ui::Rect& bounds,
    windowing::DrawCallback draw,
    windowing::InputCallback input_callback,
    void* context,
    windowing::WindowId* out_id) {
    if (out_id == nullptr) return windowing::Status::InvalidArgument;
    AdapterSlot* slot = reserve_slot();
    if (slot == nullptr) return windowing::Status::CapacityReached;

    *slot = {};
    slot->draw = draw;
    slot->input = input_callback;
    slot->context = context;
    slot->active = true;

    windowing::WindowId window = windowing::INVALID_WINDOW;
    const windowing::Status status = windowing::create_window(
        title,
        owner_pid,
        bounds,
        draw_adapter,
        input_adapter,
        slot,
        &window);
    if (status != windowing::Status::Ok) {
        *slot = {};
        return status;
    }

    slot->window = window;
    *out_id = window;
    return windowing::Status::Ok;
}

} // namespace user::runtime::ui_adapter
