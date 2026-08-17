#include "../kernel/ipc/shared_memory.hpp"
#include "../kernel/memory/physical_memory.hpp"

#include <cassert>
#include <cstdint>

namespace {

alignas(4096) uint8_t g_arena[4096U * 64U]{};

} // namespace

int main() {
    memory::init_physical_memory(
        reinterpret_cast<uintptr_t>(g_arena),
        sizeof(g_arena),
        ipc::shared_memory::PAGE_SIZE);
    assert(memory::physical_memory_initialized());

    using namespace ipc::shared_memory;
    assert(initialize() == Status::Ok);
    assert(initialize() == Status::AlreadyInitialized);

    const size_t free_before = memory::free_frames();
    Handle object = INVALID_HANDLE;
    assert(create(100U, 5000U, &object) == Status::Ok);
    assert(object != INVALID_HANDLE);
    assert(memory::free_frames() + 2U == free_before);

    View owner_view{};
    assert(acquire(100U, object, &owner_view) == Status::Ok);
    assert(owner_view.page_count == 2U);
    assert(owner_view.size == 5000U);
    for (size_t page = 0U; page < owner_view.page_count; ++page) {
        auto* bytes = static_cast<uint8_t*>(owner_view.frames[page]);
        for (size_t index = 0U; index < PAGE_SIZE; ++index) assert(bytes[index] == 0U);
    }
    static_cast<uint8_t*>(owner_view.frames[0])[17] = 0x5AU;

    assert(grant(100U, object, 200U) == Status::Ok);
    assert(grant(100U, object, 200U) == Status::AlreadyGranted);
    assert(grant(200U, object, 300U) == Status::AccessDenied);

    View peer_view{};
    assert(acquire(200U, object, &peer_view) == Status::Ok);
    assert(peer_view.frames[0] == owner_view.frames[0]);
    assert(static_cast<uint8_t*>(peer_view.frames[0])[17] == 0x5AU);
    assert(acquire(300U, object, &peer_view) == Status::AccessDenied);

    assert(close(100U, object) == Status::Ok);
    assert(acquire(100U, object, &owner_view) == Status::AccessDenied);
    assert(release(100U, object) == Status::Ok);
    assert(release(200U, object) == Status::Ok);
    assert(close(200U, object) == Status::Ok);
    assert(memory::free_frames() == free_before);
    assert(acquire(200U, object, &peer_view) == Status::StaleHandle);

    Handle second = INVALID_HANDLE;
    assert(create(400U, PAGE_SIZE, &second) == Status::Ok);
    assert(second != object);
    assert(grant(400U, second, 500U) == Status::Ok);
    View second_owner{};
    View second_peer{};
    assert(acquire(400U, second, &second_owner) == Status::Ok);
    assert(acquire(500U, second, &second_peer) == Status::Ok);
    release_process(400U);
    assert(acquire(400U, second, &second_owner) == Status::AccessDenied);
    assert(static_cast<uint8_t*>(second_peer.frames[0]) != nullptr);
    assert(release(500U, second) == Status::Ok);
    assert(close(500U, second) == Status::Ok);
    assert(memory::free_frames() == free_before);

    return 0;
}
