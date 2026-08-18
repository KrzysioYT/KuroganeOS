# KuroganeOS + Oracle VirtualBox

Ta instrukcja dotyczy bieżącej linii KuroganeOS **3.3.x**. Dokładna wersja
artefaktów wynika z `common/version.h`; aktualny development target to
`3.3.3-dev`.

Jeżeli chcesz tylko szybko uruchomić system, zacznij od
[`START_HERE.md`](START_HERE.md).

## Najważniejsze: ISO jest UEFI-only

KuroganeOS nie publikuje obecnie legacy-BIOS bootloadera. Oficjalne ISO zawiera
ścieżkę x86-64 UEFI/El Torito EFI oraz `EFI/BOOT/BOOTX64.EFI`.

**VirtualBox VM uruchomiona z firmware BIOS nie ma poprawnej ścieżki startu i
może pokazać `No bootable medium` / `No bootable drive`.**

Referencyjna konfiguracja:

```text
Firmware:          EFI64 / UEFI
Secure Boot:       OFF
Chipset:           PIIX3 lub ICH9
I/O APIC:          ON
RAM:               >= 768 MiB (zalecane 1024 MiB)
CPU:               1-2
Optical boot:      KuroganeOS-3.3.3-dev-x86_64.iso
Storage HDD:       SATA / Intel AHCI
Boot order:        DVD -> Disk
Network mode:      NAT
Network adapter:   Intel PRO/1000 MT Desktop (82540EM) [default]
Audio controller:  Intel AC'97
Keyboard:          PS/2
Mouse:             PS/2
```

Na hostach Apple Silicon uruchamiaj gościa x86-64 przez QEMU/TCG. Referencyjny
VirtualBox target to host x86-64 Intel/AMD.

---

## Windows: naprawa istniejącej VM z `No bootable drive`

Repozytorium zawiera helper, który dla istniejącej, wyłączonej maszyny:

- wymusza `EFI64`;
- włącza I/O APIC;
- ustawia `DVD -> Disk`;
- opcjonalnie ponownie podpina ISO.

Przykład:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\repair-virtualbox-boot.ps1 `
  -Name "KuroganeOS 3.3.3-dev" `
  -Iso .\dist\KuroganeOS-3.3.3-dev-x86_64.iso
```

Jeżeli ISO jest już poprawnie podpięte, można pominąć `-Iso`.

Po naprawie skrypt wypisuje wykrytą konfigurację firmware i boot order oraz
marker `PASS`.

---

## Tworzenie poprawnej VM automatycznie

### Windows

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\create-virtualbox-vm.ps1 `
  -Iso .\dist\KuroganeOS-3.3.3-dev-x86_64.iso
```

### Linux/macOS x86-64

```bash
bash ./scripts/create-virtualbox-vm.sh \
  --iso ./dist/KuroganeOS-3.3.3-dev-x86_64.iso
```

Helpery pobierają domyślną nazwę VM z `common/version.h`, zawsze ustawiają
`EFI64`, DVD-first boot, SATA/AHCI i odrzucają `.img` jako nośnik optyczny.

---

## Tworzenie VM ręcznie w GUI

1. Otwórz VirtualBox i wybierz `New`.
2. Utwórz gościa x86-64 i pomiń unattended installation.
3. Przydziel około 1024 MiB RAM i 1-2 CPU.
4. Utwórz pusty VDI, najlepiej >= 2 GiB.
5. **Settings -> System -> włącz EFI/UEFI.**
6. Secure Boot pozostaw wyłączony.
7. VDI podepnij przez SATA / Intel AHCI.
8. `KuroganeOS-3.3.3-dev-x86_64.iso` podepnij jako Optical Drive.
9. Ustaw boot order `Optical/DVD -> Hard Disk`.
10. Sieć: `NAT`, cable connected, domyślnie `Intel PRO/1000 MT Desktop (82540EM)`.
11. Audio: Intel AC'97.
12. Uruchom VM.

---

## Sieć: E1000, VirtIO-net i PCnet

KuroganeOS ma trzy backendy wirtualnych kart sieciowych:

```text
virtio-net
Intel E1000 / 82540EM
AMD PCnet
```

Kernel próbuje je wykrywać automatycznie. Dla VirtualBox **E1000 pozostaje
profilem domyślnym**, ponieważ ma najdłuższą historię testów KuroganeOS na tym
hypervisorze.

Rozszerzony CI kwalifikuje wszystkie trzy backendy pod QEMU user NAT i wymaga
dzierżawy DHCP oraz odpowiedzi ICMP z gateway:

```text
E1000      PASS
PCnet      PASS
VirtIO-net PASS
```

Helpery pozwalają wybrać alternatywę:

Windows:

```powershell
.\scripts\create-virtualbox-vm.ps1 `
  -Iso .\dist\KuroganeOS-3.3.3-dev-x86_64.iso `
  -Nic virtio
