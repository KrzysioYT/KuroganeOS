#include "console.hpp"

extern "C" bool kurogane_start_desktop_session() __attribute__((weak));

namespace user::console {
namespace {

constexpr size_t kMask = INPUT_CAPACITY - 1U;
static_assert((INPUT_CAPACITY & kMask) == 0U,
              "userspace console queue must be a power of two");

char g_characters[INPUT_CAPACITY]{};
uint16_t g_head = 0U;
uint16_t g_tail = 0U;
uint64_t g_dropped = 0U;
bool g_active = false;

} // namespace

void initialize() {
    g_head = 0U;
    g_tail = 0U;
    g_dropped = 0U;
    g_active = true;

    // KuroganeOS 2.3: normal userspace boot owns a graphical session.
    // The weak hook keeps hosted console tests independent from the desktop
    // runtime while the kernel build resolves it from desktop_session.cpp.
    if (kurogane_start_desktop_session != nullptr) {
        static_cast<void>(kurogane_start_desktop_session());
    }
}

void shutdown() {
    g_active = false;
    g_head = 0U;
    g_tail = 0U;
}

bool active() { return g_active; }

bool push(char character) {
    if (!g_active || character == 0) return false;
    const uint16_t head = g_head;
    if (static_cast<uint16_t>(head - g_tail) >= INPUT_CAPACITY) {
        ++g_dropped;
        return false;
    }
    g_characters[head & kMask] = character;
    g_head = static_cast<uint16_t>(head + 1U);
    return true;
}

bool try_read(char* character) {
    if (!g_active || character == nullptr || g_tail == g_head) return false;
    *character = g_characters[g_tail & kMask];
    g_tail = static_cast<uint16_t>(g_tail + 1U);
    return true;
}

size_t pending() {
    return g_active
        ? static_cast<size_t>(static_cast<uint16_t>(g_head - g_tail))
        : 0U;
}

uint64_t dropped() { return g_dropped; }

} // namespace user::console
