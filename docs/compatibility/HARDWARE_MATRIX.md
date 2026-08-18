# Hardware matrix

Status dotyczy bieżącej linii **KuroganeOS 3.3.x**. `implemented` oznacza, że
backend istnieje w źródłach; `qualified` wymaga dodatkowo runtime smoke dla
danego emulowanego urządzenia/platformy.

| Device/platform | Status |
|---|---|
| x86-64 UEFI / EDK2 GOP | implemented; QEMU/OVMF qualified |
| Legacy BIOS boot | unsupported — official ISO is UEFI-only |
| QEMU q35 | qualified development platform |
| Legacy PIC / PIT | implemented and host/QEMU tested |
| PS/2 keyboard | implemented; primary compatibility input path |
| PS/2 mouse | implemented; compatibility path |
| PCI configuration enumeration | implemented and QEMU tested |
| SATA / Intel AHCI | implemented; QEMU qualified, real-hardware coverage experimental |
| NVMe | unsupported |
| VirtIO-blk | unsupported |
| Intel E1000 / 82540EM | implemented; QEMU NAT DHCP/gateway qualification |
| AMD PCnet | implemented; QEMU NAT DHCP/gateway qualification |
| VirtIO-net PCI | implemented; QEMU runtime qualification gate present; VirtualBox host smoke still required |
| USB xHCI / HID | implementation in progress; not universal hardware support |
| Intel ICH AC'97 | implemented PCM output path; VirtualBox/QEMU host coverage still release-dependent |
| UEFI GOP software compositor | implemented; no hardware 3D command submission |
| VirtualBox EFI x86-64 | supported target configuration; release claim requires real VBox smoke |
| VirtualBox legacy BIOS | unsupported |
| Physical x86-64 UEFI | experimental; narrow driver coverage |
| Wi-Fi | unsupported |

For the current VM configuration use [`../VIRTUALBOX.md`](../VIRTUALBOX.md).
Network backend details are in [`../NETWORKING.md`](../NETWORKING.md).
