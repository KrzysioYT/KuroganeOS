from pathlib import Path


def replace_once(path, old, new):
    file_path = Path(path)
    text = file_path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{path}: anchor count={count}: {old[:100]!r}')
    file_path.write_text(text.replace(old, new, 1))


replace_once(
    'kernel/net/socket.cpp',
    '''bool address_zero(const IPv4Address& address) {
    for (size_t index = 0U; index < IPV4_ADDRESS_LENGTH; ++index) {
        if (address.bytes[index] != 0U) return false;
    }
    return true;
}
''',
    '''bool address_zero(const IPv4Address& address) {
    for (size_t index = 0U; index < IPV4_ADDRESS_LENGTH; ++index) {
        if (address.bytes[index] != 0U) return false;
    }
    return true;
}

bool address_loopback(const IPv4Address& address) {
    return address.bytes[0] == 127U;
}
''')

replace_once(
    'kernel/net/socket.cpp',
    '''bool datagram_matches(const Slot& slot, const UdpDatagram& datagram) {
    if (!slot.active || !slot.bound || slot.protocol != Protocol::Udp ||
        slot.local.port != datagram.destination_port) {
        return false;
    }
    if (!address_zero(slot.local.address) &&
        !address_equal(slot.local.address, datagram.destination)) {
        return false;
    }
    return !slot.connected ||
        (slot.remote.port == datagram.source_port &&
         address_equal(slot.remote.address, datagram.source));
}

bool enqueue(Slot& slot, const UdpDatagram& datagram) {
    if (slot.rx_count >= MAX_RX_DATAGRAMS ||
        datagram.payload_length > UDP_MAX_PAYLOAD) {
        return false;
    }
    QueuedDatagram& queued = slot.rx[slot.rx_head];
    queued = {};
    queued.source = {datagram.source, datagram.source_port};
    queued.size = datagram.payload_length;
    for (size_t index = 0U; index < queued.size; ++index) {
        queued.payload[index] = datagram.payload[index];
    }
    slot.rx_head = (slot.rx_head + 1U) % MAX_RX_DATAGRAMS;
    ++slot.rx_count;
    return true;
}
''',
    '''bool slot_accepts_datagram(
    const Slot& slot,
    const Endpoint& source,
    const Endpoint& destination) {
    if (!slot.active || !slot.bound || slot.protocol != Protocol::Udp ||
        slot.local.port != destination.port) {
        return false;
    }
    if (!address_zero(slot.local.address) &&
        !address_equal(slot.local.address, destination.address)) {
        return false;
    }
    return !slot.connected ||
        (slot.remote.port == source.port &&
         address_equal(slot.remote.address, source.address));
}

bool datagram_matches(const Slot& slot, const UdpDatagram& datagram) {
    const Endpoint source{datagram.source, datagram.source_port};
    const Endpoint destination{datagram.destination, datagram.destination_port};
    return slot_accepts_datagram(slot, source, destination);
}

bool enqueue_payload(
    Slot& slot,
    const Endpoint& source,
    const uint8_t* payload,
    size_t payload_length) {
    if (slot.rx_count >= MAX_RX_DATAGRAMS || payload == nullptr ||
        payload_length == 0U || payload_length > UDP_MAX_PAYLOAD) {
        return false;
    }
    QueuedDatagram& queued = slot.rx[slot.rx_head];
    queued = {};
    queued.source = source;
    queued.size = payload_length;
    for (size_t index = 0U; index < queued.size; ++index) {
        queued.payload[index] = payload[index];
    }
    slot.rx_head = (slot.rx_head + 1U) % MAX_RX_DATAGRAMS;
    ++slot.rx_count;
    return true;
}

bool enqueue(Slot& slot, const UdpDatagram& datagram) {
    return enqueue_payload(
        slot,
        {datagram.source, datagram.source_port},
        datagram.payload,
        datagram.payload_length);
}

Status send_loopback(const Slot& sender, const uint8_t* payload, size_t size) {
    static constexpr IPv4Address canonical_loopback{{127U, 0U, 0U, 1U}};
    const Endpoint source{
        address_zero(sender.local.address) ? canonical_loopback : sender.local.address,
        sender.local.port};
    const Endpoint destination{sender.remote.address, sender.remote.port};

    for (Slot& candidate : g_slots) {
        if (!slot_accepts_datagram(candidate, source, destination)) continue;
        return enqueue_payload(candidate, source, payload, size)
            ? Status::Ok : Status::WouldBlock;
    }

    // UDP send to a local but currently unbound port succeeds synchronously;
    // the datagram is simply not queued to any socket. Never leak 127/8 to NIC.
    return Status::Ok;
}
''')

replace_once(
    'kernel/net/socket.cpp',
    '''    if (!slot->bound) return Status::NotBound;
    if (!slot->connected) return Status::NotConnected;
    return transport_status(g_backend.send_udp(
        g_backend.context,
        slot->remote.address,
        slot->local.port,
        slot->remote.port,
        static_cast<const uint8_t*>(data),
        size));
''',
    '''    if (!slot->bound) return Status::NotBound;
    if (!slot->connected) return Status::NotConnected;
    if (address_loopback(slot->remote.address)) {
        return send_loopback(*slot, static_cast<const uint8_t*>(data), size);
    }
    return transport_status(g_backend.send_udp(
        g_backend.context,
        slot->remote.address,
        slot->local.port,
        slot->remote.port,
        static_cast<const uint8_t*>(data),
        size));
''')

replace_once(
    'tests/test_socket.cpp',
    '''    uint8_t sent_payload[net::UDP_MAX_PAYLOAD]{};
    size_t sent_size = 0U;
};
''',
    '''    uint8_t sent_payload[net::UDP_MAX_PAYLOAD]{};
    size_t sent_size = 0U;
    size_t send_calls = 0U;
};
''')

