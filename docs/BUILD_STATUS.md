# KuroganeOS build/runtime status

Snapshot: **21 sierpnia 2026**  
Linia: **3.3.3-dev / DEV BETA**

Ten plik jest krótkim podsumowaniem. Aktywną listą prac i głównym źródłem prawdy pozostaje [`ROADMAP.md`](ROADMAP.md).

## Co jest obecnie zakwalifikowane

### Build i boot media

- UEFI x86-64 `BOOTX64.EFI` + boot protocol v3;
- Linux/QEMU IMG oraz osobne canonical Oracle VirtualBox ISO;
- El Torito EFI + GPT ESP;
- obowiązkowa wielokrotna walidacja struktury ISO;
- OVMF/QEMU optical boot smoke;
- rozszerzone host regression tests.

Canonical media dla bieżącej linii:

```text
VirtualBox: dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
QEMU:       dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

Stare nazwy `*-windows-qemu.img` oraz generic `*-x86_64.iso` nie są bieżącymi canonical artifactami Windows builda.

### VirtualBox

Referencyjny profil Oracle VirtualBox:

```text
Firmware: EFI64 / UEFI
Storage:  SATA / Intel AHCI
Network:  NAT
NIC:      PCnet-FAST III (Am79C973)
Audio:    Intel AC'97
```

Bieżący runtime potrafi:

- uruchomić UEFI ISO;
- przejść instalator;
- zapisać system na SATA/AHCI VDI;
- uruchomić z zainstalowanego VDI bez ISO;
- zamontować persistent root;
- uruchomić `/system/init` jako PID 1;
- uzyskać DHCP, gateway i DNS przez VirtualBox NAT + PCnet.

Pełny release-smoke VirtualBox **nie jest jeszcze zielony**. Aktualny blocker to:

```text
[TEST] fat32_persistence: FAIL
```

Dlatego nie opisujemy jeszcze całego install/reboot flow jako release-qualified PASS.

### QEMU networking

Runtime smoke pod QEMU kwalifikuje:

- Intel E1000;
- AMD PCnet;
- VirtIO-net;
- DHCP;
- gateway ICMP;
- DNS.

E1000 pozostaje ważnym profilem QEMU/testowym, ale **canonical Oracle VirtualBox NIC to obecnie PCnet-FAST III**.

## TLS / HTTPS

KuroganeOS ma zintegrowane:

- Mbed TLS **3.6.7** w profilu freestanding;
- TLS 1.2 client;
- SNI;
- X.509;
- SHA-256/SHA-384/SHA-512;
- RSA/ECDSA;
- systemowy CA bundle `/etc/ssl/certs.pem`;
- RTC/time validation;
- Ring-3 `ku_https_get()`;
- HTTPS path w Kurogane Web.

Poprzedni problem `x509_crt_parse ... D9D2` dotyczył brakującej obsługi SHA-384/OID i nie jest bieżącym głównym blockerem świeżego `main`.

**Aktualny blocker:** handshake dochodzi do ścieżki TLS BIO, po czym TCP/BIO send może zakończyć się `net::Status::InterfaceError`.

Najważniejsze pliki:

```text
kernel/net/tls/client.cpp
kernel/net/tcp_client.cpp
kernel/net/network.cpp
kernel/net/service.cpp
tests/test_tcp_client.cpp
```

HTTPS pozostaje **partial / not release-qualified end-to-end** do czasu przejścia realnego ciągu:

```text
DNS
 -> TCP connect
 -> TLS handshake
 -> certificate + hostname + time verification
 -> HTTPS request
 -> HTTP response
```

## Najważniejsze otwarte blokery

1. Naprawić `fat32_persistence: FAIL` w pełnym VirtualBox install -> reboot smoke.
2. Naprawić TCP/BIO send podczas Mbed TLS handshake bez duplikowania danych i bez psucia SND.UNA/SND.NXT.
3. Dodać deterministyczny end-to-end HTTPS runtime smoke.
4. Zaprojektować async socket handles + readiness/event integration.
5. Domknąć userspace threads i synchronizację wymaganą przez większe porty.

Pełna lista: [`ROADMAP.md`](ROADMAP.md).

## Jak interpretować status

Funkcja jest uznawana za ukończoną dopiero, gdy zgadzają się kod, testy, runtime qualification, publiczne API (jeżeli dotyczy) i dokumentacja. Sam build lub host-test nie jest wystarczającym dowodem dla ścieżek sieciowych, storage i VM hardware.
