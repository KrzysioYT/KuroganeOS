#pragma once

#include "network.hpp"
#include "protocols.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::socket {

using ProcessId = uint64_t;
using Handle = uint64_t;

constexpr Handle INVALID_HANDLE = 0U;
constexpr size_t MAX_SOCKETS = 32U;
constexpr size_t MAX_RX_DATAGRAMS = 4U;
constexpr uint16_t EPHEMERAL_PORT_FIRST = UINT16_C(49152);

enum class Type : uint8_t {
    Datagram = 1U,
    Stream = 2U,
};

enum class Protocol : uint8_t {
    Udp = 17U,
    Tcp = 6U,
};

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    InvalidArgument,
    NotSupported,
    CapacityReached,
    AccessDenied,
    StaleHandle,
    AddressInUse,
    NotBound,
    NotConnected,
    WouldBlock,
    BufferTooSmall,
    PayloadTooLarge,
    TransportError,
};

struct Endpoint {
    IPv4Address address;
    uint16_t port;
};

struct Backend {
    void* context;
    net::Status (*send_udp)(
        void* context,
        const IPv4Address& destination,
        uint16_t source_port,
        uint16_t destination_port,
        const uint8_t* payload,
        size_t payload_length);
    net::Status (*poll)(void* context, size_t budget, size_t* processed);
    net::Status (*take_udp)(void* context, UdpDatagram* datagram);
};

Status initialize(const Backend& backend);
bool initialized();
Status create(
    ProcessId owner,
    Type type,
    Protocol protocol,
    Handle* output);
Status bind(ProcessId owner, Handle handle, const Endpoint& endpoint);
Status connect(ProcessId owner, Handle handle, const Endpoint& endpoint);
Status send(
    ProcessId owner,
    Handle handle,
    const void* data,
    size_t size);
Status receive(
    ProcessId owner,
    Handle handle,
    void* output,
    size_t capacity,
    size_t* out_size,
    Endpoint* out_source = nullptr);
Status close(ProcessId owner, Handle handle);
void release_process(ProcessId owner);
Status pump(size_t budget, size_t* out_routed = nullptr);
size_t active_count(ProcessId owner = 0U);
const char* status_message(Status status);

} // namespace net::socket