replace_once(
    'tests/test_socket.cpp',
    '''    transport->sent_destination = destination;
    transport->sent_source_port = source_port;
''',
    '''    ++transport->send_calls;
    transport->sent_destination = destination;
    transport->sent_source_port = source_port;
''')

replace_once(
    'tests/test_socket.cpp',
    '''bool bytes_equal(const uint8_t* bytes, const char* text, size_t size) {
    for (size_t index = 0U; index < size; ++index) {
        if (bytes[index] != static_cast<uint8_t>(text[index])) return false;
    }
    return true;
}
''',
    '''bool bytes_equal(const uint8_t* bytes, const char* text, size_t size) {
    for (size_t index = 0U; index < size; ++index) {
        if (bytes[index] != static_cast<uint8_t>(text[index])) return false;
    }
    return true;
}

bool address_equal(const net::IPv4Address& left, const net::IPv4Address& right) {
    for (size_t index = 0U; index < net::IPV4_ADDRESS_LENGTH; ++index) {
        if (left.bytes[index] != right.bytes[index]) return false;
    }
    return true;
}
''')

replace_once(
    'tests/test_socket.cpp',
    '''    CHECK(transport.sent_size == sizeof(request) - 1U);
    CHECK(bytes_equal(transport.sent_payload, request, transport.sent_size));
''',
    '''    CHECK(transport.sent_size == sizeof(request) - 1U);
    CHECK(bytes_equal(transport.sent_payload, request, transport.sent_size));
    CHECK(transport.send_calls == 1U);
''')

replace_once(
    'tests/test_socket.cpp',
    '''    const Handle stale = socket;
''',
    '''    const net::IPv4Address loopback_ip{{127U, 0U, 0U, 1U}};
    const Endpoint self_endpoint{loopback_ip, UINT16_C(45200)};
    Handle loopback_self = INVALID_HANDLE;
    CHECK(create(owner, Type::Datagram, Protocol::Udp, &loopback_self) == Status::Ok);
    CHECK(bind(owner, loopback_self, self_endpoint) == Status::Ok);
    CHECK(connect(owner, loopback_self, self_endpoint) == Status::Ok);
    constexpr char loopback_payload[] = "kurogane-loopback";
    const size_t physical_sends_before_loopback = transport.send_calls;
    CHECK(send(
        owner, loopback_self, loopback_payload, sizeof(loopback_payload) - 1U) == Status::Ok);
    CHECK(transport.send_calls == physical_sends_before_loopback);
    received = 0U;
    CHECK(receive(
        owner, loopback_self, buffer, sizeof(buffer), &received, &source) == Status::Ok);
    CHECK(received == sizeof(loopback_payload) - 1U);
    CHECK(bytes_equal(buffer, loopback_payload, received));
    CHECK(source.port == self_endpoint.port);
    CHECK(address_equal(source.address, loopback_ip));
    CHECK(close(owner, loopback_self) == Status::Ok);

    const Endpoint process_endpoint{loopback_ip, UINT16_C(45201)};
    Handle loopback_receiver = INVALID_HANDLE;
    Handle loopback_sender = INVALID_HANDLE;
    CHECK(create(other, Type::Datagram, Protocol::Udp, &loopback_receiver) == Status::Ok);
    CHECK(bind(other, loopback_receiver, process_endpoint) == Status::Ok);
    CHECK(create(owner, Type::Datagram, Protocol::Udp, &loopback_sender) == Status::Ok);
    CHECK(connect(owner, loopback_sender, process_endpoint) == Status::Ok);
    constexpr char interprocess_payload[] = "pid-to-pid";
    CHECK(send(
        owner, loopback_sender, interprocess_payload,
        sizeof(interprocess_payload) - 1U) == Status::Ok);
    CHECK(transport.send_calls == physical_sends_before_loopback);
    received = 0U;
    CHECK(receive(
        other, loopback_receiver, buffer, sizeof(buffer), &received, &source) == Status::Ok);
    CHECK(received == sizeof(interprocess_payload) - 1U);
    CHECK(bytes_equal(buffer, interprocess_payload, received));
    CHECK(source.port >= EPHEMERAL_PORT_FIRST);
    CHECK(address_equal(source.address, loopback_ip));

    constexpr char queued_payload[] = "q";
    for (size_t index = 0U; index < MAX_RX_DATAGRAMS; ++index) {
        CHECK(send(
            owner, loopback_sender, queued_payload,
            sizeof(queued_payload) - 1U) == Status::Ok);
    }
    CHECK(send(
        owner, loopback_sender, queued_payload,
        sizeof(queued_payload) - 1U) == Status::WouldBlock);
    CHECK(transport.send_calls == physical_sends_before_loopback);
    for (size_t index = 0U; index < MAX_RX_DATAGRAMS; ++index) {
        received = 0U;
        CHECK(receive(
            other, loopback_receiver, buffer, sizeof(buffer),
            &received, nullptr) == Status::Ok);
        CHECK(received == sizeof(queued_payload) - 1U);
    }

    CHECK(close(other, loopback_receiver) == Status::Ok);
    const Endpoint unused_loopback{loopback_ip, UINT16_C(45999)};
    CHECK(connect(owner, loopback_sender, unused_loopback) == Status::Ok);
    CHECK(send(
        owner, loopback_sender, queued_payload,
        sizeof(queued_payload) - 1U) == Status::Ok);
    CHECK(transport.send_calls == physical_sends_before_loopback);
    CHECK(close(owner, loopback_sender) == Status::Ok);

    const Handle stale = socket;
''')

print('applied local UDP loopback delivery and host regressions')