```

Linux/macOS:

```bash
bash ./scripts/create-virtualbox-vm.sh \
  --iso ./dist/KuroganeOS-3.3.3-dev-x86_64.iso \
  --nic virtio
```

Dostępne profile:

```text
e1000  -> 82540EM
virtio -> VirtIO-net
pcnet   -> Am79C973
```

QEMU PASS nie jest automatycznie VirtualBox PASS. Przed zmianą domyślnego
profilu na VirtIO-net nadal wymagany jest realny test Oracle VirtualBox x86-64
na Windows: NAT + DHCP + gateway + DNS oraz install/reboot smoke.

---

## Oczekiwany boot z ISO

Poprawne ISO zawiera między innymi:

```text
/EFI/BOOT/BOOTX64.EFI
/kernel.elf
/install.pkg
/efiboot.img
```

`efiboot.img` jest dedykowanym FAT16 obrazem EFI używanym przez wpis El Torito
UEFI. Ten sam obraz jest wystawiony jako EFI System Partition w GPT.
Zainstalowany system korzysta z własnego ESP FAT32 + root FAT32.

Ścieżka startu:

```text
VirtualBox EFI64
  -> El Torito EFI
  -> EFI/BOOT/BOOTX64.EFI
  -> kernel.elf
  -> install.pkg
  -> Red Flux Setup
```

---

## Instalacja

1. Uruchom ISO.
2. Wybierz `Install KuroganeOS`.
3. Wybierz język i konto.
4. Wybierz **pusty** VDI podpięty przez SATA/AHCI.
5. Wpisz `INSTALL` dopiero po ponownym sprawdzeniu dysku.
6. Poczekaj na zakończenie verification.
7. Wyłącz VM.
8. Odłącz ISO.
9. Uruchom ponownie z HDD.

Po instalacji boot order może być `Hard Disk -> Optical`.

---

## Diagnostyka `No bootable medium` / `No bootable drive`

Sprawdź w tej kolejności:

1. **Firmware = EFI64 / UEFI**, nie BIOS.
2. ISO, a nie `.img`, jest podpięte jako Optical Drive.
3. DVD/Optical jest pierwsze w boot order przy pierwszym uruchomieniu.
4. Secure Boot jest wyłączony.
5. Plik ISO nie ma 0 B.
6. SHA-256 zgadza się z `dist/SHA256SUMS.txt`.
7. ISO przeszło `verify-virtualbox-iso.sh`.
8. Jeśli VM utworzono wcześniej w Windows, uruchom `repair-virtualbox-boot.ps1`.

Jeżeli firmware uruchamia UEFI Shell, można ręcznie sprawdzić, czy widoczna jest
ścieżka `EFI\BOOT\BOOTX64.EFI`. Brak tej ścieżki zwykle oznacza błędnie podpięte
lub uszkodzone ISO, a nie błąd kernela.

### System nie widzi dysku do instalacji

Użyj:

```text
SATA Controller / Intel AHCI
```

Nie używaj NVMe jako jedynego dysku instalacyjnego w bieżącej DEV BETA.

### Brak internetu

Najpierw użyj profilu referencyjnego:

```text
Attached to = NAT
Adapter Type = Intel PRO/1000 MT Desktop (82540EM)
Cable Connected = ON
```

Jeżeli testujesz VirtIO-net, zaznacz w zgłoszeniu dokładny model adaptera i
wersję VirtualBox.

---

## Weryfikacja ISO

Ręcznie:

```bash
bash ./scripts/verify-virtualbox-iso.sh \
  ./dist/KuroganeOS-3.3.3-dev-x86_64.iso \
  --passes 20
```

Verifier kontroluje m.in.:

- El Torito EFI;
- GPT i EFI System Partition;
- FAT obrazu bootowego;
- `EFI/BOOT/BOOTX64.EFI`;
- PE32+ / AMD64 / EFI Application;
- kernel i `install.pkg`;
- stabilny SHA-256.

Jeżeli `VBoxManage` jest dostępny na zgodnym hoście x86-64:

```bash
bash ./scripts/verify-virtualbox-iso.sh \
  ./dist/KuroganeOS-3.3.3-dev-x86_64.iso \
  --passes 20 \
  --virtualbox-smoke
```

Na Windows media build może wykonać ten smoke przez `-VirtualBoxSmoke`.

GitHub Actions dodatkowo bootuje ISO przez OVMF/QEMU i kwalifikuje E1000,
PCnet oraz VirtIO-net. Realny VirtualBox smoke pozostaje wymagany przed
bezwarunkowym oznaczeniem konkretnego release candidate jako
`VirtualBox runtime PASS`.
