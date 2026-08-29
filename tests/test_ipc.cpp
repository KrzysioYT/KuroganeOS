#include "../kernel/ipc/channel.hpp"

#include <cstdio>

namespace {

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("check failed at line %d: %s\n", __LINE__, #condition); \
            return 1;                                                           \
        }                                                                       \
    } while (false)

bool bytes_equal(const uint8_t* left, const char* right, size_t size) {
    for (size_t index = 0U; index < size; ++index) {
        if (left[index] != static_cast<uint8_t>(right[index])) return false;
    }
    return true;
}

} // namespace

int main() {
    using ipc::Handle;
    using ipc::Status;

    CHECK(ipc::initialize() == Status::Ok);
    CHECK(ipc::initialize() == Status::AlreadyInitialized);

    constexpr ipc::ProcessId server_pid = 100U;
    constexpr ipc::ProcessId client_pid = 200U;
    constexpr char service[] = "settings";

    Handle endpoint = ipc::INVALID_HANDLE;
    CHECK(ipc::bind(
        server_pid, service, sizeof(service) - 1U, &endpoint) == Status::Ok);
    CHECK(endpoint != ipc::INVALID_HANDLE);

    Handle duplicate = ipc::INVALID_HANDLE;
    CHECK(ipc::bind(
        server_pid, service, sizeof(service) - 1U, &duplicate) ==
        Status::AlreadyExists);

    Handle client = ipc::INVALID_HANDLE;
    CHECK(ipc::connect(
        client_pid, service, sizeof(service) - 1U, &client) == Status::Ok);
    CHECK(client != ipc::INVALID_HANDLE);

    Handle server = ipc::INVALID_HANDLE;
    CHECK(ipc::accept(server_pid, endpoint, &server) == Status::Ok);
    CHECK(server != ipc::INVALID_HANDLE);
    CHECK(ipc::accept(server_pid, endpoint, &duplicate) == Status::WouldBlock);

    constexpr char request[] = "ping";
    CHECK(ipc::send(client_pid, client, request, sizeof(request) - 1U) == Status::Ok);
    ipc::Message message{};
    CHECK(ipc::receive(server_pid, server, &message) == Status::Ok);
    CHECK(message.sender_pid == client_pid);
    CHECK(message.size == sizeof(request) - 1U);
    CHECK(bytes_equal(message.bytes, request, message.size));

    constexpr char response[] = "pong";
    CHECK(ipc::send(server_pid, server, response, sizeof(response) - 1U) == Status::Ok);
    CHECK(ipc::receive(client_pid, client, &message) == Status::Ok);
    CHECK(message.sender_pid == server_pid);
    CHECK(message.size == sizeof(response) - 1U);
    CHECK(bytes_equal(message.bytes, response, message.size));

    CHECK(ipc::send(server_pid + 1U, server, response, 1U) == Status::AccessDenied);
    CHECK(ipc::receive(client_pid + 1U, client, &message) == Status::AccessDenied);

    for (size_t index = 0U; index < ipc::MAX_MESSAGES_PER_DIRECTION; ++index) {
        CHECK(ipc::send(client_pid, client, request, 1U) == Status::Ok);
    }
    CHECK(ipc::send(client_pid, client, request, 1U) == Status::WouldBlock);
    for (size_t index = 0U; index < ipc::MAX_MESSAGES_PER_DIRECTION; ++index) {
        CHECK(ipc::receive(server_pid, server, &message) == Status::Ok);
    }
    CHECK(ipc::receive(server_pid, server, &message) == Status::WouldBlock);

    CHECK(ipc::close(server_pid, server) == Status::Ok);
    CHECK(ipc::send(client_pid, client, request, 1U) == Status::PeerClosed);
    CHECK(ipc::close(client_pid, client) == Status::Ok);
    CHECK(ipc::close(client_pid, client) == Status::StaleHandle);

    Handle second_client = ipc::INVALID_HANDLE;
    CHECK(ipc::connect(
        client_pid, service, sizeof(service) - 1U, &second_client) == Status::Ok);
    ipc::release_process(server_pid);
    CHECK(ipc::send(client_pid, second_client, request, 1U) == Status::PeerClosed);
    CHECK(ipc::accept(server_pid, endpoint, &server) == Status::StaleHandle);
    CHECK(ipc::close(client_pid, second_client) == Status::Ok);

    Handle rebound = ipc::INVALID_HANDLE;
    CHECK(ipc::bind(
        server_pid, service, sizeof(service) - 1U, &rebound) == Status::Ok);
    CHECK(rebound != endpoint);
    CHECK(ipc::close(server_pid, rebound) == Status::Ok);

    // Regression for the service-loop order used by Ring-3 daemons: the
    // server may poll accept before a client exists. A later client must stay
    // pending even if it queues data before the server accepts the channel.
    constexpr ipc::ProcessId deferred_server_pid = 300U;
    constexpr ipc::ProcessId deferred_client_pid = 400U;
    constexpr char deferred_service[] = "event-regression";
    Handle deferred_endpoint = ipc::INVALID_HANDLE;
    Handle deferred_server = ipc::INVALID_HANDLE;
    Handle deferred_client = ipc::INVALID_HANDLE;
    CHECK(ipc::bind(
        deferred_server_pid,
        deferred_service,
        sizeof(deferred_service) - 1U,
        &deferred_endpoint) == Status::Ok);
    CHECK(ipc::accept(
        deferred_server_pid,
        deferred_endpoint,
        &deferred_server) == Status::WouldBlock);
    CHECK(deferred_server == ipc::INVALID_HANDLE);
    CHECK(ipc::connect(
        deferred_client_pid,
        deferred_service,
        sizeof(deferred_service) - 1U,
        &deferred_client) == Status::Ok);
    CHECK(ipc::send(
        deferred_client_pid,
        deferred_client,
        request,
        sizeof(request) - 1U) == Status::Ok);
    CHECK(ipc::accept(
        deferred_server_pid,
        deferred_endpoint,
        &deferred_server) == Status::Ok);
    CHECK(deferred_server != ipc::INVALID_HANDLE);
    message = {};
    CHECK(ipc::receive(
        deferred_server_pid,
        deferred_server,
        &message) == Status::Ok);
    CHECK(message.sender_pid == deferred_client_pid);
    CHECK(message.size == sizeof(request) - 1U);
    CHECK(bytes_equal(message.bytes, request, message.size));
    CHECK(ipc::close(deferred_server_pid, deferred_server) == Status::Ok);
    CHECK(ipc::close(deferred_client_pid, deferred_client) == Status::Ok);
    CHECK(ipc::close(deferred_server_pid, deferred_endpoint) == Status::Ok);

    Handle invalid = ipc::INVALID_HANDLE;
    constexpr char bad_name[] = "bad/name";
    CHECK(ipc::bind(
        server_pid, bad_name, sizeof(bad_name) - 1U, &invalid) ==
        Status::InvalidName);

    std::puts("IPC channel core tests passed");
    return 0;
}
