#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: anchor count={count}, expected 1")
    file.write_text(text.replace(old, new, 1))


replace_once(
    "kernel/net/network.hpp",
    """    IterationStopped,\n    InterfaceError\n};\n""",
    """    IterationStopped,\n    InterfaceError,\n    NameNotFound\n};\n""",
)

replace_once(
    "kernel/net/protocols.cpp",
    """    const uint16_t flags = read_be16(packet + 2U);\n    if (read_be16(packet) != expected_transaction_id) return Status::NotForUs;\n    if ((flags & UINT16_C(0x8000)) == 0U ||\n        (flags & UINT16_C(0x000f)) != 0U) return Status::MalformedPacket;\n    const size_t questions = read_be16(packet + 4U);\n""",
    """    const uint16_t flags = read_be16(packet + 2U);\n    if (read_be16(packet) != expected_transaction_id) return Status::NotForUs;\n    if ((flags & UINT16_C(0x8000)) == 0U) return Status::MalformedPacket;\n    const uint16_t response_code = flags & UINT16_C(0x000f);\n    if (response_code == UINT16_C(3)) return Status::NameNotFound;\n    if (response_code != 0U) return Status::InterfaceError;\n    const size_t questions = read_be16(packet + 4U);\n""",
)

replace_once(
    "kernel/user/runtime_base.inc",
    """        case net::Status::Ok: return KU_STATUS_OK;\n        case net::Status::WouldBlock:\n""",
    """        case net::Status::Ok: return KU_STATUS_OK;\n        case net::Status::NameNotFound: return KU_STATUS_NOT_FOUND;\n        case net::Status::WouldBlock:\n""",
)

replace_once(
    "tests/test_network_protocols.cpp",
    """    if (net::parse_dns_a_response(\n            response, cursor, UINT16_C(0x9999), &answer) !=\n        net::Status::NotForUs) return 10;\n    return 0;\n}\n""",
    """    if (net::parse_dns_a_response(\n            response, cursor, UINT16_C(0x9999), &answer) !=\n        net::Status::NotForUs) return 10;\n\n    uint8_t nxdomain[512]{};\n    copy_bytes(nxdomain, query, query_length);\n    net::write_be16(nxdomain + 2U, UINT16_C(0x8183));\n    net::write_be16(nxdomain + 6U, 0U);\n    if (net::parse_dns_a_response(\n            nxdomain, query_length, UINT16_C(0x1234), &answer) !=\n        net::Status::NameNotFound) return 11;\n\n    net::write_be16(nxdomain + 2U, UINT16_C(0x8182));\n    if (net::parse_dns_a_response(\n            nxdomain, query_length, UINT16_C(0x1234), &answer) !=\n        net::Status::InterfaceError) return 12;\n\n    net::write_be16(nxdomain + 2U, UINT16_C(0x0180));\n    if (net::parse_dns_a_response(\n            nxdomain, query_length, UINT16_C(0x1234), &answer) !=\n        net::Status::MalformedPacket) return 13;\n    return 0;\n}\n""",
)

print("DNS NXDOMAIN typed contract applied")
