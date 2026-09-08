#pragma once

#include <stddef.h>
#include <stdint.h>

namespace user::file_handle {

constexpr uint32_t FIRST_DESCRIPTOR = 3U;

enum class Resolution : uint8_t {
    Ok = 0,
    Invalid,
    Stale
};

template <typename Slot>
struct Result {
    Resolution resolution;
    Slot* slot;
};

/*
 * Decode the process-local descriptor namespace without collapsing malformed
 * input and a stale generation into the same result. Slot is deliberately a
 * small structural contract: runtime slots provide `active` and `generation`,
 * while host tests can exercise the resolver without constructing a VFS.
 */
template <typename Slot>
Result<Slot> resolve(
    Slot* slots,
    size_t slot_count,
    uint64_t handle) {
    const uint64_t encoded = handle & UINT64_C(0xFFFFFFFF);
    const uint32_t generation = static_cast<uint32_t>(handle >> 32U);
    if (slots == nullptr || slot_count == 0U ||
        encoded < FIRST_DESCRIPTOR ||
        encoded - FIRST_DESCRIPTOR >= slot_count) {
        return {Resolution::Invalid, nullptr};
    }
    Slot& slot = slots[static_cast<size_t>(encoded - FIRST_DESCRIPTOR)];
    if (!slot.active || slot.generation != generation) {
        return {Resolution::Stale, nullptr};
    }
    return {Resolution::Ok, &slot};
}

} // namespace user::file_handle
