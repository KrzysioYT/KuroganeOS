#include "ui_window_adapter.hpp"

namespace windowing {
namespace {

struct AdapterSlot {
    WindowId window;
    DrawCallback draw;
    InputCallback input;
    void* context;
    ui::Rect content;
    bool has_content;
    bool active;
};

AdapterSlot g_slots[MAX_WINDOWS]{};

void reap_closed_slots() {
    for (AdapterSlot& slot : g_slots) {
        if (!slot.active) continue;
        WindowInfo info{};
        if (query(slot.window, &info) != Status::Ok) slot = {};
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
    WindowId id,
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
    WindowId id,
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

Status create_ring3_window(
    const char* title,
    uint64_t owner_pid,
    const ui::Rect& bounds,
    DrawCallback draw,
    InputCallback input_callback,
    void* context,
    WindowId* out_id) {
    if (out_id == nullptr) return Status::InvalidArgument;
    AdapterSlot* slot = reserve_slot();
    if (slot == nullptr) return Status::CapacityReached;

    *slot = {};
    slot->draw = draw;
    slot->input = input_callback;
    slot->context = context;
    slot->active = true;

    WindowId window = INVALID_WINDOW;
    const Status status = windowing::create_window(
        title,
        owner_pid,
        bounds,
        draw_adapter,
        input_adapter,
        slot,
        &window);
    if (status != Status::Ok) {
        *slot = {};
        return status;
    }

    slot->window = window;
    *out_id = window;
    return Status::Ok;
}

} // namespace windowing
