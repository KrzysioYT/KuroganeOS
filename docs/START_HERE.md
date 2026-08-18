# KuroganeOS — START HERE

> Jeśli pierwszy raz uruchamiasz KuroganeOS i nie wiesz czym są UEFI, GPT, AHCI
> albo QEMU, zacznij **tutaj**. Nie musisz znać programowania.

KuroganeOS 3.3.3-dev jest systemem x86-64 w fazie **DEV BETA**. Najbezpieczniej
uruchamiać go w maszynie wirtualnej. Nie instaluj go na prawdziwym dysku z
ważnymi danymi.

---

## 1. Chcę tylko zobaczyć system

Najprostsza droga:

1. Zbuduj albo pobierz plik `KuroganeOS-3.3.3-dev-x86_64.iso`.
2. Utwórz nową maszynę w VirtualBox na komputerze Intel/AMD x86-64.
3. Włącz **EFI/UEFI** — oficjalne ISO KuroganeOS jest UEFI-only.
4. Ustaw ISO jako napęd optyczny.
5. Ustaw boot order `DVD/Optical -> Hard Disk`.
6. Uruchom VM.
7. Na ekranie KuroganeOS wybierz **Try KuroganeOS**.
8. System uruchomi sesję live bez instalowania na dysku.

Jeżeli używasz Maca z Apple Silicon (M1/M2/M3/M4), użyj QEMU. KuroganeOS jest
systemem x86-64 i nie jest systemem ARM64.

Szczegółowa instrukcja VirtualBox: [`VIRTUALBOX.md`](VIRTUALBOX.md).

---

## 2. Chcę zainstalować KuroganeOS w VirtualBox

### Potrzebujesz

- VirtualBox;
- ISO KuroganeOS;
- pusty wirtualny dysk minimum 1 GiB;
- około 1 GiB RAM dla VM.

### Bezpieczna konfiguracja

Ustaw:

```text
Firmware:       UEFI / EFI64
RAM:            1024 MiB
CPU:            1 lub 2
Graphics:       standardowy kontroler VirtualBox
Disk:           SATA / Intel AHCI
Optical drive:  KuroganeOS ISO
Boot order:     DVD -> Disk
Network:        NAT
NIC model:      Intel PRO/1000 MT Desktop (82540EM)
Audio:          Intel AC'97
Keyboard:       PS/2
Mouse:          PS/2
Secure Boot:    OFF
```

Następnie:

1. Start VM.
2. Wybierz `Install KuroganeOS`.
3. Wybierz język `English` albo `Polski`.
4. Podaj nazwę użytkownika.
5. Wybierz konto bez hasła albo ustaw hasło testowe.
6. Wybierz **wyłącznie pusty wirtualny dysk**.
7. Instalator pokaże ostrzeżenie.
8. Aby rozpocząć kasowanie/partycjonowanie wybranego dysku, wpisz dokładnie:

```text
INSTALL
```

9. Poczekaj na komunikat o zakończeniu instalacji.
10. Wyłącz VM.
11. Odłącz ISO od napędu optycznego.
12. Uruchom VM ponownie.
13. System powinien wystartować z wirtualnego HDD i pokazać Login.

> **UWAGA:** słowo `INSTALL` jest ostatnim zabezpieczeniem przed destrukcyjnym
> zapisem. Nigdy nie wybieraj dysku z ważnymi danymi.

---

## 3. VirtualBox pokazuje `No bootable medium`, `No bootable drive` albo podobny błąd

KuroganeOS nie ma obecnie legacy-BIOS bootloadera. Jeśli VM działa z BIOS-em,
VirtualBox nie ma poprawnej ścieżki startu z oficjalnego ISO.

Nie zmieniaj losowo ustawień. Sprawdź po kolei:

1. Czy używasz pliku `.iso`, a nie `.img`?
2. Czy ISO jest faktycznie podpięte do napędu optycznego VM?
3. Czy `Settings -> System -> EFI/UEFI` jest włączone?
4. Czy Secure Boot jest wyłączony?
5. Czy `DVD/Optical` jest przed `Hard Disk` w boot order?
6. Czy ISO ma nazwę odpowiadającą aktualnej wersji, np.:

```text
KuroganeOS-3.3.3-dev-x86_64.iso
```

7. Czy build zakończył się komunikatem `VIRTUALBOX ISO VERIFIED`?
8. Czy plik ISO nie ma rozmiaru 0 B i czy jego SHA-256 zgadza się z
   `dist/SHA256SUMS.txt`?

### Windows — automatyczna naprawa istniejącej VM

Wyłącz maszynę i uruchom z katalogu repo:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\repair-virtualbox-boot.ps1 `
  -Name "KuroganeOS 3.3.3-dev" `
  -Iso .\dist\KuroganeOS-3.3.3-dev-x86_64.iso
