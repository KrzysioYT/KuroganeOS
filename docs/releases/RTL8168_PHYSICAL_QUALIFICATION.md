# RTL8168 physical qualification procedure

Status: **BLOCKED / NOT QUALIFIED**. Host fault injection and QEMU do not
qualify this controller. Keep `KUROGANE_EXPERIMENTAL_RTL81XX` disabled in
normal images. No physical execution was performed for this procedure.

## Fixture and admission gate

Use an isolated x86-64 UEFI test computer, serial capture, a separate network
peer and a private switch/DHCP server. Disconnect production disks; boot
read-only test media with only disposable, labelled scratch storage present.
Have a physical power-off path and a known-good recovery image available.
Record PCI domain/bus/device/function, vendor/device `10ec:8168`, subsystem
`17aa:39dc`, revision `15`, firmware version and board identity. This identifies
one qualification target, not the whole RTL8111/8168 family.

Record full source commit, clean-tree status, submodule commits, compiler,
experimental build flags, IMG/ISO SHA256 and exact test commands. Preserve
serial and peer packet captures with timestamps. Never publish private LAN
traffic or credentials as artifacts. Verify scratch disk hashes before/after.

Admit active DMA testing only on a reviewed candidate with the dedicated
shutdown/rebind probe. The RTL8168H test backend checks the live MAC XID
`0x541`; PCI revision `15` alone is insufficient to establish that match.
Physical execution must verify the controller's stop and pending-transaction
readbacks on this fixture.

## Building the isolated probe

From a dedicated Linux or WSL checkout of the candidate commit, initialize
the recursive submodules, record `git rev-parse HEAD` and keep the checkout
separate from release builds. Run these commands before boot:

```sh
python3 scripts/qualification/inject-rtl8168-physical.py
git diff --check
git diff --binary -- kernel/main.cpp kernel/net/physical.cpp > rtl8168-bench.patch
bash scripts/build-media-linux.sh --configuration release --rebuild
```

Retain `rtl8168-bench.patch` and `dist/SHA256SUMS.txt` alongside the probe
image. Label the image `RTL8168-BENCH` when moving it into the test evidence.
Keep the unmodified candidate available for the default-image comparison.

The injected boot code runs before user sessions. It requires the Realtek
interface, a DHCP lease and gateway echo, queues a marked Ethernet frame
with EtherType `0x88B5`, calls verified shutdown and then obtains a new lease
and gateway echo after rebind with the original MAC. Capture ICMP sequences
`0x8101` and `0x8102` and the Ethernet frame on the isolated peer. The enqueue
result verifies submission. Peer capture is needed to verify delivery.
Qualify repeated traffic, cable churn and D3hot/D0 separately.

## Required sequence and evidence

1. Boot the default image. Verify discovery without Realtek reset, DMA enable
   or interface publication. Then boot the isolated experimental candidate.
2. Verify BAR selection, PCI memory decoding, reset completion within its
   timeout, a valid unicast MAC matching the fixture, and PHY/link state.
   Log both PCI configuration and controller status; a PCI ID alone is not PASS.
3. Obtain a DHCP lease from the isolated peer. Verify the assigned address,
   gateway and lease data against the server capture. Confirm ARP and ICMP in
   both directions with the production network interface.
4. Exchange uniquely sequenced payloads in both directions, including short
   frames and supported MTU frames. Compare payload bytes/checksums at each
   end, record loss/duplicates and run enough traffic to wrap both rings many
   times. A transmit enqueue counter alone is not delivery evidence.
5. Repeat cable disconnect/reconnect at least 20 times, with idle and queued
   traffic. Confirm no new descriptors are offered while link is down, no
   stale RX is delivered, and traffic/DHCP recover after reconnect. Record
   ring ownership and DMA page counts at every transition.
6. Invoke explicit shutdown with RX and TX outstanding. Require acknowledged
   engine stop/reset, PCI bus-master-disable readback and device-specific
   evidence that outstanding DMA has drained before freeing pages or MMIO.
   Observe the peer and allocator trace. Reinitialize and repeat traffic.
7. Inject reset timeout, unreadable/changed PCI identity, lost/relocated BAR,
   failed bus-master disable and resource-release failure through the reviewed
   test harness. Never physically remove a non-hotplug PCIe device. Require
   bounded failure, no stale MMIO access, retained DMA ownership if stopping is
   unverified, no duplicate frees and no automatic reuse of ambiguous pages.
8. Run the active PCI D0/D3hot/D0 sequence only after shutdown is verified.
   Check identity/configuration/BARs before reinitialization; verify fresh
   bidirectional traffic and lease recovery. This is not system ACPI suspend.
9. Cold reboot twice, repeat steps 2–4, and compare all scratch disk hashes.
   Attach serial logs, peer captures, allocator/PCI traces and checksums to
   the exact candidate SHA. An independent reviewer assesses the result.

## Stop conditions and verdict

Stop on unexpected writes, unverified DMA stop, corruption, controller loss,
reset timeout, unsafe ownership or unrelated-device failure. Do not free or
reuse ambiguous DMA pages; retain them and power off the isolated fixture.
Investigate before restarting. Do not turn a timeout into PASS by retrying.

For every row record PASS/FAIL/BLOCKED, observed/expected values and artifact
paths. Missing equipment, shutdown instrumentation or independent review is
BLOCKED. Passing this fixture does not qualify other revisions, physical USB
hubs, the complete 7.0 milestone or Flux 8.0.
