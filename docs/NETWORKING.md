# KuroganeOS Networking

## Status 3.3.1-dev

KuroganeOS posiada własny kernelowy stos sieciowy. Referencyjnym urządzeniem dla
QEMU/VirtualBox jest Intel E1000 **82540EM (`8086:100E`)**.

Warstwy dostępne w kernelu:

```text
PCI
 -> E1000 82540EM
 -> Ethernet
 -> ARP
 -> IPv4
 -> ICMP
 -> UDP
 -> DHCP
 -> DNS A resolver
 -> podstawowy TCP connect/probe
```

Bieżący rozwój 3.3.1 rozszerza publiczne API Ring-3, żeby zwykłe aplikacje nie
musiały korzystać z kernela bezpośrednio.

## VirtualBox

Ustaw:

```text
Network Adapter: Enabled
Attached to: NAT
Adapter Type: Intel PRO/1000 MT Desktop (82540EM)
Cable Connected: ON
```

To jest profil referencyjny. Domyślny PCNet VirtualBox nie jest obecnie
referencyjnym urządzeniem KuroganeOS.

## QEMU

Referencyjny model urządzenia:

```text
-device e1000
```

oraz typowa sieć userspace/NAT:

```text
-netdev user,id=net0
-device e1000,netdev=net0
```

## Co oznacza `internet READY`

Samo wykrycie karty nie oznacza internetu. Pełny pozytywny stan wymaga:

1. wykrycia PCI `8086:100E`;
2. poprawnej inicjalizacji E1000;
3. aktywnego linku;
4. uzyskania dzierżawy DHCP;
5. ustawienia gateway;
6. ustawienia DNS;
7. poprawnego ARP do gateway;
8. testu ICMP gateway;
9. opcjonalnie rozwiązania nazwy DNS i TCP probe.

Kernel wypisuje markery diagnostyczne na serial.

## Bezpieczeństwo

Nie dodawaj syscalla typu `execute_kernel_network_command`. Publiczne API Ring-3
ma być wąskie i walidowane: status interfejsu, rozwiązywanie DNS, ping oraz
późniejsze socket handles.

## Dla programistów

Zobacz:

- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)
- [`DEVELOPERS/APP_DEVELOPMENT.md`](DEVELOPERS/APP_DEVELOPMENT.md)

Publiczne API sieciowe w SDK powinno być używane zamiast prywatnych nagłówków
`kernel/net/*`.
