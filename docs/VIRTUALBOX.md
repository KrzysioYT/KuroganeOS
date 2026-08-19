# KuroganeOS + Oracle VirtualBox

Ta instrukcja opisuje referencyjny profil Oracle VirtualBox dla KuroganeOS
**3.3.3-dev DEV BETA** na hoście x86-64 Intel/AMD.

KuroganeOS rozdziela media według hypervisora. Nie używaj IMG przeznaczonego
dla QEMU jako nośnika VirtualBox.

```text
VirtualBox: dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
QEMU:       dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

Na Apple Silicon uruchamiaj gościa x86-64 przez QEMU/TCG. Referencyjny target
VirtualBox pozostaje hostem Windows/Linux x86-64.

## Referencyjna konfiguracja VM

KuroganeOS ISO jest obecnie **x86-64 UEFI-only**. Legacy BIOS nie jest
obsługiwany przez release ISO.

```text
Firmware:           EFI64 / UEFI
Secure Boot:        OFF
I/O APIC:           ON
RAM:                2048 MiB zalecane
CPU:                1-2
Graphics:           VBoxSVGA, 64-128 MiB VRAM, 3D OFF
HDD controller:     SATA / Intel AHCI
SATA port count:    1 dla pojedynczego VDI
HDD:                pusty VDI, >= 2 GiB
Optical controller: IDE / PIIX4
DVD:                KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
Boot order:         DVD -> Disk
Network:            NAT
NIC:                Intel PRO/1000 MT Desktop (82540EM)
Audio:              Intel AC'97
Keyboard/Mouse:     PS/2
```

Najważniejsza zasada storage:

```text
SATA / IntelAHCI (1 port dla pojedynczego dysku)
└── SATA 0:0 -> KuroganeOS.vdi

IDE / PIIX4
└── DVD -> KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

**Nie podpinaj dysku instalacyjnego VDI jako IDE.** Installer 3.3.3-dev zapisuje
system przez własny sterownik PCI AHCI i wymaga wykrytego kontrolera Intel AHCI.

Przy pojedynczym dysku ustaw `Port Count = 1`. VirtualBox potrafi utworzyć
kontroler AHCI z dużą liczbą zaimplementowanych, ale pustych portów. Sterownik
KuroganeOS po resecie HBA sonduje każdy port oznaczony jako implemented, więc
niepotrzebnie wysoki `Port Count` może znacznie wydłużyć start lub spowodować
fałszywy timeout automatycznego smoke-testu.

## Tworzenie nowej VM automatycznie — Windows

Najbezpieczniejsza opcja to helper repozytorium:

```powershell
.\scripts\create-virtualbox-vm.ps1 `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso" `
    -Name "KuroganeOS-3.3.3-VB" `
    -Nic e1000
```

Helper tworzy:

- firmware EFI64;
- I/O APIC;
- HDD na SATA / IntelAHCI;
- pojedynczy port SATA dla pojedynczego VDI;
- ISO jako DVD na IDE / PIIX4;
- boot order DVD -> Disk;
- E1000 82540EM + NAT.

Windowsowy helper rozwiązuje ścieżki względem bieżącej lokalizacji PowerShell,
a nie katalogu procesu Windows, i uruchamia `VBoxManage.exe` przez
`System.Diagnostics.Process`. Normalny progress VirtualBox `0%...100%` na
stderr nie jest dzięki temu traktowany jako `NativeCommandError`.

## Naprawa istniejącej VM — Windows

VM musi być **całkowicie wyłączona**. Saved state nie jest obsługiwany podczas
zmiany kontrolerów storage.

```powershell
.\scripts\repair-virtualbox-boot.ps1 `
    -Name "KuroganeOS" `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso"
```

Aktualny helper:

1. wymusza EFI64 + I/O APIC + DVD -> Disk;
2. tworzy SATA / IntelAHCI, jeżeli go brakuje;
3. jeżeli wykryje VDI/VMDK/VHD podpięty jako IDE i SATA jest wolne, przenosi ten
   sam wirtualny dysk na SATA 0:0;
4. przy błędzie migracji próbuje przywrócić dysk do starego slotu IDE;
5. utrzymuje ISO jako optyczny napęd IDE;
6. przed PASS sprawdza obecność IntelAHCI i dysku SATA.

