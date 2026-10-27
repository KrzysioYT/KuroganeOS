# Iron Shield PCI power transactions

The 7.0 native PCI PM layer supports D0, D1, D2 and D3hot through the
`pci::power` API. This is a PMCSR transaction primitive, not system suspend,
automatic idle power saving, D3cold support, or a complete driver resume path.

## Ownership and ordering

- Zero-initialize `Transaction`; serialize access to the device for its whole
  lifetime. An active token cannot be overwritten by another transition.
- Quiesce device DMA and interrupts before a low-power transition. The API
  additionally rejects entry to D1/D2/D3hot while PCI Bus Master is enabled.
- Save device/configuration state before D3hot. The API restores the power
  state only; the driver must restore BARs and its device state before I/O or
  DMA can resume. A D3hot exit can reset the device.
- Return through D0 when waking from D3hot/D2 to an intermediate state. Direct
  D3hot->D1/D2 and D2->D1 requests fail without writing. `restore()` performs
  the required D0 hop when the original state was D1 or D2.
- Supply a bounded `wait_us` callback for any transition involving D2 (200 us)
  or D3hot (10 ms). The API waits before readback or further config accesses.
  A missing callback rejects the operation before a write.
- Failure after a write retains the active token. In particular, a failed
  timer leaves `pending_delay_us` set; `restore()` completes that delay before
  touching the device. Retry restoration rather than discarding the token.
- PME Status is write-one-to-clear and is never echoed as one. A no-op does
  not write PMCSR, and restoration revalidates the capability before writing.

Hardware-specific longer delays, readiness polling, ACPI power resources,
hotplug identity/generation tracking, and driver suspend/resume callbacks
remain future integration work. Callers must not use a token for a replaced
device at the same BDF.

## Verification

`tests/test_pci_power.cpp` exercises the 16 source/target combinations,
unsupported states, malformed capabilities, DMA gating, delay ordering,
timer failure/retry, ignored hardware writes, active-token ownership and a
PME W1C hardware model. It compiles the production implementation directly.

The Iron Shield Intel GbE workflow first builds unmodified source and requires
DHCP/gateway ICMP on both E1000 and E1000e. It then uses
`scripts/qualification/inject-pci-power-runtime.py` in its test checkout to
cycle an unbound E1000e four times through D0->D3hot->D0, using HPET for the
settling delays. The harness restores writable Type-0 configuration with DMA
disabled, then the production driver must initialize and pass DHCP/gateway
ICMP. This pre-bind exercise does not demonstrate suspend of active queues.
The injector is not applied to production source or normal build media.

The test requires QEMU evidence before it is considered qualified; successful
host tests or compilation alone are not runtime proof. Physical hardware and
VirtualBox validation remain separate.

## Design references

- Linux PCI PM documentation, native state transitions and driver ordering:
  <https://www.kernel.org/doc/html/latest/power/pci.html>
- PCI Express Base Specification 2.1, section 5.3.1.4.1 (D3hot exit recovery):
  <https://www.intel.com/content/dam/support/us/en/programmable/support-resources/fpga-wiki/asset03/pci-express-base-r2.1.pdf>

The implementation is written for KuroganeOS; these references define the
hardware contract, not a Linux driver port.
