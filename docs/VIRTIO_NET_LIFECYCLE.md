# VirtIO-net initialization failure ownership

The modern PCI driver is a single-device, polling driver. MSI-X transport has
separate qualification; this change does not enable VirtIO queue interrupts.

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
| Reset and cleanup complete | DMA freed, MMIO unmapped, original PCI Command restored | Allowed |
| Reset not acknowledged | DMA and mappings retained; bus mastering disabled where possible | Blocked, `DeviceResetFailed` |
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