Helper nie usuwa zawartości wirtualnego dysku. Istniejącego kontrolera SATA z
wieloma używanymi portami nie zwęża automatycznie, ponieważ mogłoby to odłączyć
inne dyski użytkownika.

## Konfiguracja ręczna w GUI

1. `New` -> utwórz VM typu `Other / Other 64-bit` i pomiń unattended install.
2. Przydziel 2048 MiB RAM i 1-2 CPU.
3. `Settings -> System` -> włącz EFI/UEFI oraz I/O APIC.
4. Secure Boot pozostaw wyłączony.
5. `Settings -> Storage` -> dodaj `SATA Controller` typu `AHCI`.
6. Dla pojedynczego VDI ustaw `Port Count = 1`.
7. Podepnij pusty VDI jako `SATA Port 0`.
8. Dodaj `IDE Controller / PIIX4`.
9. Podepnij `KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso` jako napęd DVD.
10. Ustaw boot order `Optical -> Hard Disk`.
11. `Network -> Adapter 1`: NAT, Intel PRO/1000 MT Desktop (82540EM), Cable Connected.
12. Audio: Intel AC'97.
13. Uruchom VM.

## Oczekiwany boot ISO

Ścieżka startu:

```text
VirtualBox EFI64
 -> El Torito EFI
 -> GPT EFI System Partition
 -> FAT EFI image
 -> EFI/BOOT/BOOTX64.EFI
 -> kernel.elf
 -> install.pkg
 -> Red Flux Setup
```

Jeżeli widzisz ekran `WELCOME TO KUROGANEOS` albo Red Flux Setup, to ISO,
UEFI/El Torito, loader i kernel zostały już poprawnie uruchomione.

## Instalacja

1. Uruchom VM z canonical VirtualBox ISO.
2. Wybierz `INSTALL KUROGANEOS`.
3. Wybierz język i konto.
4. Wybierz pusty dysk SATA/AHCI.
5. Na ekranie potwierdzenia wpisz `INSTALL` i naciśnij Enter.
6. Wielkość liter nie ma znaczenia: `install`, `Install` i `INSTALL` są
   normalizowane do `INSTALL`.
7. Błędne słowo nie zatrzymuje instalatora — formularz pozostaje aktywny i
   pozwala poprawić wpis.
8. `Esc` na ekranie potwierdzenia wraca bezpiecznie do wyboru dysku. Do tego
   momentu GPT nie jest jeszcze zapisany.
9. Poczekaj na `[TEST] installer_complete: PASS`.
10. Wyłącz VM, odłącz ISO i ustaw Disk jako pierwszy boot device.
11. Uruchom zainstalowany system z VDI.

Pierwszy destrukcyjny zapis na wybrany dysk następuje dopiero po zaakceptowanym
potwierdzeniu i markerze:

```text
[TEST] installer_confirmation: PASS
```

Po nim udany zapis GPT jest raportowany jako:

```text
installer stage 1/9: target confirmed and GPT written
```

Ten marker jest również wiarygodnym runtime proof ścieżki SATA/AHCI: installer
nie może go osiągnąć bez poprawnie wykrytego block device i udanego zapisu na
tymczasowy VDI.

## Diagnostyka

### `No bootable medium` / `No bootable drive`

Sprawdź:

```text
Firmware = EFI64
Secure Boot = OFF
DVD = canonical virtualbox_x86_64.iso
Boot order = DVD -> Disk
```

Nie używaj `.img` jako VirtualBox DVD.

### UEFI Shell zamiast KuroganeOS

W UEFI Shell można sprawdzić nośnik:

```text
map -r
fs0:
ls
cd EFI\BOOT
BOOTX64.EFI
```

Jeżeli `BOOTX64.EFI` uruchamia loader, problem nie leży w samym pliku EFI.

### `[FATAL][INSTALL] no PCI AHCI controller`

ISO już wystartowało. Problemem jest konfiguracja storage VM.

Popraw na:

```text
VDI -> SATA / Intel AHCI
ISO -> IDE / PIIX4 DVD
```

Normalny boot systemu (poza skróconą ścieżką instalatora) może wypisywać m.in.:

