#!/usr/bin/env python3
"""Inject the TLS runtime qualification into the physical-network boot branch."""

from pathlib import Path


path = Path("kernel/main.cpp")
text = path.read_text()
needle = ': "[TEST] network_online_icmp: SKIP");'
count = text.count(needle)
if count != 1:
    raise SystemExit(f"TLS probe marker count={count}, expected 1")
marker = text.index(needle)
line_end = text.find("\n", marker)
if line_end < 0:
    raise SystemExit("TLS probe marker has no line terminator")
line_end += 1

probe = r'''        {
            static uint8_t tls_output[4096]{};
            static uint8_t tls_bounded[128]{};
            size_t tls_length = 0U;
            uint16_t http_status = 0U;
            net::Status tls_status = net::service::https_get(
                "kurogane.test", "/ok",
                tls_output, sizeof(tls_output), &tls_length, &http_status);
            bool body_ok = false;
            static constexpr char expected_body[] = "KURO_TLS_OK";
            for (size_t start = 0U;
                 start + sizeof(expected_body) - 1U <= tls_length;
                 ++start) {
                size_t matched = 0U;
                while (matched < sizeof(expected_body) - 1U &&
                       tls_output[start + matched] ==
                           static_cast<uint8_t>(expected_body[matched])) {
                    ++matched;
                }
                if (matched == sizeof(expected_body) - 1U) {
                    body_ok = true;
                    break;
                }
            }
            if (tls_status != net::Status::Ok || http_status != 200U ||
                !body_ok) {
                terminal::println("[TEST] tls_https_valid: FAIL");
                boot_failure("TLS", "verified HTTPS request failed");
            }
            terminal::println("[TEST] tls_https_valid: PASS");

            tls_length = 0U;
            http_status = 0U;
            tls_status = net::service::https_get(
                "wrong.kurogane.test", "/ok",
                tls_output, sizeof(tls_output), &tls_length, &http_status);
            if (tls_status != net::Status::InterfaceError) {
                terminal::println("[TEST] tls_hostname_reject: FAIL");
                boot_failure("TLS", "hostname mismatch was not rejected");
            }
            terminal::println("[TEST] tls_hostname_reject: PASS");

            tls_length = 0U;
            http_status = 0U;
            tls_status = net::service::https_get(
                "untrusted.kurogane.test", "/ok",
                tls_output, sizeof(tls_output), &tls_length, &http_status);
            if (tls_status != net::Status::InterfaceError) {
                terminal::println("[TEST] tls_untrusted_ca_reject: FAIL");
                boot_failure("TLS", "untrusted certificate was not rejected");
            }
            terminal::println("[TEST] tls_untrusted_ca_reject: PASS");

            tls_length = 0U;
            http_status = 0U;
            tls_status = net::service::https_get(
                "kurogane.test", "/large",
                tls_bounded, sizeof(tls_bounded), &tls_length, &http_status);
            if (tls_status != net::Status::BufferTooSmall ||
                tls_length != sizeof(tls_bounded) || http_status != 200U) {
                terminal::println("[TEST] tls_bounded_response: FAIL");
                boot_failure("TLS", "bounded HTTPS response contract failed");
            }
            terminal::println("[TEST] tls_bounded_response: PASS");
            terminal::println("[TEST] tls_https: PASS");
        }
'''

path.write_text(text[:line_end] + probe + text[line_end:])
print("TLS runtime qualification injected after physical-network probe")
