#include "client.hpp"

#include "kurogane_mbedtls_platform.h"
#include "../tcp_client.hpp"
#include "../../core/log.hpp"
#include "../../drivers/pit.hpp"
#include "../../drivers/rtc.hpp"
#include "../../memory/allocator.hpp"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

namespace net::tls {
namespace {

constexpr uint64_t kTcpTimeoutMs = 5000U;
constexpr uint64_t kBioTimeoutMs = 1500U;
constexpr uint64_t kHandshakeTimeoutMs = 20000U;
constexpr size_t kHandshakeIterationLimit = 96U;
constexpr size_t kRequestCapacity = 768U;
constexpr size_t kTlsReadChunk = 1400U;
constexpr uint16_t kHttpsPort = 443U;

struct Session {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt trust;
    tcp_client::Client tcp;
};

void log_mbedtls_error(const char* stage, int result) {
    log::write_hex(
        log::Level::Error,
        "TLS",
        stage,
        static_cast<uint64_t>(static_cast<int64_t>(result)));
}

void zero_bytes(void* pointer, size_t size) {
    auto* bytes = static_cast<uint8_t*>(pointer);
    if (bytes == nullptr) return;
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

size_t text_length(const char* text) {
    if (text == nullptr) return 0U;
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    return length;
}

bool append_text(
    uint8_t* output,
    size_t capacity,
    size_t* length,
    const char* text) {
    if (output == nullptr || length == nullptr || text == nullptr ||
        *length > capacity) {
        return false;
    }
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (*length >= capacity) return false;
        output[(*length)++] = static_cast<uint8_t>(text[index]);
    }
    return true;
}

uint16_t parse_http_status(const uint8_t* bytes, size_t length) {
    if (bytes == nullptr || length < 12U || bytes[0] != 'H' || bytes[1] != 'T' ||
        bytes[2] != 'T' || bytes[3] != 'P' || bytes[4] != '/') {
        return 0U;
    }
    size_t index = 5U;
    while (index < length && bytes[index] != ' ') ++index;
    while (index < length && bytes[index] == ' ') ++index;
    if (index + 2U >= length || bytes[index] < '0' || bytes[index] > '9' ||
        bytes[index + 1U] < '0' || bytes[index + 1U] > '9' ||
        bytes[index + 2U] < '0' || bytes[index + 2U] > '9') {
        return 0U;
    }
    return static_cast<uint16_t>(
        (bytes[index] - '0') * 100U +
        (bytes[index + 1U] - '0') * 10U +
        (bytes[index + 2U] - '0'));
}

bool handshake_window_open(uint64_t started, size_t iteration) {
    if (iteration >= kHandshakeIterationLimit) return false;
    if (!drivers::pit::initialized() || drivers::pit::frequency_hz() == 0U) {
        return true;
    }
    const uint64_t frequency = drivers::pit::frequency_hz();
    const uint64_t timeout_ticks =
        (kHandshakeTimeoutMs * frequency + UINT64_C(999)) / UINT64_C(1000);
    return drivers::pit::ticks() - started < timeout_ticks;
}

int bio_send(void* context, const unsigned char* buffer, size_t length) {
    auto* client = static_cast<tcp_client::Client*>(context);
    if (client == nullptr || (length != 0U && buffer == nullptr)) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }
    const net::Status status = tcp_client::send(
        client,
        reinterpret_cast<const uint8_t*>(buffer),
        length,
        kBioTimeoutMs);
    if (status == net::Status::Ok) {
        return length > static_cast<size_t>(INT32_MAX)
            ? MBEDTLS_ERR_SSL_BAD_INPUT_DATA
            : static_cast<int>(length);
    }
    if (status == net::Status::WouldBlock) return MBEDTLS_ERR_SSL_WANT_WRITE;
    log::write_u64(
        log::Level::Error,
        "TLS",
        "BIO send network status=",
        static_cast<uint64_t>(status));
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

int bio_receive(void* context, unsigned char* buffer, size_t length) {
    auto* client = static_cast<tcp_client::Client*>(context);
    if (client == nullptr || buffer == nullptr || length == 0U) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }
    size_t received = 0U;
    const net::Status status = tcp_client::receive(
        client,
        reinterpret_cast<uint8_t*>(buffer),
        length,
        &received,
        kBioTimeoutMs);
    if (status == net::Status::Ok) {
        return received > static_cast<size_t>(INT32_MAX)
            ? MBEDTLS_ERR_SSL_INTERNAL_ERROR
            : static_cast<int>(received);
    }
    if (status == net::Status::WouldBlock) return MBEDTLS_ERR_SSL_WANT_READ;
    log::write_u64(
        log::Level::Error,
        "TLS",
        "BIO receive network status=",
        static_cast<uint64_t>(status));
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

bool rtc_valid(const rtc::DateTime& value) {
    return value.year >= 2020U && value.year <= 9999U &&
        value.month >= 1U && value.month <= 12U &&
        value.day >= 1U && value.day <= 31U &&
        value.hour <= 23U && value.minute <= 59U && value.second <= 60U;
}

int compare_datetime(
    const rtc::DateTime& now,
    const mbedtls_x509_time& certificate_time) {
    const int current[] = {
        static_cast<int>(now.year), static_cast<int>(now.month),
        static_cast<int>(now.day), static_cast<int>(now.hour),
        static_cast<int>(now.minute), static_cast<int>(now.second)};
    const int target[] = {
        certificate_time.year, certificate_time.mon, certificate_time.day,
        certificate_time.hour, certificate_time.min, certificate_time.sec};
    for (size_t index = 0U; index < 6U; ++index) {
        if (current[index] < target[index]) return -1;
        if (current[index] > target[index]) return 1;
    }
    return 0;
}

bool peer_certificate_times_valid(const mbedtls_x509_crt* peer) {
    rtc::DateTime now{};
    if (peer == nullptr || !rtc::read(now) || !rtc_valid(now)) {
        log::write(log::Level::Error, "TLS", "RTC unavailable or invalid for certificate check");
        return false;
    }
    for (const mbedtls_x509_crt* certificate = peer;
         certificate != nullptr;
         certificate = certificate->next) {
        if (compare_datetime(now, certificate->valid_from) < 0 ||
            compare_datetime(now, certificate->valid_to) > 0) {
            log::write(log::Level::Error, "TLS", "certificate validity interval rejected by RTC");
            return false;
        }
    }
    return true;
}

void initialize_session(Session* session) {
    zero_bytes(session, sizeof(*session));
    mbedtls_entropy_init(&session->entropy);
    mbedtls_ctr_drbg_init(&session->drbg);
    mbedtls_ssl_init(&session->ssl);
    mbedtls_ssl_config_init(&session->config);
    mbedtls_x509_crt_init(&session->trust);
    tcp_client::initialize(&session->tcp);
}

void free_session(Session* session) {
    if (session == nullptr) return;
    static_cast<void>(tcp_client::close(&session->tcp));
    mbedtls_x509_crt_free(&session->trust);
    mbedtls_ssl_free(&session->ssl);
    mbedtls_ssl_config_free(&session->config);
    mbedtls_ctr_drbg_free(&session->drbg);
    mbedtls_entropy_free(&session->entropy);
    zero_bytes(session, sizeof(*session));
    memory::kfree(session);
}

Status setup_tls(
    Session* session,
    const char* host_name,
    const uint8_t* trust_pem,
    size_t trust_pem_size) {
    if (ku_tls_hardware_entropy_available() == 0) {
        log::write(log::Level::Error, "TLS", "RDRAND/RDSEED entropy unavailable");
        return Status::EntropyUnavailable;
    }
    int result = mbedtls_entropy_add_source(
        &session->entropy,
        ku_tls_hardware_entropy,
        nullptr,
        32U,
        MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (result != 0) {
        log_mbedtls_error("entropy_add_source error=", result);
        return Status::EntropyUnavailable;
    }

    static constexpr unsigned char personalization[] =
        "KuroganeOS TLS 3.3.3";
    result = mbedtls_ctr_drbg_seed(
        &session->drbg,
        mbedtls_entropy_func,
        &session->entropy,
        personalization,
        sizeof(personalization) - 1U);
    if (result != 0) {
        log_mbedtls_error("ctr_drbg_seed error=", result);
        return Status::EntropyUnavailable;
    }

    if (trust_pem_size < 2U || trust_pem[trust_pem_size - 1U] != '\0') {
        log::write(log::Level::Error, "TLS", "trust PEM is empty or not NUL terminated");
        return Status::TrustStoreInvalid;
    }
    result = mbedtls_x509_crt_parse(
        &session->trust,
        trust_pem,
        trust_pem_size);
    if (result < 0) {
        log_mbedtls_error("x509_crt_parse error=", result);
        return Status::TrustStoreInvalid;
    }
    if (result > 0) {
        log::write_u64(
            log::Level::Warn,
            "TLS",
            "trust certificates skipped during parse=",
            static_cast<uint64_t>(result));
    }

    result = mbedtls_ssl_config_defaults(
        &session->config,
        MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) {
        log_mbedtls_error("ssl_config_defaults error=", result);
        return Status::SetupFailure;
    }
    mbedtls_ssl_conf_authmode(&session->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&session->config, &session->trust, nullptr);
    mbedtls_ssl_conf_rng(
        &session->config,
        mbedtls_ctr_drbg_random,
        &session->drbg);
    result = mbedtls_ssl_setup(&session->ssl, &session->config);
    if (result != 0) {
        log_mbedtls_error("ssl_setup error=", result);
        return Status::SetupFailure;
    }
    result = mbedtls_ssl_set_hostname(&session->ssl, host_name);
    if (result != 0) {
        log_mbedtls_error("ssl_set_hostname error=", result);
        return Status::SetupFailure;
    }
    mbedtls_ssl_set_bio(
        &session->ssl,
        &session->tcp,
        bio_send,
        bio_receive,
        nullptr);
    return Status::Ok;
}

Status perform_handshake(Session* session) {
    const uint64_t started = drivers::pit::ticks();
    for (size_t iteration = 0U;
         handshake_window_open(started, iteration);
         ++iteration) {
        const int result = mbedtls_ssl_handshake(&session->ssl);
        if (result == 0) {
            const uint32_t verify_result = mbedtls_ssl_get_verify_result(&session->ssl);
            if (verify_result != 0U) {
                log::write_hex(
                    log::Level::Error,
                    "TLS",
                    "certificate verify flags=",
                    static_cast<uint64_t>(verify_result));
                return Status::CertificateFailure;
            }
            const mbedtls_x509_crt* peer =
                mbedtls_ssl_get_peer_cert(&session->ssl);
            return peer_certificate_times_valid(peer)
                ? Status::Ok : Status::CertificateTimeFailure;
        }
        if (result != MBEDTLS_ERR_SSL_WANT_READ &&
            result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            log_mbedtls_error("ssl_handshake error=", result);
            return Status::HandshakeFailure;
        }
    }
    log::write(log::Level::Error, "TLS", "TLS handshake deadline exceeded");
    return Status::Timeout;
}

Status write_all(Session* session, const uint8_t* data, size_t length) {
    size_t offset = 0U;
    size_t attempts = 0U;
    while (offset < length && attempts++ < 128U) {
        const int result = mbedtls_ssl_write(
            &session->ssl,
            data + offset,
            length - offset);
        if (result > 0) {
            offset += static_cast<size_t>(result);
            continue;
        }
        if (result == MBEDTLS_ERR_SSL_WANT_READ ||
            result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        log_mbedtls_error("ssl_write error=", result);
        return Status::IoFailure;
    }
    if (offset != length) {
        log::write(log::Level::Error, "TLS", "TLS request write deadline exceeded");
    }
    return offset == length ? Status::Ok : Status::Timeout;
}

Status read_response(
    Session* session,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length,
    uint16_t* out_http_status) {
    uint8_t chunk[kTlsReadChunk]{};
    size_t written = 0U;
    size_t idle_attempts = 0U;
    while (idle_attempts < 128U) {
        const int result = mbedtls_ssl_read(&session->ssl, chunk, sizeof(chunk));
        if (result == MBEDTLS_ERR_SSL_WANT_READ ||
            result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            ++idle_attempts;
            continue;
        }
        if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        if (result < 0) {
            log_mbedtls_error("ssl_read error=", result);
            return Status::IoFailure;
        }
        idle_attempts = 0U;
        const size_t available = static_cast<size_t>(result);
        if (available > output_capacity - written) {
            const size_t remaining = output_capacity - written;
            for (size_t index = 0U; index < remaining; ++index) {
                output[written + index] = chunk[index];
            }
            written += remaining;
            *out_length = written;
            if (*out_http_status == 0U) {
                *out_http_status = parse_http_status(output, written);
            }
            return Status::ResponseTooLarge;
        }
        for (size_t index = 0U; index < available; ++index) {
            output[written + index] = chunk[index];
        }
        written += available;
        if (*out_http_status == 0U) {
            *out_http_status = parse_http_status(output, written);
        }
    }
    *out_length = written;
    if (written == 0U) {
        log::write(log::Level::Error, "TLS", "HTTPS peer returned no response bytes");
    }
    return written == 0U ? Status::IoFailure : Status::Ok;
}

} // namespace

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
    uint16_t* out_http_status) {
    if (out_length != nullptr) *out_length = 0U;
    if (out_http_status != nullptr) *out_http_status = 0U;
    if (stack == nullptr || host_name == nullptr || host_name[0] == '\0' ||
        path == nullptr || path[0] != '/' || trust_pem == nullptr ||
        output == nullptr || output_capacity == 0U || out_length == nullptr ||
        out_http_status == nullptr || local_port == 0U ||
        text_length(host_name) > 253U || text_length(path) > 1024U) {
        return Status::InvalidArgument;
    }

    auto* session = static_cast<Session*>(
        memory::kmalloc(sizeof(Session), alignof(Session)));
    if (session == nullptr) {
        log::write(log::Level::Error, "TLS", "unable to allocate TLS session");
        return Status::SetupFailure;
    }
    initialize_session(session);

    Status status = setup_tls(session, host_name, trust_pem, trust_pem_size);
    if (status != Status::Ok) {
        free_session(session);
        return status;
    }

    const net::Status tcp_status = tcp_client::connect(
        &session->tcp,
        stack,
        peer,
        local_port,
        kHttpsPort,
        initial_sequence,
        kTcpTimeoutMs);
    if (tcp_status != net::Status::Ok) {
        log::write_u64(
            log::Level::Error,
            "TLS",
            "TCP connect to 443 failed status=",
            static_cast<uint64_t>(tcp_status));
        free_session(session);
        return Status::TcpFailure;
    }

    status = perform_handshake(session);
    if (status != Status::Ok) {
        free_session(session);
        return status;
    }

    uint8_t request[kRequestCapacity]{};
    size_t request_length = 0U;
    if (!append_text(request, sizeof(request), &request_length, "GET ") ||
        !append_text(request, sizeof(request), &request_length, path) ||
        !append_text(request, sizeof(request), &request_length,
            " HTTP/1.1\r\nHost: ") ||
        !append_text(request, sizeof(request), &request_length, host_name) ||
        !append_text(request, sizeof(request), &request_length,
            "\r\nUser-Agent: KuroganeWeb/0.3\r\n"
            "Accept: text/html,text/plain,*/*\r\n"
            "Connection: close\r\n\r\n")) {
        free_session(session);
        return Status::ResponseTooLarge;
    }

    status = write_all(session, request, request_length);
    if (status == Status::Ok) {
        status = read_response(
            session,
            output,
            output_capacity,
            out_length,
            out_http_status);
    }
    static_cast<void>(mbedtls_ssl_close_notify(&session->ssl));
    free_session(session);
    return status;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidArgument: return "invalid argument";
        case Status::EntropyUnavailable: return "secure hardware entropy unavailable";
        case Status::TcpFailure: return "TCP connection failed";
        case Status::TrustStoreInvalid: return "CA trust store invalid";
        case Status::SetupFailure: return "TLS setup failed";
        case Status::HandshakeFailure: return "TLS handshake failed";
        case Status::CertificateFailure: return "certificate or hostname verification failed";
        case Status::CertificateTimeFailure: return "certificate validity time check failed";
        case Status::IoFailure: return "TLS I/O failed";
        case Status::ResponseTooLarge: return "HTTPS response exceeded bounded buffer";
        case Status::Timeout: return "TLS operation timed out";
        default: return "unknown TLS status";
    }
}

} // namespace net::tls
