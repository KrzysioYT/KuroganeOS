# Instalacja KuroganeOS 3.3.1-dev — DEV BETA

Jeżeli nie znasz VirtualBox/UEFI, najpierw przeczytaj:

- [`START_HERE.md`](START_HERE.md)
- [`VIRTUALBOX.md`](VIRTUALBOX.md)

KuroganeOS 3.3.1 używa jednego modelu nośnika dla IMG i ISO:

```text
boot media
  -> Red Flux Setup
     -> Try KuroganeOS
     -> Install KuroganeOS
```

> [!WARNING]
> Ścieżka `Install KuroganeOS` zapisuje GPT i formatuje wybrany dysk. Testuj ją
> wyłącznie na pustym wirtualnym dysku lub nośniku przeznaczonym do skasowania.
> Pierwszy destrukcyjny krok następuje dopiero po wpisaniu dokładnego `INSTALL`.

## Try KuroganeOS

Try uruchamia system bez instalacji. `install.pkg` jest montowany jako read-only
live root, z którego uruchamiane są `/system/init`, Login, Red Flux Home i
aplikacje Ring 3.

Sesja live ma służyć do poznania/testowania systemu. Root live jest tylko do
odczytu i nie zapewnia persistence zmian po restarcie.

## Install KuroganeOS

Wizard prowadzi przez:

1. `English` / `Polski`;
2. nazwę lokalnego użytkownika;
3. konto bez hasła albo z hasłem;
4. wybór dysku SATA/AHCI;
5. wpisanie `INSTALL`;
6. protective MBR + primary/backup GPT;
7. ESP FAT32 i Kurogane Root FAT32;
8. kopiowanie bootloadera, kernela i userspace;
9. zapis `/etc/locale.cfg`, `/etc/user.cfg` i `/etc/first.run`;
10. verification i flush.

W DEV BETA mechanizm hasła używa tymczasowego `FNV1A64-DEV`. Nie jest to
produkcyjny credential store ani kryptograficzny password KDF. Użyj wyłącznie
hasła testowego, którego nie stosujesz w innych usługach.

## Dlaczego ISO 3.3.1 różni się od wcześniejszego

3.3.1 naprawia istotny problem formatu nośnika optycznego. Wcześniejszy builder
używał 64 MiB EFI boot image, co przekracza 16-bitowy zakres liczby sektorów
El Torito EFI. Nowy builder używa:

```text
El Torito platform: EFI / 0xEF
boot image:         efiboot.img
filesystem:         FAT16
size:               30 MiB
512-byte sectors:   61440 (<65535)
UEFI path:          EFI/BOOT/BOOTX64.EFI
GPT:                EFI System Partition
```

Zainstalowany system nadal korzysta z własnego ESP FAT32 + Kurogane Root
FAT32. FAT16 jest używany tylko jako mały obraz bootowy nośnika ISO.

Każde ISO przechodzi obowiązkową 20-pass walidację struktury przed
opublikowaniem do `dist/`.

## Budowanie IMG + ISO

### Windows 11 + WSL

Najpierw wymagany jest dodatkowy toolchain:

https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing

Paczka ma zostać wypakowana do głównego katalogu repozytorium z zachowaniem:

```text
tools/compiler/x86_64-elf/bin/
```

Standardowy build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild
```

Najmocniejsza kwalifikacja na Windows x86-64 z zainstalowanym VirtualBox:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild `
  -VirtualBoxSmoke
