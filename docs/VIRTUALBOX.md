# KuroganeOS + Oracle VirtualBox

Ta instrukcja dotyczy KuroganeOS **3.3.1-dev** i nowszych wersji serii 3.3.
Jeżeli chcesz tylko szybko uruchomić system, najpierw przeczytaj
[`START_HERE.md`](START_HERE.md).

## Obsługiwany model

KuroganeOS jest gościem **x86-64 UEFI**.

Dla VirtualBox referencyjna konfiguracja to:

```text
Firmware:          EFI / UEFI
Secure Boot:       OFF
Chipset:           PIIX3 lub ICH9
I/O APIC:          ON
RAM:               >= 768 MiB (zalecane 1024 MiB)
CPU:               1-2
Optical boot:      KuroganeOS-3.3.1-dev-x86_64.iso
Storage HDD:       SATA / Intel AHCI
Network mode:      NAT
Network adapter:   Intel PRO/1000 MT Desktop (82540EM)
Audio controller:  Intel AC'97
Keyboard:          PS/2
Mouse:             PS/2
```

Na hostach Apple Silicon KuroganeOS x86-64 powinien być uruchamiany przez QEMU
TCG. VirtualBox jest referencyjnym targetem dla hostów x86-64 Intel/AMD.

---

## Tworzenie VM w GUI krok po kroku

1. Otwórz VirtualBox.
2. Kliknij `New`.
3. Nazwa: `KuroganeOS 3.3.1-dev`.
4. Jeżeli VirtualBox rozpoznaje ISO jako system wymagający unattended install,
   wyłącz/omiń unattended installation. KuroganeOS ma własny instalator.
5. Przydziel 1024 MiB RAM.
6. Przydziel 1 lub 2 CPU.
7. Utwórz pusty VDI, najlepiej 2 GiB lub większy.
8. Wejdź w `Settings -> System` i włącz EFI/UEFI.
9. Secure Boot pozostaw wyłączony.
10. W `Storage` upewnij się, że pusty VDI jest podpięty przez SATA/AHCI.
11. Podepnij `KuroganeOS-3.3.1-dev-x86_64.iso` jako Optical Drive.
12. W `Network` ustaw Adapter 1:

```text
Enable Network Adapter: ON
Attached to: NAT
Adapter Type: Intel PRO/1000 MT Desktop (82540EM)
Cable Connected: ON
```

13. W `Audio` ustaw:

```text
Enable Audio: ON
Audio Controller: Intel AC'97
Audio Output: ON
```

14. Boot order ustaw na:

```text
1. Optical / DVD
2. Hard Disk
```

15. Uruchom VM.

---

## Oczekiwany boot z ISO

Poprawne ISO ma zawierać jednocześnie:

```text
/EFI/BOOT/BOOTX64.EFI
/kernel.elf
/install.pkg
/efiboot.img
```

`efiboot.img` jest dedykowanym **FAT16 30 MiB** obrazem EFI używanym przez wpis
El Torito UEFI. Ma dokładnie 61440 sektorów po 512 B, czyli pozostaje poniżej
16-bitowego limitu `<65535` sektorów obrazu bootowego. W jego wnętrzu muszą
istnieć:

```text
/EFI/BOOT/BOOTX64.EFI
/EFI/BOOT/kernel.elf
/kernel.elf
/install.pkg
```

Ten sam EFI boot image jest również wystawiony w GPT jako EFI System Partition.
Zainstalowany system nadal używa własnego ESP FAT32 i root FAT32; FAT16 dotyczy
tylko nośnika optycznego/El Torito.

Po wybraniu nośnika UEFI firmware powinno uruchomić:

```text
EFI/BOOT/BOOTX64.EFI
  -> kernel.elf
  -> install.pkg
  -> Red Flux Setup
```

---

## Instalacja

Po starcie ISO:

1. Wybierz `Install KuroganeOS`.
2. Wybierz język.
3. Ustaw konto.
4. Wybierz pusty dysk VDI.
5. Sprawdź model i rozmiar jeszcze raz.
6. Wpisz `INSTALL`.
7. Poczekaj na zakończenie verification.
8. Wyłącz VM.
9. Odłącz ISO.
10. Uruchom ponownie z HDD.

