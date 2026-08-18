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
    -> aktywny TCP client
    -> HTTP / rozwijany HTTPS/TLS client
```

Referencyjnym profilem VirtualBox pozostaje obecnie Intel E1000 **82540EM
(`8086:100E`)**, ponieważ ma najdłuższą historię kompatybilności z tym
hypervisorem. Nie oznacza to, że E1000 jest jedynym backendem kernela.

## Backendy NIC

### VirtIO-net

Kernel zawiera własny backend VirtIO-net PCI. Implementacja obsługuje m.in.:

- vendor PCI `1AF4`;
- transitional device `1000`;
- modern network device `1041`;
- nowoczesne capability structures PCI;
- split virtqueues RX/TX;
- DMA-backed buffers;
- negocjację `VIRTIO_F_VERSION_1` i `VIRTIO_NET_F_MAC`.

QEMU qualification używa `virtio-net-pci` i **przechodzi wymagany runtime smoke
DHCP + gateway ICMP**. Realny test Oracle VirtualBox x86-64 pozostaje osobnym
wymaganiem przed oznaczeniem VirtIO-net jako bezwarunkowo zweryfikowanego
profilu VirtualBox.

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
Attached to: NAT
Adapter Type: Intel PRO/1000 MT Desktop (82540EM)
Cable Connected: ON
```

### AMD PCnet

PCnet jest utrzymywanym backendem zgodności dla starszych maszyn wirtualnych.
Nie jest domyślnym profilem dla nowych VM. QEMU runtime qualification DHCP +
gateway ICMP również przechodzi.

## DHCP i konfiguracja IPv4

Stan `internet READY` nie zależy od jednego konkretnego PCI ID. Pełny pozytywny
stan wymaga:

1. wykrycia obsługiwanego backendu NIC;
2. poprawnej inicjalizacji i aktywnego linku;
3. uzyskania dzierżawy DHCP;
4. konfiguracji adresu IPv4, maski, gateway i DNS;
5. poprawnego ARP do gateway;
6. pozytywnego testu ICMP gateway;
7. dla wyższych warstw: poprawnego DNS i odpowiedniej sesji TCP/TLS.

Klient DHCP zachowuje parametry z zaakceptowanego `DHCPOFFER`, jeżeli poprawny
`DHCPACK` nie powtarza części opcji sieciowych. Eliminuje to fałszywy fallback
do loopback na serwerach DHCP, które nie duplikują wszystkich opcji w ACK.

## QEMU user-mode NAT

Dla deterministycznych smoke testów można używać standardowego QEMU user NAT.
Skrypty kwalifikacyjne sprawdzają co najmniej:

```text
DHCP lease
ARP/gateway reachability
ICMP gateway
```

Bieżący workflow kwalifikuje runtime wszystkie trzy modele:

```text
e1000          PASS
pcnet          PASS
virtio-net-pci PASS
```

## DNS, TCP, HTTP i TLS

Kernel posiada bounded DNS A resolver i aktywnego klienta TCP używanego przez
warstwę HTTP. Istnieje również freestanding klient TLS oparty na Mbed TLS z
własnym źródłem entropy i trust store ładowanym z root volume.

To nadal nie jest pełny socket stack klasy desktopowej. Brakuje m.in. publicznej
asynchronicznej warstwy socket handles/event waits, bardziej kompletnej obsługi
retransmisji i okien TCP oraz pełnej kwalifikacji przeglądarkowego HTTPS jako
stabilnego API użytkownika.

## Publiczne API

Aplikacje Ring-3 powinny korzystać z publicznego SDK/usługi sieciowej, a nie z
prywatnych nagłówków `kernel/net/*`.

Nie dodawaj syscalla typu `execute_kernel_network_command`. Publiczne API ma być
wąskie i walidowane: status interfejsu, DNS, ping, HTTP/HTTPS i docelowo
asynchroniczne socket handles.

Dla programistów:

- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)
- [`DEVELOPERS/APP_DEVELOPMENT.md`](DEVELOPERS/APP_DEVELOPMENT.md)
- [`VIRTUALBOX.md`](VIRTUALBOX.md)

## Ograniczenia

Wirtualne E1000/PCnet/VirtIO-net nie zastępują sterowników prawdziwych kart
Ethernet/Wi-Fi. Obsługa fizycznego sprzętu wymaga osobnych backendów dla
konkretnych rodzin urządzeń. IPv6, Wi-Fi, pełne socket API, firewall i
produkcyjny browser-grade TCP/TLS pozostają osobnymi etapami rozwoju.