```

`-VirtualBoxSmoke` tworzy tymczasową VM EFI, podpina ISO jako DVD i uznaje test
za PASS dopiero gdy KuroganeOS faktycznie wyemituje marker kernela przez COM1.

### macOS

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-macos.sh --install
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

Na Apple Silicon uruchamiaj gościa x86-64 przez QEMU/TCG.

### Linux x86-64

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-linux.sh --install
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

## Wyniki

IMG zależy od hosta:

```text
dist/KuroganeOS-3.3.1-dev-windows-qemu.img
dist/KuroganeOS-3.3.1-dev-macos-qemu.img
dist/KuroganeOS-3.3.1-dev-linux-qemu.img
```

ISO:

```text
dist/KuroganeOS-3.3.1-dev-x86_64.iso
dist/SHA256SUMS.txt
```

IMG i ISO zawierają `install.pkg`, dlatego oba powinny wyświetlić Red Flux
Setup z wyborem Try/Install.

## VirtualBox — referencyjna VM

Ustaw:

```text
Firmware:       EFI / UEFI
Secure Boot:    OFF
RAM:            1024 MiB
CPU:            1-2
DVD:            KuroganeOS-3.3.1-dev-x86_64.iso
Boot order:     DVD -> HDD
HDD:            SATA / Intel AHCI
Network:        NAT
NIC:            Intel PRO/1000 MT Desktop (82540EM)
Audio:          Intel AC'97
Input:          PS/2
```

Możesz użyć helpera:

Linux/macOS na zgodnym hoście x86-64:

```bash
bash ./scripts/create-virtualbox-vm.sh \
  --iso ./dist/KuroganeOS-3.3.1-dev-x86_64.iso
```

Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\create-virtualbox-vm.ps1 `
  -Iso .\dist\KuroganeOS-3.3.1-dev-x86_64.iso
```

## Instalacja w VirtualBox krok po kroku

1. Uruchom VM z ISO.
2. Wybierz `Install KuroganeOS`.
3. Wybierz język.
4. Wybierz username i tryb hasła.
5. Wybierz **pusty VDI podpięty przez SATA/AHCI**.
6. Sprawdź model i rozmiar dysku.
7. Wpisz `INSTALL`.
8. Poczekaj na `installer_complete: PASS` / komunikat końcowy.
9. Wyłącz VM.
10. Odłącz ISO z napędu optycznego.
11. Ustaw HDD jako pierwszy boot device.
12. Uruchom VM ponownie.
13. Login powinien użyć języka, username i trybu hasła z instalatora.

## Co instaluje system

Między innymi:

```text
/EFI/BOOT/BOOTX64.EFI
/boot/kernel.elf
/system/init
/apps/*
/gui/*
/etc/system.cfg
/etc/locale.cfg
/etc/user.cfg
/etc/first.run
```

## Markery testowe

Try:

```text
[TEST] live_package_root: PASS
[TEST] setup_try_mode: PASS
[TEST] live_login_profile: PASS
```

Install:

```text
[TEST] installer_gpt: PASS
[TEST] installer_filesystems: PASS
[TEST] installer_uefi_bootloader: PASS
[TEST] installer_profile: PASS
[TEST] installer_complete: PASS
```

Zainstalowany profil:

```text
[TEST] installed_account_profile: PASS
[TEST] installed_login_password: PASS
```

Ostatni marker występuje tylko po poprawnym uwierzytelnieniu konta chronionego
hasłem.

## ISO validation

Ręcznie:

```bash
bash ./scripts/verify-virtualbox-iso.sh \
  ./dist/KuroganeOS-3.3.1-dev-x86_64.iso \
  --passes 20
```

Verifier kontroluje m.in. El Torito EFI, GPT ESP, limit sektorów boot image,
FAT, `EFI/BOOT/BOOTX64.EFI`, PE AMD64, kernel, `install.pkg` i stabilny SHA-256.

GitHub Actions wykonuje ponadto niezależny optical UEFI smoke przez OVMF/QEMU.
Prawdziwy VirtualBox smoke jest dostępny na hostach x86-64 przez opisany wyżej
`-VirtualBoxSmoke`.

## Stan walidacji

`3.3.1-dev` jest DEV BETA. Obowiązkowa weryfikacja struktury ISO jest częścią
buildera. Pełne określenie "VirtualBox runtime PASS" wymaga realnego smoke na
x86-64 VirtualBox oraz pełnego testu `ISO -> Install -> HDD -> reboot -> Login`.