Po instalacji boot order może być:

```text
1. Hard Disk
2. Optical
```

---

## Diagnostyka

### `No bootable medium` / `No bootable drive`

Najczęstsze przyczyny:

- VM działa w BIOS zamiast EFI;
- ISO nie jest podpięte do Optical Drive;
- Optical/DVD nie występuje w boot order;
- ISO zostało skopiowane niekompletnie;
- użytkownik próbuje bootować `.img` jako DVD;
- build ISO zakończył się przed krokiem verification;
- Secure Boot blokuje niepodpisany loader deweloperski.

Dla 3.3.1-dev builder release nie powinien publikować ISO, które nie przejdzie
weryfikatora struktury UEFI.

### System nie widzi dysku do instalacji

Dysk powinien być podpięty przez:

```text
SATA Controller / Intel AHCI
```

Nie używaj NVMe jako jedynego dysku instalacyjnego w bieżącej DEV BETA.

### Brak internetu

Sprawdź:

```text
Attached to = NAT
Adapter Type = Intel PRO/1000 MT Desktop (82540EM)
Cable Connected = ON
```

KuroganeOS ma sterownik urządzenia PCI `8086:100E` używany przez 82540EM.
Jeżeli DHCP/NAT chwilowo nie odpowiada, 3.3.1-dev uruchamia desktop z loopback
zamiast zatrzymywać cały boot.

### Brak dźwięku

Sprawdź:

```text
Enable Audio = ON
Controller = Intel AC'97
Audio Output = ON
```

Sterownik 3.3.1-dev targetuje emulowany kontroler Intel ICH AC'97 `8086:2415`.
W tej wersji jest to kernelowy backend PCM; stabilne publiczne API playback dla
programów Ring-3 nie jest jeszcze ukończone.

---

## Weryfikacja ISO po buildzie

Builder wywołuje:

```bash
bash ./scripts/verify-virtualbox-iso.sh \
  ./dist/KuroganeOS-3.3.1-dev-x86_64.iso \
  --passes 20
```

Każdy pass sprawdza między innymi:

- czy ISO istnieje i nie jest puste;
- czy xorriso potrafi odczytać obraz;
- czy istnieje wpis El Torito EFI;
- czy istnieje GPT i EFI System Partition;
- czy istnieje `/efiboot.img`;
- czy ISO zawiera loader/kernel/package;
- czy `efiboot.img` ma mniej niż 65535 sektorów po 512 B;
- czy `efiboot.img` przechodzi `fsck.fat`/`dosfsck`;
- czy FAT16 zawiera `EFI/BOOT/BOOTX64.EFI`, kernel i `install.pkg`;
- czy loader ma `MZ` + `PE\0\0`, PE32+, AMD64 i subsystem EFI application;
- czy zewnętrzna i wewnętrzna kopia `BOOTX64.EFI` są identyczne;
- czy SHA-256 ISO nie zmienia się pomiędzy passami.

Jeżeli `VBoxManage` jest dostępny, można dodatkowo wykonać realny smoke boot:

```bash
bash ./scripts/verify-virtualbox-iso.sh \
  ./dist/KuroganeOS-3.3.1-dev-x86_64.iso \
  --passes 20 \
  --virtualbox-smoke
```

Na Windows preferowana jest integracja z media buildem:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild `
  -VirtualBoxSmoke
```

Smoke tworzy tymczasową VM, ustawia EFI, SATA/AHCI, E1000 82540EM i AC'97,
podpina ISO, uruchamia VM i uznaje test za PASS dopiero po pojawieniu się markera
kernela na COM1. Tymczasowa VM jest później usuwana.

---

## Referencyjne ustawienia VBoxManage

VirtualBox 7.x pozwala skonfigurować te same elementy z CLI. Referencyjny profil
KuroganeOS wykorzystuje m.in.:

```text
firmware = efi64
audio controller = ac97
network adapter = 82540EM
network attachment = NAT
boot1 = dvd
boot2 = disk
SATA controller = IntelAHCI
```

Repozytorium zawiera helper tworzący VM:

```text
scripts/create-virtualbox-vm.sh
scripts/create-virtualbox-vm.ps1
```

Użycie i wymagane argumenty są opisane przez `--help` / parametry skryptu.
