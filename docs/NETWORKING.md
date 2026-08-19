# KuroganeOS Networking

## Status 3.3.x

Bieżący development target to **3.3.3-dev**. KuroganeOS posiada własny
kernelowy stos sieciowy i kilka backendów wirtualnych kart PCI.

Warstwa `kernel/net/physical.cpp` próbuje urządzenia w kolejności:

```text
VirtIO-net
  -> Intel E1000 / 82540EM
  -> AMD PCnet
```

Dalsza ścieżka protokołów jest wspólna:

```text
PCI NIC
 -> Ethernet II
 -> ARP
 -> IPv4
    -> ICMP
    -> UDP -> DHCP / DNS A
    -> TCP client
    -> HTTP
    -> HTTPS / TLS 1.2 / X.509
```

Referencyjnym profilem VirtualBox pozostaje Intel E1000 **82540EM
(`8086:100E`)**.

## Referencyjny VirtualBox NIC

```text
Attached to: NAT
Adapter Type: Intel PRO/1000 MT Desktop (82540EM)
Cable Connected: ON
```

VirtualBox media i QEMU media są rozdzielone:

```text
VirtualBox: KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
QEMU:       KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

## Backendy NIC

### VirtIO-net

Kernel zawiera własny backend VirtIO-net PCI z obsługą split virtqueues RX/TX,
DMA-backed buffers i negocjacją wymaganych feature bits.

### Intel E1000

Referencyjny model:

```text
Intel 82540EM / PCI 8086:100E
```

QEMU:

```text
-netdev user,id=net0
-device e1000,netdev=net0
```

VirtualBox:

```text
NAT + Intel PRO/1000 MT Desktop (82540EM)
```

### AMD PCnet

PCnet pozostaje backendem zgodności dla starszych VM. Nie jest domyślnym
profilem nowych maszyn VirtualBox.

## DHCP i konfiguracja IPv4

Pozytywny stan sieci wymaga kolejno:

1. wykrycia backendu NIC;
2. aktywnego linku;
3. dzierżawy DHCP;
4. IPv4 + maska + gateway + DNS;
5. ARP do gateway;
6. ICMP gateway;
7. DNS/TCP/TLS dla wyższych warstw.

Klient DHCP zachowuje brakujące parametry z zaakceptowanego `DHCPOFFER`, jeżeli
poprawny `DHCPACK` nie powtarza wszystkich opcji.

## Jak czytać boot log

Przykład zdrowej sieci QEMU E1000:

```text
[TEST] e1000_link: PASS
[TEST] dhcp_lease: PASS
[TEST] udp_transport: PASS
gateway ICMP: PASS
[TEST] network_gateway_icmp: PASS
DNS A example.com: PASS
[TEST] dns_resolver: PASS
[TEST] tcp_http_optional: PASS
[TEST] network_online_icmp: PASS
```

Jeżeli te markery są PASS, problem z HTTPS nie powinien być opisywany jako
"brak internetu". Trzeba wtedy diagnozować TLS/X.509 osobno.

## TLS / HTTPS

Klient TLS jest freestanding i używa przypiętego **Mbed TLS 3.6.7**.
Konfiguracja znajduje się w:

```text
kernel/net/tls/kurogane_mbedtls_config.h
```

Bieżący profil zawiera między innymi:

```text
TLS 1.2 client
SNI
ECDHE_RSA
ECDHE_ECDSA
AES-GCM
SHA-256
SHA-384
SHA-512
X.509 certificate parsing
RSA / ECDSA
```

Trust store jest ładowany z:

```text
/etc/ssl/certs.pem
```

Repozytorium zawiera rooty GTS używane przez pierwszy profil Web PKI.

### `x509_crt_parse error=...D9D2`

Wartość odpowiada `-0x262E`, czyli kombinacji:

```text
MBEDTLS_ERR_X509_UNKNOWN_SIG_ALG
+
MBEDTLS_ERR_OID_NOT_FOUND
```

W Mbed TLS 3.6.x SHA-384 jest osobną capability flag. Samo
`MBEDTLS_SHA512_C` nie wystarcza do zbudowania tabeli OID dla podpisów SHA-384.
Dlatego konfiguracja KuroganeOS jawnie wymaga:

```c
#define MBEDTLS_SHA384_C
```

CI probe wymaga również finalnego:

```text
MBEDTLS_MD_CAN_SHA384
```

Jeżeli po aktualizacji kodu nadal widzisz `D9D2`, najpierw wyklucz stare media.
Na Windows zbuduj ponownie:

```powershell
.\scripts\build-media.ps1 -Configuration release -Rebuild
```

Następnie uruchamiaj wyłącznie canonical artifact:

```text
QEMU:       dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
VirtualBox: dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

Stare nazwy `*-windows-qemu.img` i generic `*-x86_64.iso` nie są canonical
artefaktami bieżącego Windows builda.

### Oczekiwany log poprawnego trust store

Po poprawnym parsowaniu powinien pojawić się log w rodzaju:

```text
[INFO][TLS][CPU0][KERNEL] trust store loaded bytes=...
[INFO][TLS][CPU0][KERNEL] trust certificates accepted=...
```

Nie powinno wystąpić:

```text
CA trust store invalid
x509_crt_parse error=...D9D2
```

## TCP

Klient TCP posiada m.in. SND.UNA/SND.NXT/RCV.NXT, retransmisję z zachowaniem
sequence number, bounded out-of-order buffering i scalanie segmentów
kontynuujących strumień.

To nadal nie jest pełny desktopowy socket stack. Publiczna asynchroniczna
warstwa socket handles/event waits pozostaje dalszym etapem.

## QEMU user NAT

Referencyjny Windows runner:

```powershell
.\scripts\run-qemu-desktop.ps1 -MemoryMiB 2048
```

Runner wybiera canonical `KuroganeOS-*-qemu-x86_64.img` i używa:

```text
E1000
QEMU user NAT
AC97
x86-64 UEFI
```

## Publiczne API

Aplikacje Ring-3 powinny korzystać z publicznego SDK/usługi sieciowej, nie z
prywatnych nagłówków `kernel/net/*`.

Dokumentacja deweloperska:

- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)
- [`DEVELOPERS/APP_DEVELOPMENT.md`](DEVELOPERS/APP_DEVELOPMENT.md)
- [`VIRTUALBOX.md`](VIRTUALBOX.md)

## Ograniczenia

Wirtualne E1000/PCnet/VirtIO-net nie zastępują sterowników fizycznych kart
Ethernet/Wi-Fi. IPv6, Wi-Fi, pełne socket API, firewall oraz kompletna
browser-grade kwalifikacja TCP/TLS pozostają osobnymi etapami rozwoju.
