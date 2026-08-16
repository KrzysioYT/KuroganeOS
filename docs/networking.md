# Networking

## Data path

```text
Intel 82540EM / E1000 DMA rings
              |
          Ethernet II
          /         \
        ARP         IPv4
                    |-- ICMP echo
                    |-- UDP -- DHCP / DNS
                    `-- bounded active TCP -- HTTP probe
```

The supported physical path is PCI vendor `8086`, device `100e` (82540EM),
used by QEMU's `e1000` model. The driver enables PCI bus mastering, maps MMIO as
supervisor cache-disabled memory and owns bounded RX/TX DMA descriptor rings.
Frame lengths and DMA addresses are checked before submission.

## Protocols

- Ethernet II validates minimum/maximum frame sizes and EtherType.
- ARP maintains a bounded neighbor cache and resolves the configured gateway.
- IPv4 validates version, header length, total length and checksum; outgoing
  packets are not fragmented.
- ICMP implements echo request/reply and backs `ping` plus boot probes.
- UDP validates pseudo-header checksum and bounded payloads.
- DHCP performs DISCOVER/OFFER/REQUEST/ACK and installs address, mask, gateway
  and DNS server from the validated lease.
- DNS sends bounded A queries and parses compressed response names without
  following unbounded pointer loops.
- TCP is a minimal active client sufficient for a bounded HTTP validation
  probe; it is not a general socket implementation.

The kernel also has a loopback interface. When no supported NIC is present,
the safe-mode diagnostic path reports loopback rather than a fake physical
link. Normal QEMU qualification requires E1000 link, DHCP, UDP and gateway ICMP;
DNS and online/TCP probes are recorded separately because external connectivity
can depend on the host.

## Diagnostics and tests

The emergency kernel console provides `ip`, `ifconfig`, `route`, `arp`, `ping`
and `nslookup`. Hosted tests cover serialization, checksums, malformed frames,
ARP, IPv4, ICMP, UDP and TCP header logic. QEMU serial markers include
`e1000_link`, `dhcp_lease`, `udp_transport`, `network_gateway_icmp`,
`dns_resolver` and the optional online/TCP results.

## Limitations

There is no userspace socket ABI, IPv6, fragmentation/reassembly, listening
TCP server, full retransmission/congestion control, TLS, firewall, Wi-Fi,
VirtIO-net or multiple simultaneously routed NICs. Network service polling is
bounded and single-core; the stack is suitable for system validation, not
untrusted production traffic.