```

Skrypt wymusza `EFI64`, I/O APIC, `DVD -> Disk` i ponownie podpina ISO.

Pełna diagnostyka: [`VIRTUALBOX.md`](VIRTUALBOX.md).

---

## 4. Chcę tylko zbudować system

### Windows 11 + WSL

Windows wymaga dodatkowego toolchainu, którego nie ma w repozytorium.

Pobierz:

https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing

Wypakuj zawartość do katalogu głównego repozytorium. Musi istnieć m.in.:

```text
tools/compiler/x86_64-elf/bin/
```

Potem uruchom PowerShell w katalogu repo:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-media.ps1 -Configuration release -Rebuild
```

### macOS

Pierwszy raz:

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-macos.sh --install
```

Build:

```bash
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

Na Apple Silicon do uruchamiania systemu używaj QEMU.

### Linux x86-64

Pierwszy raz:

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-linux.sh --install
```

Build:

```bash
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

### Wynik

Po poprawnym buildzie w `dist/` powinny znaleźć się co najmniej:

```text
KuroganeOS-3.3.3-dev-<host>-qemu.img
KuroganeOS-3.3.3-dev-x86_64.iso
SHA256SUMS.txt
```

ISO jest artefaktem do VirtualBox. IMG jest artefaktem przede wszystkim do QEMU.

---

## 5. Internet w VirtualBox

Najbezpieczniejszy profil zgodności:

```text
Attached to: NAT
Adapter Type: Intel PRO/1000 MT Desktop (82540EM)
Cable Connected: ON
```

Kernel ma obecnie backendy VirtIO-net, E1000 i PCnet oraz wspólny stos
Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS, aktywnego klienta TCP i rozwijaną warstwę
HTTP/HTTPS/TLS. E1000 pozostaje domyślnym profilem VirtualBox do czasu pełnej
kwalifikacji VirtIO na realnym hoście VirtualBox x86-64.

Dokumentacja: [`NETWORKING.md`](NETWORKING.md).

---

## 6. Dźwięk w VirtualBox

Ustaw:

```text
Enable Audio: ON
Audio Controller: Intel AC'97
Audio Output: ON
```

KuroganeOS 3.3.3-dev posiada własny sterownik Intel ICH AC'97 przeznaczony m.in.
dla sprzętu emulowanego przez VirtualBox.

Dokumentacja: [`AUDIO.md`](AUDIO.md).

---

## 7. Chcę napisać program dla KuroganeOS

Nie zaczynaj od kernela.

Najpierw przeczytaj:

1. [`DEVELOPERS/README.md`](DEVELOPERS/README.md)
2. [`DEVELOPERS/APP_DEVELOPMENT.md`](DEVELOPERS/APP_DEVELOPMENT.md)
3. [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)
4. [`DEVELOPERS/GUI_APPLICATIONS.md`](DEVELOPERS/GUI_APPLICATIONS.md)

Najprostsza aplikacja może być napisana w C albo C++ i skompilowana przez SDK
KuroganeOS jako ELF64 Ring-3.

---

## 8. Chcę rozwijać kernel/system

Przeczytaj:

- [`DEVELOPERS/KERNEL_CONTRIBUTION.md`](DEVELOPERS/KERNEL_CONTRIBUTION.md)
- [`ARCHITECTURE.md`](ARCHITECTURE.md)
- [`GUI.md`](GUI.md)
- [`INSTALLATION.md`](INSTALLATION.md)

Zasada projektu: aplikacja użytkownika nie może dostawać nieograniczonego
wejścia do Ring-0 tylko po to, żeby łatwiej zaimplementować funkcję.

---

## 9. Czego 3.3.3-dev jeszcze NIE obiecuje

DEV BETA nie oznacza gotowego zamiennika Windows/macOS/Linux.

W szczególności:

- Direct3D/DirectX nie jest jeszcze kompletnym runtime Windows;
- brak pełnego GPU acceleration stack;
- Guest Additions VirtualBox nie są dostępne dla KuroganeOS;
- nie każdy prawdziwy kontroler audio/sieci/storage jest obsługiwany;
- hardware installation poza kontrolowanym testem pozostaje eksperymentalna.

Status API graficznego i plan kompatybilności DirectX:
[`GRAPHICS_COMPATIBILITY.md`](GRAPHICS_COMPATIBILITY.md).

---

## 10. Gdzie zgłosić błąd

Przy zgłoszeniu podaj:

```text
KuroganeOS version:
Host OS:
QEMU / VirtualBox version:
CPU architecture hosta:
IMG czy ISO:
Co wybrałeś: Try / Install:
Co miało się stać:
Co się stało:
Ostatnie linie serial log:
Screenshot:
```

Im dokładniejszy raport, tym szybciej można odtworzyć błąd.
