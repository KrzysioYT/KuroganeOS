#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, got {count}: {old[:120]!r}")
    file.write_text(text.replace(old, new, 1))


def create_new(path: str, content: str) -> None:
    file = Path(path)
    if file.exists():
        raise SystemExit(f"{path}: file already exists")
    file.write_text(content)


# A syntactically valid DNS NXDOMAIN response is not a malformed packet.
replace_once(
    "kernel/net/network.hpp",
    "    NotForUs,\n    NoRoute,\n",
    "    NotForUs,\n    NameNotFound,\n    NoRoute,\n",
)

replace_once(
    "kernel/net/protocols.cpp",
    "    if ((flags & UINT16_C(0x8000)) == 0U ||\n"
    "        (flags & UINT16_C(0x000f)) != 0U) return Status::MalformedPacket;\n"
    "    const size_t questions = read_be16(packet + 4U);\n",
    "    if ((flags & UINT16_C(0x8000)) == 0U) return Status::MalformedPacket;\n"
    "    const uint16_t response_code = flags & UINT16_C(0x000f);\n"
    "    if (response_code == UINT16_C(3)) return Status::NameNotFound;\n"
    "    if (response_code != 0U) return Status::InterfaceError;\n"
    "    const size_t questions = read_be16(packet + 4U);\n",
)

replace_once(
    "kernel/user/runtime_base.inc",
    "        case net::Status::WouldBlock:\n"
    "        case net::Status::NeighborResolutionPending: return KU_STATUS_WOULD_BLOCK;\n"
    "        case net::Status::InvalidArgument:\n",
    "        case net::Status::WouldBlock:\n"
    "        case net::Status::NeighborResolutionPending: return KU_STATUS_WOULD_BLOCK;\n"
    "        case net::Status::NameNotFound: return KU_STATUS_NOT_FOUND;\n"
    "        case net::Status::InvalidArgument:\n",
)

# Pin the public DNS service ABI in the normal SDK regression.
replace_once(
    "tests/test_sdk_abi.cpp",
    "#include <kurogane/device.h>\n",
    "#include <kurogane/device.h>\n#include <kurogane/dns_service.h>\n",
)
replace_once(
    "tests/test_sdk_abi.cpp",
    "    static_assert(KU_IPC_SERVICE_NAME_CAPACITY == 32U);\n",
    "    static_assert(KU_DNS_SERVICE_NAME_SIZE == 7U);\n"
    "    static_assert(KU_DNS_SERVICE_HOST_CAPACITY == 64U);\n"
    "    static_assert(sizeof(ku_dns_service_request) == 88U);\n"
    "    static_assert(sizeof(ku_dns_service_response) == 24U);\n"
    "    static_assert(offsetof(ku_dns_service_request, host) == 24U);\n"
    "    static_assert(offsetof(ku_dns_service_response, address) == 16U);\n"
    "\n"
    "    static_assert(KU_IPC_SERVICE_NAME_CAPACITY == 32U);\n",
)

# Host regression for DNS response-code semantics.
create_new(
    "tests/test_dns_protocol.cpp",
    r'''#include "../kernel/net/protocols.hpp"

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
''',
)

replace_once(
    "scripts/run-host-tests.sh",
    '"$OUT_DIR/test_socket"\n\n',
    '"$OUT_DIR/test_socket"\n\n'
    '"$HOST_CXX" \\\n'
    '  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \\\n'
    '  tests/test_dns_protocol.cpp \\\n'
    '  kernel/net/protocols.cpp \\\n'
    '  kernel/net/network.cpp \\\n'
    '  -o "$OUT_DIR/test_dns_protocol"\n\n'
    '"$OUT_DIR/test_dns_protocol"\n\n',
)

