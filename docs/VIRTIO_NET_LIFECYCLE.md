# VirtIO-net initialization failure ownership

The modern PCI driver owns one network device. It now attempts one
MSI-X group with separate RX and TX vectors, retaining polling when LAPIC, MSI-X capability,
bounded BAR mappings or a free route are unavailable. A queue rejecting an
assigned vector fails initialization with `QueueInterruptFailed` and resets
the device before releasing its resources.

A one-entry device uses a shared vector. If a two-vector reservation runs out
of vectors and completely rolls back, the driver retries a shared route. A
failed rollback never permits fallback: retained leases and MMIO stay owned
until group teardown succeeds. Malformed/unowned table state does not trigger
the allocation-exhaustion retry. One `RouteGroup` owns the function-wide bits
in both interrupt modes; the existing single-vector PCI API remains available
to other drivers.

Only after reset does the driver disable PCI decoding and size the table/PBA
BARs. Each complete BAR is bounded to 256 KiB and mapped with supervisor-only,
cache-disabled, NX pages. A shared table/PBA BAR has one mapping. The existing
PCI MSI-X core validates spans and owns the vector generation. Both queue
assignments are read back; configuration-change interrupts remain unmapped.
See [VirtIO 1.2, PCI MSI-X vector configuration](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html).

The IRQ handlers increment total and source-specific counters and set atomic
pending bits. A shared IRQ updates only the total: its origin is unknown.
The normal network pump consumes those bits and services TX completions alongside
RX. It still checks the rings to tolerate notification suppression and races;
no packet parsing, allocation or network stack execution occurs in the IRQ.
This is separate notification for the existing RX/TX queue pair, not multiple
queue pairs or an SMP network scheduler. `interrupt_diagnostics()` preserves
the earlier status/vector/total/pending fields and adds route count, RX/TX
vectors and source delivery counts. It does not expose Ring-3 MMIO access.

Initialization owns the original PCI Command, each successfully mapped MMIO
page and every DMA page. Memory decoding is enabled before mapping; bus
mastering is enabled only after device status reads zero following reset.
Once queue addresses have been written, local configuration errors defer
resource release to the device cleanup path.

Cleanup first withdraws the interface and resets the device with a bounded
status poll. Only a completed reset permits freeing exposed buffers. This
follows the [VirtIO device cleanup contract](https://github.com/oasis-tcs/virtio-spec/blob/master/content.tex).
Clearing PCI bus mastering limits additional DMA requests but does not prove
that outstanding DMA has drained.

| Outcome | Resource ownership | Further initialization |
| --- | --- | --- |
| Reset and cleanup complete | MSI-X route released, DMA freed, MMIO unmapped, original PCI Command restored | Allowed |
| Reset not acknowledged | Vector, DMA and mappings retained; function masked and bus mastering disabled where possible | Blocked, `DeviceResetFailed` |
| Vector cleanup fails | Route/DMA/MMIO ownership quarantined | Blocked, `DeviceCleanupFailed` |
| DMA release or unmap fails | Failed ownership records retained | Blocked, `DeviceCleanupFailed` |
| PCI Command cannot be restored | Original command record retained | Blocked, `PciCommandFailed` |

The current initialization path is serialized during boot. This is not a
public runtime hot-unplug API; driver removal, interrupt synchronization and
SMP require their own lifecycle integration.

`tests/test_virtio_net_cleanup.cpp` executes production failure paths with
host PCI/MMIO/DMA adapters, including failed release and rollback. The PCI
word-access test models adjacent write-one-to-clear Status bits. These host
tests do not qualify hardware.

`Qualify 5.0 VirtIO Network Cleanup` injects test-only calls into a clean CI
checkout, builds release media and boots QEMU/OVMF with a real VirtIO NIC.
It initializes both queues, invokes failure cleanup, checks every former DMA
page against the PMM bitmap, checks former MMIO addresses against the page
tables and checks the restored PCI command. Four cycles must pass, followed
by reinitialization, DHCP and gateway ICMP. Production sources contain no
failure trigger. Exact-source run `34546210440`, job `103099276350`, passed
at `bcbcd9b18a7b4a07d4d1022930acc29eb77e8075`. This qualifies reset/free/retry
with the QEMU VirtIO device; it does not qualify MSI-X queue delivery.

`Qualify 5.0 VirtIO MSI-X Runtime` additionally requires both RX/TX assignments,
exact vector allocation counts, rejection of retired vector generations, and
MSI-X BAR unmapping in every cleanup cycle. After final initialization it
requires the real IRQ count to increase during the successful gateway ICMP
transaction. Merely programming a vector cannot satisfy this gate. The new
driver integration passed at source `6898c72f273207dbfd526f78a640433932e2291c`
in Actions run `34683815908`, job `103527144398`. The same source passed the
3.4 regression sweep (`34683815923`), real TLS/HTTPS (`34683815909`) and both
PCI interrupt transport gates (`34683815912`, `34683815880`).

The expanded gate also rebuilds uninjected production media and boots QEMU
with `virtio-net-pci,vectors=0`. It requires the typed capability-unavailable
polling state and successful DHCP/gateway traffic. Raw serial/QEMU logs are
retained as CI artifacts. That fallback passed in run `34684091876` at
`cb352b7aa78105a3b446b354cd0376b96010ac6d`, and again in `34811940595` at
`d42d9e31e98fbcb39577184475e36d97cb77d251`.

The split-source extension now runs a one-/two-vector QEMU matrix, checking
the exact route count, both queue assignments, every retired generation and
independent RX/TX count increases during gateway traffic. The shared case
requires equal vectors and zero source-specific counts. The two-vector job
also rebuilds production media and boots the no-MSI-X device. The split-source
extension is implemented and host-tested, but awaits its exact-source runtime
matrix before being marked qualified.
