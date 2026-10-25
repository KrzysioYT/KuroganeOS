#pragma once

#include "network.hpp"
#include "protocols.hpp"
#include "tcp_client.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::socket {

using ProcessId = uint64_t;
using Handle = uint64_t;

constexpr Handle INVALID_HANDLE = 0U;
constexpr size_t MAX_SOCKETS = 32U;
constexpr size_t MAX_RX_DATAGRAMS = 4U;
constexpr size_t MAX_TCP_SESSIONS = 4U;
constexpr uint16_t EPHEMERAL_PORT_FIRST = UINT16_C(49152);
constexpr uint64_t TCP_CONNECT_TIMEOUT_MS = UINT64_C(7000);
constexpr uint64_t TCP_SYN_RETRY_MS = UINT64_C(1750);
constexpr uint8_t TCP_CONNECT_TRANSMISSION_LIMIT = 4U;

enum class Type : uint8_t {
    Datagram = 1U,
    Stream = 2U,
};

enum class Protocol : uint8_t {
    Udp = 17U,
    Tcp = 6U,
};

enum ReadyFlags : uint32_t {
    ReadyNone = 0U,
    ReadyRead = UINT32_C(1) << 0,
    ReadyWrite = UINT32_C(1) << 1,
    ReadyConnected = UINT32_C(1) << 2,
    ReadyHangup = UINT32_C(1) << 3,
    ReadyError = UINT32_C(1) << 4,
    ReadyAll = ReadyRead | ReadyWrite | ReadyConnected | ReadyHangup | ReadyError,
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
    ConnectionRefused,
    ConnectionReset,
    TimedOut,
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
    uint64_t (*monotonic_ms)(void* context);
    net::Status (*tcp_begin_connect)(
        void* context,
        tcp_client::Client* client,
        const IPv4Address& destination,
        uint16_t source_port,
        uint16_t destination_port,
        uint32_t initial_sequence);
    net::Status (*tcp_progress)(void* context, tcp_client::Client* client);
    net::Status (*tcp_try_send)(
        void* context,
        tcp_client::Client* client,
        const uint8_t* data,
        size_t length,
        size_t* out_sent);
    net::Status (*tcp_try_receive)(
        void* context,
        tcp_client::Client* client,
        uint8_t* output,
        size_t output_capacity,
        size_t* out_length);
    net::Status (*tcp_begin_close)(void* context, tcp_client::Client* client);
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
    size_t size,
    size_t* out_sent = nullptr);
Status receive(
    ProcessId owner,
    Handle handle,
    void* output,
    size_t capacity,
    size_t* out_size,
    Endpoint* out_source = nullptr);
Status close(ProcessId owner, Handle handle);
Status readiness(
    ProcessId owner,
    Handle handle,
    uint32_t requested,
    uint32_t* out_ready);
void release_process(ProcessId owner);
Status pump(size_t budget, size_t* out_routed = nullptr);
size_t active_count(ProcessId owner = 0U);
const char* status_message(Status status);

} // namespace net::socket