# Extend the existing public DNS probe into a real dnsd.v1 service contract probe.
Path("userspace/system/dns-probe/main.c").write_text(r'''#include "../../runtime/user.h"

#include <kurogane/dns_service.h>
#include <kurogane/network.h>

#define DNS_PROBE_WAIT_ATTEMPTS 2000U
#define DNS_PROBE_CLEANUP_ROUNDS 12U

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static int nonzero_address(const uint8_t address[4]) {
    return address[0] != 0U || address[1] != 0U ||
        address[2] != 0U || address[3] != 0U;
}

static int zero_address(const uint8_t address[4]) {
    return !nonzero_address(address);
}

static int copy_host(char* destination, size_t capacity, const char* source) {
    size_t index = 0U;
    if (destination == (char*)0 || source == (const char*)0 || capacity == 0U) {
        return 0;
    }
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    if (source[index] != '\0') return 0;
    destination[index] = '\0';
    return 1;
}

static void fail_public(uint32_t code) {
    (void)u_puts("[TEST] public_dns_resolve: FAIL code=");
    (void)u_put_u64(code);
    (void)u_puts("\n");
    ku_exit((int32_t)code);
}

static void fail_service(uint32_t code) {
    (void)u_puts("[TEST] dns_service_roundtrip: FAIL code=");
    (void)u_put_u64(code);
    (void)u_puts("\n");
    ku_exit((int32_t)(40U + code));
}

static ku_result_t connect_dns_service(void) {
    uint32_t attempts;
    ku_result_t result = KU_STATUS_NOT_FOUND;
    for (attempts = 0U; attempts < DNS_PROBE_WAIT_ATTEMPTS; ++attempts) {
        result = ku_dns_service_connect();
        if (result > 0) return result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK) {
            return result;
        }
        (void)ku_sleep(1U);
    }
    return result;
}

static ku_status_t receive_response(
    ku_service_connection_t connection,
    ku_dns_service_response* response) {
    uint32_t attempts;
    if (response == (ku_dns_service_response*)0) return KU_STATUS_INVALID_ARGUMENT;
    for (attempts = 0U; attempts < DNS_PROBE_WAIT_ATTEMPTS; ++attempts) {
        ku_service_message message;
        ku_status_t status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_dns_service_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response) ||
            response->reserved != 0U) {
            return KU_STATUS_CORRUPT_DATA;
        }
        return KU_STATUS_OK;
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_status_t transact_request(
    ku_service_connection_t connection,
    const ku_dns_service_request* request,
    ku_dns_service_response* response) {
    ku_status_t status;
    if (request == (const ku_dns_service_request*)0 ||
        response == (ku_dns_service_response*)0) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) return status;
    clear_bytes(response, sizeof(*response));
    return receive_response(connection, response);
}

static void initialize_request(
    ku_dns_service_request* request,
    uint64_t request_id,
    uint32_t operation,
    const char* host) {
    clear_bytes(request, sizeof(*request));
    request->structure_size = sizeof(*request);
    request->operation = operation;
    request->request_id = request_id;
    request->flags = KU_DNS_SERVICE_FLAG_NONE;
    request->reserved = 0U;
    if (host != (const char*)0) {
        (void)copy_host(request->host, sizeof(request->host), host);
    }
}

__attribute__((noreturn)) void _start(void) {
    ku_network_status network = {0};
    ku_dns_a_request public_request = {0};
    ku_result_t connected;
    ku_service_connection_t connection;
    ku_dns_service_request request;
    ku_dns_service_response response;
    uint32_t attempts;
    uint32_t round;

    network.structure_size = sizeof(network);
    for (attempts = 0U; attempts < DNS_PROBE_WAIT_ATTEMPTS; ++attempts) {
        const ku_status_t status = ku_network_get_status(&network);
        if (status == KU_STATUS_OK && network.ready != 0U &&
            network.address[0] != 0U && nonzero_address(network.dns)) {
            break;
        }
        (void)ku_sleep(1U);
    }
    if (attempts == DNS_PROBE_WAIT_ATTEMPTS) fail_public(1U);

    public_request.structure_size = sizeof(public_request);
    public_request.flags = KU_DNS_FLAG_NONE;
    if (!copy_host(public_request.host, sizeof(public_request.host), "example.com")) {
        fail_public(2U);
    }
    if (ku_dns_resolve_a(&public_request) != KU_STATUS_OK ||
        !nonzero_address(public_request.address)) {
        fail_public(3U);
    }

    {
        ku_dns_a_request invalid = {0};
        invalid.structure_size = sizeof(invalid) - 1U;
        invalid.host[0] = 'x';
        invalid.host[1] = '\0';
        if ((ku_status_t)ku_syscall3(
                KU_SYS_DNS_RESOLVE_A,
                (uint64_t)(uintptr_t)&invalid,
                sizeof(invalid),
                0U) != KU_STATUS_VERSION_MISMATCH) {
            fail_public(4U);
        }
    }

    {
        ku_dns_a_request unknown = {0};
        unknown.structure_size = sizeof(unknown);
        if (!copy_host(
                unknown.host, sizeof(unknown.host),
                "kurogane-os-does-not-exist.invalid")) {
            fail_public(5U);
        }
        if (ku_dns_resolve_a(&unknown) != KU_STATUS_NOT_FOUND ||
            !zero_address(unknown.address)) {
            fail_public(6U);
        }
    }

    (void)u_puts("[TEST] public_dns_resolve: PASS\n");

    connected = connect_dns_service();
    if (connected <= 0) fail_service(1U);
    connection = (ku_service_connection_t)connected;

    initialize_request(&request, UINT64_C(1), KU_DNS_SERVICE_RESOLVE_A, "example.com");
    if (transact_request(connection, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OK || response.request_id != UINT64_C(1) ||
        !nonzero_address(response.address)) {
        fail_service(2U);
    }

    {
        const uint8_t malformed = UINT8_C(0xa5);
        if (ku_service_send(connection, &malformed, sizeof(malformed)) != KU_STATUS_OK) {
            fail_service(3U);
        }
        clear_bytes(&response, sizeof(response));
        if (receive_response(connection, &response) != KU_STATUS_OK ||
            response.status != KU_STATUS_CORRUPT_DATA ||
            response.request_id != 0U || !zero_address(response.address)) {
            fail_service(4U);
        }
    }

    initialize_request(
        &request, UINT64_C(2), UINT32_C(0xffffffff), "unsupported.invalid");
    if (transact_request(connection, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_NOT_SUPPORTED ||
        response.request_id != UINT64_C(2) || !zero_address(response.address)) {
        fail_service(5U);
    }

    initialize_request(&request, UINT64_C(3), KU_DNS_SERVICE_RESOLVE_A, (const char*)0);
    for (attempts = 0U; attempts < sizeof(request.host); ++attempts) {
        request.host[attempts] = 'x';
    }
    if (transact_request(connection, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_INVALID_ARGUMENT ||
        response.request_id != UINT64_C(3) || !zero_address(response.address)) {
        fail_service(6U);
    }

    initialize_request(
        &request,
        UINT64_C(4),
        KU_DNS_SERVICE_RESOLVE_A,
        "kurogane-os-does-not-exist.invalid");
    if (transact_request(connection, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_NOT_FOUND ||
        response.request_id != UINT64_C(4) || !zero_address(response.address)) {
        fail_service(7U);
    }

    if (ku_service_close(connection) != KU_STATUS_OK) fail_service(8U);
    (void)ku_sleep(2U);

    /* Exceed DNSD_MAX_CLIENTS over sequential lifetimes. If peer cleanup is
     * broken, later accepted connections are immediately rejected by dnsd. */
    for (round = 0U; round < DNS_PROBE_CLEANUP_ROUNDS; ++round) {
        connected = connect_dns_service();
        if (connected <= 0) fail_service(9U);
        connection = (ku_service_connection_t)connected;
        initialize_request(
            &request,
            UINT64_C(100) + round,
            UINT32_C(0xffffffff),
            "cleanup.invalid");
        if (transact_request(connection, &request, &response) != KU_STATUS_OK ||
            response.status != KU_STATUS_NOT_SUPPORTED ||
            response.request_id != UINT64_C(100) + round) {
            fail_service(10U);
        }
        if (ku_service_close(connection) != KU_STATUS_OK) fail_service(11U);
        (void)ku_sleep(2U);
    }

    connected = connect_dns_service();
    if (connected <= 0) fail_service(12U);
    if (ku_service_close((ku_service_connection_t)connected) != KU_STATUS_OK) {
        fail_service(13U);
    }

    (void)u_puts("[TEST] dns_service_roundtrip: PASS\n");
    ku_exit(0);
}
''')

print("DNS service contract patch applied")