```text
[INFO][AHCI][CPU0][KERNEL] PCI AHCI controllers detected=1
[INFO][AHCI][CPU0][KERNEL] active AHCI controllers=1
```

Installer mode 3.3.3-dev może nie wypisać tych liczników, mimo że AHCI działa.
Jeżeli zamiast nich pojawi się:

```text
[TEST] installer_confirmation: PASS
installer stage 1/9: target confirmed and GPT written
```

to kontroler, VDI oraz zapis przez AHCI faktycznie działają.

### Smoke zatrzymuje się po `Intel ICH AC97 PCM output ready`

Jeżeli realny VirtualBox smoke uruchamia loader/kernel, ale długo nie przechodzi
do setupu, sprawdź liczbę portów SATA. Dla jednej maszyny testowej z jednym VDI
użyj:

```text
Controller: IntelAHCI
Port Count: 1
VDI:        SATA 0:0
```

Starszy helper CLI tworzył kontroler bez jawnego `--portcount`, co mogło
wystawić wiele pustych portów. Kernel sonduje porty zaimplementowane przez HBA,
więc taki profil mógł wyglądać jak zawieszenie AHCI mimo poprawnego ISO.
Bieżące helpery i smoke-test tworzą kontroler z `--portcount 1`.

### Smoke dochodzi do `GPT written`, ale nadal kończy timeoutem

To był błąd starszej kwalifikacji, nie błąd VirtualBoxa. Smoke czekał wyłącznie
na tekst `active AHCI controllers=...`, którego installer mode nie musi
emitować. Bieżący `smoke-virtualbox-iso.ps1` akceptuje dwa dowody runtime:

```text
[INFO][AHCI] ... active AHCI controllers=N   (N >= 1)
```

albo silniejszy test zapisu:

```text
[TEST] installer_confirmation: PASS
installer stage 1/9: target confirmed and GPT written
```

Drugi wariant potwierdza nie tylko wykrycie HBA, ale również wybór block device i
rzeczywisty zapis GPT do tymczasowego VDI.

### `INSTALLATION CONFIRMATION DID NOT MATCH INSTALL`

Ten fatalny flow dotyczył starszego instalatora 3.3.3-dev. Bieżący installer
normalizuje potwierdzenie do wielkich liter, pozwala ponowić błędny wpis i
obsługuje `Esc` jako bezpieczny powrót do wyboru dysku.

Jeżeli nadal widzisz stary ekran, uruchamiasz stare ISO. Zrób pełny rebuild i
sprawdź datę/hash canonical VirtualBox ISO.

### Brak internetu

Profil referencyjny:

```text
Attached to = NAT
Adapter Type = Intel PRO/1000 MT Desktop (82540EM)
Cable Connected = ON
```

Szczegóły: [`NETWORKING.md`](NETWORKING.md).

## Weryfikacja ISO

Statyczna walidacja:

```bash
bash ./scripts/verify-virtualbox-iso.sh \
  ./dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso \
  --passes 20
```

Na Windows realny smoke Oracle VirtualBox:

```powershell
.\scripts\smoke-virtualbox-iso.ps1 `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso" `
    -TimeoutSeconds 90
```

Smoke tworzy tymczasową VM z EFI64, **jednoportowym SATA/IntelAHCI**, IDE DVD i
E1000 NAT. `VBoxManage.exe` jest uruchamiany przez `System.Diagnostics.Process`,
dlatego normalne komunikaty progress `0%...100%` na stderr nie są traktowane
jako PowerShell `NativeCommandError`.

Smoke nie zalicza samego `kernel entry`. Wymaga runtime proof storage: jawnego
`active AHCI controllers >= 1` albo udanego `installer stage 1/9` z zapisem GPT
na tymczasowy VDI. Odrzuca log zawierający `[FATAL][INSTALL]` lub wymagany test
FAIL.

Windows `build-media.ps1` uruchamia realny smoke domyślnie. Można go pominąć
wyłącznie jawnie:

```powershell
.\scripts\build-media.ps1 -Configuration release -Rebuild -SkipVirtualBoxSmoke
```

Build z `-SkipVirtualBoxSmoke` nie jest kwalifikowany jako runtime VirtualBox
PASS.
