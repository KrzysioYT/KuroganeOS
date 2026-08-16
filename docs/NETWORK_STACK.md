# Network Stack

## Warstwy

```text
NIC driver → NetworkDeviceOps → Ethernet → ARP → IPv4
                                              ├→ ICMP
                                              ├→ UDP → DHCP/DNS
                                              └→ TCP
                                                    ↓
                                                 Sockets
```

## Stan

Parsery i logika Ethernet/ARP/IPv4/ICMP oraz loopback mają testy hostowe i runtime marker loopback. Nie ma sterownika NIC, DMA, rzeczywistego ruchu warstwy 2, DHCP, UDP/TCP, DNS ani sockets. Z tego powodu całej sieci nie klasyfikujemy jako działającej.

## Następny backend

Po storage i podstawach userspace referencyjnym driverem będzie VirtIO Net dla QEMU; E1000 i RTL8169 są późniejszymi backendami. Wszystkie mają implementować `NetworkDeviceOps`, a socket API ma być bliskie POSIX bez kopiowania wewnętrznej architektury Linux.
