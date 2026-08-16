# Instalacja KuroganeOS 3.3.0-dev — DEV BETA

KuroganeOS 3.3 używa jednego modelu nośnika dla IMG i ISO:

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

## Budowanie IMG + ISO

### Windows 11 + WSL

Najpierw wymagany jest dodatkowy toolchain:

https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing

Paczka ma zostać wypakowana do głównego katalogu repozytorium z zachowaniem
`tools/compiler/x86_64-elf/bin/`.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-media.ps1 -Configuration release -Rebuild
```

### macOS

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

### Linux x86-64

```bash
bash ./scripts/setup-linux.sh --install
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

## Wyniki

IMG zależy od hosta:

```text
dist/KuroganeOS-3.3.0-dev-windows-qemu.img
dist/KuroganeOS-3.3.0-dev-macos-qemu.img
dist/KuroganeOS-3.3.0-dev-linux-qemu.img
```

ISO:

```text
dist/KuroganeOS-3.3.0-dev-x86_64.iso
dist/SHA256SUMS.txt
```

IMG i ISO zawierają `install.pkg`, dlatego oba powinny wyświetlić Red Flux
Setup z wyborem Try/Install.

## QEMU — bezpieczny test instalatora

Najlepiej bootować nośnik instalacyjny i dodać **osobny pusty dysk SATA/AHCI**
jako target. Nie instaluj na nośniku, z którego aktualnie bootujesz.

Przykładowy układ VM:

```text
boot: KuroganeOS-3.3.0-dev-x86_64.iso
SATA target: empty 512 MiB+ disk
UEFI: enabled
RAM: 768 MiB+
```

Po zakończeniu instalacji:

1. wyłącz VM;
2. odłącz ISO/IMG instalacyjne;
3. pozostaw zainstalowany dysk jako UEFI boot disk;
4. uruchom ponownie;
5. Login powinien użyć języka, username i trybu hasła wybranego w instalatorze.

## VirtualBox

1. Utwórz maszynę x86-64 z EFI/UEFI.
2. Dodaj osobny pusty VDI przez kontroler SATA/AHCI.
3. Podłącz wersjonowany ISO jako napęd optyczny.
4. Uruchom `Install KuroganeOS`.
5. Zweryfikuj model/rozmiar dysku przed wpisaniem `INSTALL`.
6. Po `INSTALL COMPLETE` wyłącz VM i odłącz ISO.
7. Bootuj z VDI.

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

## Markery testowe 3.3

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

## Stan walidacji

3.3 jest DEV BETA. Kod nie powinien być opisywany jako runtime-verified na
Windows/macOS/Linux, dopóki świeże IMG/ISO i przynajmniej jedna pełna instalacja
`media -> target disk -> reboot -> Login` nie zostaną sprawdzone na danym
środowisku.
