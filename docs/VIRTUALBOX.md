# KuroganeOS + Oracle VirtualBox

Referencyjny profil Oracle VirtualBox dla KuroganeOS **3.3.3-dev DEV BETA** na
hoście x86-64 Intel/AMD.

```text
VirtualBox: dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
QEMU:       dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

Nie używaj QEMU `.img` jako napędu optycznego VirtualBox.

## Referencyjna VM

KuroganeOS release ISO jest obecnie **x86-64 UEFI-only**.

```text
Firmware:           EFI64 / UEFI
Secure Boot:        OFF
I/O APIC:           ON
RAM:                2048 MiB
CPU:                1-2
Graphics:           VMSVGA, 128 MiB VRAM, 3D OFF
HDD controller:     SATA / Intel AHCI
SATA port count:    1 dla pojedynczego VDI
HDD:                VDI >= 2 GiB, SATA 0:0
Optical controller: IDE / PIIX4
DVD:                KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
Boot order:         DVD -> Disk
Network:            NAT
NIC:                Intel PRO/1000 MT Desktop (82540EM)
Audio:              Intel AC'97
Keyboard/Mouse:     PS/2
Serial:             COM1 0x3F8 IRQ4 -> file
```

Canonical storage layout:

```text
SATA / IntelAHCI (Port Count = 1)
└── SATA 0:0 -> KuroganeOS.vdi

IDE / PIIX4
└── DVD -> KuroganeOS-3.3.3-dev-virtualbox_x86_64.iso
```

Installer zapisuje target przez własny sterownik PCI AHCI. VDI podpięty tylko
pod IDE nie jest wspieranym targetem instalacji.

Przy jednym VDI ustaw `Port Count = 1`. Duża liczba pustych portów AHCI może
wydłużyć polling linku i fałszować timeout automatycznej kwalifikacji.

## Automatyczne utworzenie VM — Windows

Najprostszy pełny start:

```powershell
.\scripts\create-virtualbox-vm.ps1 `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox_x86_64.iso" `
    -Name "KuroganeOS-3.3.3-VB" `
    -Nic e1000 `
    -Start
```

Helper tworzy EFI64, I/O APIC, `VMSVGA + 128 MiB`, jednoportowy IntelAHCI +
VDI, IDE/PIIX4 + ISO, DVD-first boot, E1000/NAT oraz COM1 log.

Bez `-Start` helper tylko tworzy i weryfikuje VM, a na końcu wypisuje dokładną
komendę uruchomienia przez znaleziony `VBoxManage.exe`.

Dla domyślnej lokalizacji VM serial znajduje się tutaj:

```text
%USERPROFILE%\VirtualBox VMs\<NAZWA_VM>\kurogane-serial.log
```

Windowsowe helpery rozwiązują ścieżki względem bieżącej lokalizacji PowerShell,
a `VBoxManage.exe` jest uruchamiany przez `System.Diagnostics.Process`, bez
polegania na PowerShellowym traktowaniu normalnego stderr `0%...100%` jako
`NativeCommandError`.

## Naprawa istniejącej VM

VM musi być całkowicie wyłączona, nie tylko zapisana w saved state.

```powershell
.\scripts\repair-virtualbox-boot.ps1 `
    -Name "KuroganeOS" `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox_x86_64.iso"
```

Helper najpierw próbuje dokładnej nazwy. Jeżeli nie istnieje, pobiera
`VBoxManage list vms`. Gdy `-Name` pasuje jako prefix do dokładnie jednej VM,
wybierze ją i wypisze resolved name. Gdy pasuje kilka VM, zatrzyma się i poda
listę zamiast zgadywać.

Repair wymusza EFI64 + DVD -> Disk + VMSVGA, zapewnia IntelAHCI, może przenieść
istniejący HDD z IDE na SATA 0:0 z próbą rollbacku, utrzymuje ISO jako IDE DVD
i konfiguruje COM1 serial diagnostics. Nie usuwa zawartości VDI.

Domyślny serial log naprawianej VM trafia do `%TEMP%` i jego pełna ścieżka jest
wypisywana przez helper. Można podać własną:

```powershell
.\scripts\repair-virtualbox-boot.ps1 `
    -Name "KuroganeOS-VB-Test" `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox_x86_64.iso" `
    -SerialLog ".\state\runlogs\virtualbox-manual.log"
