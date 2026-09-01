# KuroganeOS + Oracle VirtualBox

Referencyjny profil dla KuroganeOS `3.3.3-dev` na hoście x86-64 Intel/AMD.
Bieżąca gałąź rozwija Forged Steel/KuroganeOS 5, ale nie jest jeszcze release
5.0.0.

## Właściwe medium

Windows media pipeline publikuje osobne artefakty:

```text
VirtualBox: dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
QEMU:       dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

Nie podpinaj QEMU `.img` jako DVD VirtualBox.

## Canonical VM profile

```text
Firmware:           EFI64 / UEFI
Secure Boot:        OFF
I/O APIC:           ON
RAM:                2048 MiB
CPU:                1-2
Graphics:           VMSVGA / 128 MiB VRAM / 3D OFF
HDD controller:     SATA / Intel AHCI
SATA port count:    1 dla pojedynczego VDI
HDD:                VDI >= 2 GiB @ SATA 0:0
Optical controller: IDE / PIIX4
DVD:                KuroganeOS VirtualBox ISO
Boot order:         DVD -> Disk
Network:            NAT
NIC:                PCnet-FAST III (Am79C973)
Audio:              Intel AC'97
Keyboard/Mouse:     PS/2
Serial:             COM1 0x3F8 IRQ4 -> file
```

**VirtualBox canonical NIC to obecnie PCnet-FAST III.** QEMU development runner
używa E1000. `create-virtualbox-vm.ps1` pozwala jawnie wybrać `e1000` albo
`virtio` do dodatkowej kwalifikacji, ale nie są one domyślnym profilem Oracle
VirtualBox.

## Storage

```text
SATA / IntelAHCI
└── SATA 0:0 -> KuroganeOS.vdi

IDE / PIIX4
└── DVD -> KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

Installer zapisuje target własnym backendem AHCI. VDI podpięty wyłącznie do IDE
nie jest referencyjnym targetem instalacji.

Przy jednym VDI ustaw `SATA Port Count = 1`. Puste dodatkowe porty zwiększają
polling i mogą pogarszać timeouty testowe.

## Automatyczne utworzenie VM — Windows

```powershell
.\scripts\create-virtualbox-vm.ps1 `
  -Iso .\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso `
  -Name "KuroganeOS-3.3.3-VB" `
  -Start
```

Domyślnie helper tworzy PCnet/NAT. Profile alternatywne:

```powershell
-Nic e1000
-Nic virtio
-Nic pcnet
```

Serial dla domyślnego katalogu VM:

```text
%USERPROFILE%\VirtualBox VMs\<NAZWA_VM>\kurogane-serial.log
```

## Naprawa istniejącej VM

VM musi być całkowicie wyłączona:

```powershell
.\scripts\repair-virtualbox-boot.ps1 `
  -Name "KuroganeOS" `
  -Iso .\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

Helper ustawia EFI, boot order, VMSVGA, IntelAHCI, ISO oraz serial diagnostics i
może przenieść istniejący HDD z IDE na SATA bez tworzenia nowego dysku.

## Ręczna konfiguracja

1. New -> Other / Other 64-bit.
2. RAM 2048 MiB, CPU 1-2.
3. EFI ON, I/O APIC ON, Secure Boot OFF.
4. VMSVGA, 128 MiB VRAM, 3D OFF.
5. SATA / Intel AHCI, Port Count 1.
6. VDI -> SATA 0:0.
7. IDE / PIIX4 -> bieżące VirtualBox ISO jako DVD.
8. Boot order Optical -> Hard Disk.
9. Network NAT + PCnet-FAST III, Cable Connected.
10. Audio Intel AC'97.
11. Opcjonalnie COM1 0x3F8 IRQ4 do pliku.

## Boot flow

```text
VirtualBox EFI64
 -> El Torito EFI
 -> EFI/BOOT/BOOTX64.EFI
 -> kernel
 -> installer/live payload
 -> PID1
 -> KUROGANE // SECURE ACCESS
 -> Forged Steel desktop
```

Pojawienie się Secure Access oznacza, że UEFI, loader, kernel, Foundation root,
PID1 i graficzna session gate już wystartowały.

## Pełna instalacja

1. Uruchom ISO.
2. Wybierz `INSTALL KUROGANEOS`.
3. Ustaw język i lokalny profil.
4. Wybierz VDI na SATA/AHCI.
5. Potwierdź destrukcyjną operację tekstem `INSTALL`.
6. Poczekaj na `[TEST] installer_complete: PASS`.
7. Wyłącz VM.
8. Odłącz ISO albo ustaw HDD jako pierwszy boot target.
9. Uruchom z VDI.
10. Zweryfikuj `persistent Kurogane Root`, PID1 i Secure Access.

Szczegóły: [INSTALLATION.md](INSTALLATION.md).

## Realny smoke VirtualBox

```powershell
.\scripts\smoke-virtualbox-iso.ps1 `
  -Iso .\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso `
  -TimeoutSeconds 180
```

Smoke używa tymczasowej VM/VDI i powinien potwierdzić nie tylko optical boot,
ale pełne:

```text
ISO boot
AHCI target
install.pkg
installer_complete
reboot z VDI
persistent Kurogane Root
/system/init PID 1
sieć canonical PCnet/NAT
```

Tymczasowa VM jest usuwana po teście.

## Statyczna walidacja ISO

```bash
bash ./scripts/verify-virtualbox-iso.sh \
  ./dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso \
  --passes 20
```

Statyczny verifier sprawdza El Torito/EFI/GPT/ESP/PE. Nie zastępuje runtime
VirtualBox smoke.

## Diagnostyka

### `No bootable medium`

Sprawdź:

```text
EFI ON
Secure Boot OFF
VirtualBox ISO
DVD przed HDD
```

### `[FATAL][INSTALL] no PCI AHCI controller`

Boot ISO działa. Problemem jest storage — VDI musi być na SATA/IntelAHCI.

### Czarny ekran, ale serial pracuje

Jeżeli serial pokazuje kernel/PID1, boot działa, a problem dotyczy GOP/display.
Sprawdź VMSVGA, 128 MiB VRAM i 3D OFF.

### System wolny

VirtualBox i QEMU mają inne ścieżki wydajności. Nie porównuj bezpośrednio FPS z
QEMU TCG. Dla QEMU na Windows do pracy nad GUI używaj WHPX przez
`run-qemu-fast.ps1`.

## Ograniczenia

- Guest Additions nie są portowane;
- 3D acceleration VirtualBox nie jest backendem KuroganeOS;
- pełny Direct3D/GPU compositor nie jest jeszcze produkcyjnie gotowy;
- release PASS wymaga realnej kwalifikacji na x86-64 host, nie tylko poprawnego
  ISO.
