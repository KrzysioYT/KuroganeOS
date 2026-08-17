#pragma once

#include "../network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::tls {

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    EntropyUnavailable,
    TcpFailure,
    TrustStoreInvalid,
    SetupFailure,
    HandshakeFailure,
    CertificateFailure,
    CertificateTimeFailure,
    IoFailure,
    ResponseTooLarge,
    Timeout
};

/*
 * One bounded, verified HTTPS/1.0 GET. The caller owns DNS and supplies the
 * resolved peer plus a NUL-terminated PEM trust store. The TLS layer enforces
 * SNI/hostname verification, required CA verification and explicit RTC-based
 * certificate validity checks.
 */
Status https_get(
    NetworkStack* stack,
    const IPv4Address& peer,
    uint16_t local_port,
    uint32_t initial_sequence,
    const char* host_name,
    const char* path,
    const uint8_t* trust_pem,
    size_t trust_pem_size,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length,
    uint16_t* out_http_status);

const char* status_message(Status status);

} // namespace net::tls