```

## Konfiguracja ręczna

1. `New` -> `Other / Other 64-bit`, bez unattended installation.
2. RAM 2048 MiB, CPU 1-2.
3. `System` -> EFI/UEFI ON, I/O APIC ON, Secure Boot OFF.
4. `Display` -> VMSVGA, 128 MiB VRAM, 3D OFF.
5. `Storage` -> SATA Controller / Intel AHCI, `Port Count = 1`.
6. VDI -> SATA Port 0.
7. IDE Controller / PIIX4.
8. Canonical VirtualBox ISO -> IDE DVD.
9. Boot order `Optical -> Hard Disk`.
10. Network -> NAT, Intel PRO/1000 MT Desktop (82540EM), Cable Connected.
11. Audio -> Intel AC'97.
12. Opcjonalnie COM1: `0x3F8`, IRQ4, file output.

## Co oznacza wejście do Red Flux Setup

Ścieżka bootu ISO:

```text
VirtualBox EFI64
 -> El Torito EFI
 -> GPT EFI System Partition w ISO
 -> FAT EFI image
 -> EFI/BOOT/BOOTX64.EFI
 -> kernel.elf
 -> install.pkg
 -> Red Flux Setup
```

Jeżeli Red Flux Setup jest widoczny, ISO/UEFI/El Torito/BOOTX64.EFI/kernel już
zadziałały. Kolejne błędy należy diagnozować jako installer/storage/filesystem,
a nie jako „ISO nie bootuje”.

## Pełna instalacja

1. Uruchom canonical ISO.
2. `INSTALL KUROGANEOS`.
3. Wybierz język i konto.
4. Wybierz dysk SATA/AHCI.
5. Wpisz `INSTALL` (`install`/`Install` również są akceptowane).
6. Poczekaj na `[TEST] installer_complete: PASS`.
7. Wyłącz VM.
8. Odłącz ISO lub ustaw HDD przed DVD.
9. Uruchom system z VDI.

Szczegółowy pipeline i recovery: [`INSTALLATION.md`](INSTALLATION.md).

## Installer 3.3.3-dev — ważne poprawki

### Nested package directories

Starszy flow tworzył ręcznie tylko kilka katalogów root. Paczka zawiera jednak
m.in.:

```text
/etc/ssl/certs.pem
```

co wymaga `/etc/ssl`. Brak katalogu powodował:

```text
PACKAGE COPY FAILED
```

Bieżący installer tworzy katalogi nadrzędne dynamicznie z każdej zweryfikowanej
ścieżki package manifestu.

### Retry na tym samym VDI

Starszy flow pisał GPT, a po późniejszym błędzie następny start odrzucał ten sam
dysk jako `target is not blank`. Bieżąca potwierdzona ścieżka instalacyjna może
odtworzyć GPT/filesystemy na tym samym wybranym VDI po ponownym wpisaniu
`INSTALL`.

### Root przed bootloaderem

Bieżąca kolejność publikuje UEFI payload dopiero po skopiowaniu i
zsynchronizowaniu ROOT. Dzięki temu niekompletna instalacja nie jest celowo
aktywowanym boot targetem przed trwałym root payloadem.

### Szybka, nadal pełna weryfikacja

Stage 8 nadal robi byte-for-byte readback całego payloadu. Starszy kod używał
4 KiB okna. Ponieważ każdy `fat32::read(path, offset, ...)` zaczyna traversing
łańcucha pliku od początku, było to szczególnie kosztowne na ESP z klastrem
512 B i mogło zatrzymywać realny VirtualBox smoke na stage 7/8 przez ponad 90 s.

Bieżący installer używa bounded 1 MiB readback window, dzięki czemu zachowuje
pełną weryfikację danych, ale radykalnie ogranicza ponowne przechodzenie FAT.
Dodatkowo serial pokazuje każdy weryfikowany plik:

```text
[INSTALL][VERIFY] file=... destination=ROOT path=... bytes=...
[INSTALL][VERIFY] PASS path=...
```

### Diagnostyka kopiowania

Serial log przy błędzie podaje np.:

```text
[INSTALL][COPY] file=17 destination=ROOT path=/etc/ssl/certs.pem operation=mkdir-parent status=...
```

Dostępne etapy obejmują `mkdir-parent`, `remove-stale`, `create`, `write`,
`verify-read` i `verify-stat`.

## Oczekiwane markery instalacji

```text
[TEST] installer_package_preflight: PASS
[TEST] installer_confirmation: PASS
installer stage 1/9: target confirmed and GPT written
installer stage 2/9: GPT validated and partition views ready
installer stage 3/9: filesystems formatted
installer stage 4/9: fresh filesystems mounted
installer stage 5/9: root payload copied and synced
installer stage 6/9: profile and first-boot state committed
installer stage 7/9: UEFI payload activated and synced
[INSTALL][VERIFY] file=...
[INSTALL][VERIFY] PASS path=...
installer stage 8/9: installed payload verified
[TEST] installer_gpt: PASS
[TEST] installer_filesystems: PASS
[TEST] installer_root_payload: PASS
[TEST] installer_uefi_bootloader: PASS
[TEST] installer_profile: PASS
[TEST] installer_payload_verify: PASS
[TEST] installer_complete: PASS
installer stage 9/9: installation complete
```

## Realny Oracle VirtualBox smoke

```powershell
.\scripts\smoke-virtualbox-iso.ps1 `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox_x86_64.iso" `
    -TimeoutSeconds 180
