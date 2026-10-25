#include "../kernel/net/protocols.hpp"

#include <cstdio>

namespace {

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("check failed at line %d: %s\n", __LINE__, #condition); \
            return 1;                                                           \
        }                                                                       \
    } while (false)

void write16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 8U);
    bytes[1] = static_cast<uint8_t>(value);
}

} // namespace

int main() {
    constexpr uint16_t transaction = UINT16_C(0x4b55);
    net::DnsAnswer answer{};

    uint8_t nxdomain[12]{};
    write16(nxdomain, transaction);
    write16(nxdomain + 2U, UINT16_C(0x8183));
    write16(nxdomain + 4U, 1U);
    CHECK(net::parse_dns_a_response(
        nxdomain, sizeof(nxdomain), transaction, &answer) ==
        net::Status::NameNotFound);

    uint8_t server_failure[12]{};
    write16(server_failure, transaction);
    write16(server_failure + 2U, UINT16_C(0x8182));
    write16(server_failure + 4U, 1U);
    CHECK(net::parse_dns_a_response(
        server_failure, sizeof(server_failure), transaction, &answer) ==
        net::Status::InterfaceError);

    uint8_t no_answers[12]{};
    write16(no_answers, transaction);
    write16(no_answers + 2U, UINT16_C(0x8180));
    write16(no_answers + 4U, 1U);
    CHECK(net::parse_dns_a_response(
        no_answers, sizeof(no_answers), transaction, &answer) ==
        net::Status::WouldBlock);

    uint8_t not_response[12]{};
    write16(not_response, transaction);
    write16(not_response + 2U, UINT16_C(0x0100));
    write16(not_response + 4U, 1U);
    CHECK(net::parse_dns_a_response(
        not_response, sizeof(not_response), transaction, &answer) ==
        net::Status::MalformedPacket);

    std::puts("DNS protocol response-code tests passed");
    return 0;
}
