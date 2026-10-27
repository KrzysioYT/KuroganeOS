#include <cassert>
#include <cstring>
#include <string>
#include "../kernel/net/tls/client.cpp"

namespace log {
// The isolated HTTP lifecycle workflow compiles this fixture without the
// shared host logger. The full host suite links host_log_stub.cpp instead.
// Weak test-only symbols let the real host fixture override them without
// causing duplicate-definition errors. Never built into guest images.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void write(Level, const char*, const char*) {}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void write_hex(Level, const char*, const char*, uint64_t) {}
}

namespace fixture {
const char* payload = nullptr;
size_t offset = 0;
int terminal = MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
size_t reads = 0;
size_t fragment = 7;
}
extern "C" int mbedtls_ssl_read(mbedtls_ssl_context*, unsigned char* output, size_t capacity) {
    ++fixture::reads;
    const size_t length = fixture::payload ? std::strlen(fixture::payload) : 0;
    if (fixture::offset == length) return fixture::terminal;
    size_t count = length - fixture::offset;
    if (count > fixture::fragment) count = fixture::fragment;
    if (count > capacity) count = capacity;
    std::memcpy(output, fixture::payload + fixture::offset, count);
    fixture::offset += count;
    return static_cast<int>(count);
}
int main() {
    using namespace net::tls;
    uint8_t request[768];
    std::memset(request, 0xA5, sizeof(request));
    size_t length = 99;
    assert(build_http_request("example.org", "/a%20b?q=1&x=2", request, sizeof(request), &length) == Status::Ok);
    const std::string valid(reinterpret_cast<char*>(request), length);
    assert(valid.find("GET /a%20b?q=1&x=2 HTTP/1.1\r\nHost: example.org\r\n") == 0);
    const size_t exact = length;
    assert(build_http_request("example.org", "/a%20b?q=1&x=2", request, exact, &length) == Status::Ok);
    assert(build_http_request("example.org", "/a%20b?q=1&x=2", request, exact - 1, &length) == Status::ResponseTooLarge && length == 0);
    for (const char* host : {"", "a\r\nInjected: yes", "a b", "a:443", "a\t", "a\x7f"})
        assert(build_http_request(host, "/", request, sizeof(request), &length) == Status::InvalidArgument && length == 0);
    for (const char* path : {"relative", "/a\r\nX: y", "/a b", "/a\t", "/a#b", "/\xc3\xa9"})
        assert(build_http_request("example.org", path, request, sizeof(request), &length) == Status::InvalidArgument && length == 0);
    std::string long_host(254, 'a'), long_path(1025, 'a'); long_path[0] = '/';
    assert(build_http_request(long_host.c_str(), "/", request, sizeof(request), &length) == Status::InvalidArgument);
    assert(build_http_request("example.org", long_path.c_str(), request, sizeof(request), &length) == Status::InvalidArgument);
    Session session{};
    uint8_t response[256]{};
    const char* payload = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    const auto receive = [&](int terminal, const char* body, size_t capacity) {
        fixture::payload = body; fixture::offset = 0; fixture::terminal = terminal; fixture::reads = 0;
        length = 0;
        uint16_t status = 0;
        return read_response(&session, response, capacity, &length, &status);
    };
    assert(receive(MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY, payload, sizeof(response)) == Status::Ok);
    assert(length == std::strlen(payload) && std::memcmp(response, payload, length) == 0);
    assert(receive(MBEDTLS_ERR_SSL_WANT_READ, payload, sizeof(response)) == Status::Timeout);
    assert(length == std::strlen(payload) && fixture::reads < 160);
    assert(receive(MBEDTLS_ERR_SSL_WANT_WRITE, nullptr, sizeof(response)) == Status::Timeout && length == 0);
    assert(receive(0, payload, sizeof(response)) == Status::IoFailure && length == std::strlen(payload));
    assert(receive(-123, payload, sizeof(response)) == Status::IoFailure && length == std::strlen(payload));
    assert(receive(MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY, nullptr, sizeof(response)) == Status::IoFailure);
    assert(receive(MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY, payload, 10) == Status::ResponseTooLarge && length == 10);
}
