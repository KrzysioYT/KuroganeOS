#pragma once

#include "network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::tcp_client {

constexpr size_t MAX_SEGMENT_PAYLOAD = 1400U;
constexpr size_t PENDING_RECEIVE_CAPACITY = TRANSPORT_INBOX_CAPACITY;

struct Client {
    NetworkStack* stack;
    IPv4Address peer;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t send_next;
    uint32_t receive_next;
    uint8_t pending[PENDING_RECEIVE_CAPACITY];
    size_t pending_offset;
    size_t pending_length;
    bool connected;
    bool peer_closed;
};

void initialize(Client* client);

Status connect(
    Client* client,
    NetworkStack* stack,
    const IPv4Address& peer,
    uint16_t local_port,
    uint16_t remote_port,
    uint32_t initial_sequence,
    uint64_t timeout_ms);

/*
 * Sends the entire buffer in bounded TCP payload chunks. The call sleeps with
 * HLT between polls when PIT interrupts are available; it never hot-spins.
 */
Status send(
    Client* client,
    const uint8_t* data,
    size_t length,
    uint64_t timeout_ms);

/*
 * Returns at least one byte when data is available, up to output_capacity.
 * Unconsumed bytes from a received segment remain buffered in the Client.
 */
Status receive(
    Client* client,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length,
    uint64_t timeout_ms);

Status close(Client* client);

} // namespace net::tcp_client
