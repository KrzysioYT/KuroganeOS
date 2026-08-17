#include "../kernel/ipc/event.hpp"

#include <cassert>

int main() {
    using namespace ipc::event;

    assert(initialize() == Status::Ok);
    assert(initialize() == Status::AlreadyInitialized);

    Handle automatic = INVALID_HANDLE;
    assert(create(100U, ResetMode::Auto, false, &automatic) == Status::Ok);
    assert(automatic != INVALID_HANDLE);
    assert(poll(100U, automatic) == Status::WouldBlock);
    assert(signal(100U, automatic) == Status::Ok);
    assert(poll(100U, automatic) == Status::Ok);
    assert(poll(100U, automatic) == Status::WouldBlock);

    assert(grant(100U, automatic, 200U) == Status::Ok);
    assert(grant(100U, automatic, 200U) == Status::AlreadyGranted);
    assert(signal(200U, automatic) == Status::Ok);
    assert(poll(200U, automatic) == Status::Ok);
    assert(poll(300U, automatic) == Status::AccessDenied);

    Handle manual = INVALID_HANDLE;
    assert(create(400U, ResetMode::Manual, true, &manual) == Status::Ok);
    assert(grant(400U, manual, 500U) == Status::Ok);
    assert(poll(400U, manual) == Status::Ok);
    assert(poll(500U, manual) == Status::Ok);
    assert(reset(500U, manual) == Status::Ok);
    assert(poll(400U, manual) == Status::WouldBlock);
    assert(signal(400U, manual) == Status::Ok);
    assert(poll(500U, manual) == Status::Ok);

    assert(close(400U, manual) == Status::Ok);
    assert(poll(400U, manual) == Status::AccessDenied);
    assert(poll(500U, manual) == Status::Ok);
    assert(close(500U, manual) == Status::Ok);
    assert(poll(500U, manual) == Status::StaleHandle);

    Handle cleanup = INVALID_HANDLE;
    assert(create(600U, ResetMode::Manual, false, &cleanup) == Status::Ok);
    assert(grant(600U, cleanup, 700U) == Status::Ok);
    release_process(600U);
    assert(signal(600U, cleanup) == Status::AccessDenied);
    assert(signal(700U, cleanup) == Status::Ok);
    release_process(700U);
    assert(poll(700U, cleanup) == Status::StaleHandle);

    return 0;
}