```

`TimeoutSeconds` jest teraz **limitem braku postępu**, a nie jednym zegarem od
startu VM. Nowy serial output resetuje idle deadline. Jednocześnie smoke ma
twardy całkowity limit, więc prawdziwy hang nadal kończy się FAIL.

Smoke tworzy prawdziwą tymczasową VM:

```text
EFI64
VMSVGA / 128 MiB
1 x SATA/IntelAHCI port
2 GiB VDI @ SATA 0:0
IDE/PIIX4 DVD
E1000/NAT
COM1 serial log
```

Kwalifikacja **nie kończy się na `kernel entry` ani `GPT written`**. Finalny
PASS wymaga:

```text
[TEST] installer_complete: PASS
```

Oczekiwane zakończenie:

```text
[virtualbox-smoke] firmware EFI64: PASS
[virtualbox-smoke] DVD-first optical attachment: PASS
[virtualbox-smoke] Intel AHCI target disk configuration: PASS
[virtualbox-smoke] BOOTX64.EFI -> kernel: PASS
[virtualbox-smoke] SATA/AHCI runtime proof: PASS (...)
[virtualbox-smoke] full root + UEFI payload installation: PASS
[virtualbox-smoke] installed payload verification: PASS
[virtualbox-smoke] REAL ORACLE VIRTUALBOX FULL INSTALL: PASS
```

## Statyczna walidacja ISO

```bash
bash ./scripts/verify-virtualbox-iso.sh \
  ./dist/KuroganeOS-3.3.3-dev-virtualbox_x86_64.iso \
  --passes 20
```

Statyczny verifier sprawdza strukturę EFI/El Torito/GPT/ESP. Nie zastępuje
pełnego runtime smoke.

## Diagnostyka

### Czarny ekran po utworzeniu VM

Najpierw upewnij się, że używasz bieżącego helpera. Referencyjna konfiguracja to
`VMSVGA + 128 MiB`, a nie starsze wymuszane `VBoxSVGA`.

Dla VM utworzonej helperem sprawdź serial bez zgadywania:

```powershell
Get-Content "$HOME\VirtualBox VMs\KuroganeOS-3.3.3-VB\kurogane-serial.log" -Tail 100
```

Interpretacja:

```text
serial pusty
  -> VM nie dotarła do loadera/kernela; sprawdź ISO/EFI/boot order

KuroganeOS loader ... / kernel entry
  -> boot działa; problem dotyczy widocznego framebuffer/GOP

[SETUP] KuroganeOS ...
  -> system jest już w installerze nawet jeśli GUI pozostaje czarne
```

### `Unable to inspect VirtualBox VM`

Bieżący repair helper pokazuje stderr `VBoxManage` i zarejestrowane nazwy VM.
Jeżeli `-Name "KuroganeOS"` pasuje jednoznacznie np. do
`KuroganeOS-VB-Test`, helper rozwiąże prefix automatycznie. Przy kilku
kandydatach poda listę i poprosi o dokładną nazwę.

### `No bootable medium`

Sprawdź EFI64, Secure Boot OFF, canonical ISO i DVD-first boot.

### UEFI Shell

```text
map -r
fs0:
ls
cd EFI\BOOT
BOOTX64.EFI
```

Jeżeli `BOOTX64.EFI` uruchamia loader, problem nie leży w samym EFI executable.

### `[FATAL][INSTALL] no PCI AHCI controller`

ISO już wystartowało. Popraw storage:

```text
VDI -> SATA / Intel AHCI
ISO -> IDE / PIIX4 DVD
```

### `PACKAGE COPY FAILED`

Jeżeli widzisz wyłącznie ten stary komunikat bez `[INSTALL][COPY] ...` w serial
logu, VM używa ISO sprzed transactional installer fix. Wykonaj pełny rebuild i
podepnij świeży canonical ISO.

### Timeout po `installer stage 7/9`

Jeżeli serial zatrzymuje się po:

```text
installer stage 7/9: UEFI payload activated and synced
```

na starym buildzie, problemem był koszt stage 8 verification. Na bieżącym
buildzie powinny zaraz pojawić się linie `[INSTALL][VERIFY] ...`. Jeżeli jedna
konkretna ścieżka nie przechodzi przez cały idle timeout, właśnie ten plik lub
jego FAT chain jest punktem dalszej diagnostyki.

### Setup po restarcie

Po `[TEST] installer_complete: PASS` odłącz ISO lub ustaw HDD przed DVD.

### Internet

```text
Attached to = NAT
Adapter Type = Intel PRO/1000 MT Desktop (82540EM)
Cable Connected = ON
```

Szczegóły: [`NETWORKING.md`](NETWORKING.md).
