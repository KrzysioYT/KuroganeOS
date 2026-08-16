# Testowanie KuroganeOS 2.1 w VirtualBox

## Zakres

`scripts/run-virtualbox.ps1` automatyzuje bezpieczny, tymczasowy smoke test UEFI w VirtualBox. Skrypt tworzy własną VM, konfiguruje EFI64 i Intel AHCI, podłącza wygenerowane `kurogane.iso`, kieruje COM1 do pliku i po teście usuwa tylko VM utworzoną przez siebie.

Obecny helper **nie automatyzuje interaktywnego installera**. Dlatego pełnego scenariusza install → remove ISO → boot HDD → persistence nie wolno oznaczać jako automatycznie zweryfikowany tylko dlatego, że smoke test ISO przeszedł.

## Artefakt wejściowy

Kanoniczne wydanie 2.1 powstaje przez:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Configuration release
```

Wynik wydania:

```text
dist/KuroganeOS-2.1-x86_64.iso
dist/SHA256SUMS.txt
```

Ten sam build tworzy lokalny:

```text
kurogane.iso
```

jako compatibility copy dla istniejącego `run-virtualbox.ps1`. Plik jest generowany i nie powinien być commitowany.

## Uruchomienie smoke testu

Wymagany jest Oracle VirtualBox z dostępnym `VBoxManage.exe` na `PATH` albo w standardowym katalogu instalacji.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-virtualbox.ps1
```

Opcjonalnie:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-virtualbox.ps1 -TimeoutSeconds 90 -MemoryMiB 512 -KeepOnFailure
```

`TimeoutSeconds` ma zakres 5–600, a `MemoryMiB` 64–4096.

## Co konfiguruje helper

Skrypt między innymi:

1. weryfikuje podpis ISO-9660 `CD001`;
2. tworzy VM o unikalnej nazwie pod `build/virtualbox/`;
3. włącza EFI64, ICH9 i IO-APIC;
4. ustawia 1 vCPU oraz zadany RAM;
5. konfiguruje PS/2 keyboard i PS/2 mouse;
6. tworzy kontroler SATA typu **IntelAhci**;
7. podłącza nośniki testowe wyłącznie do utworzonej VM;
8. kieruje COM1 16550A do osobnego logu;
9. odrzuca run zawierający fatal/panic/triple-fault lub wymagany test FAIL;
10. sprząta VM po zakończeniu, z kontrolą UUID i ścieżek, aby nie usunąć obcej maszyny.

KuroganeOS 2.1 posiada sterownik myszy PS/2; stare informacje, że mysz nie jest obsługiwana, dotyczą wcześniejszego etapu projektu.

## Logi

Serial znajduje się w:

```text
build/logs/KuroganeOS-vbox-<unikalny-id>-serial.log
```

Przy `-KeepOnFailure` zachowywane są dodatkowe materiały diagnostyczne. Sama tymczasowa VM nadal jest bezpiecznie wyrejestrowywana i usuwana.

## Manual acceptance — instalacja 2.1

Pełny test wydania należy wykonać na świeżej VM:

1. Nowa VM x86-64.
2. EFI enabled.
3. 256–512 MiB RAM.
4. Nowy pusty VDI.
5. Kontroler SATA / Intel AHCI.
6. Podłącz `dist/KuroganeOS-2.1-x86_64.iso` jako DVD.
7. Boot z ISO i uruchom instalator.
8. Wybierz wyłącznie testowy VDI i wykonaj instalację.
9. Po komunikacie o udanej weryfikacji wyłącz VM.
10. Odłącz ISO.
11. Ustaw HDD jako pierwszy nośnik startowy.
12. Bootuj z VDI.
13. Zweryfikuj persistent root oraz `/system/init` jako PID 1.
14. Wykonaj drugi restart i sprawdź persistence.

Oczekiwane markery po bootowaniu z HDD:

```text
[INFO][VFS] persistent FAT32 root mounted read-write
[TEST] userspace_init_spawn: PASS
[INFO][INIT] spawned /system/init as PID 1
[TEST] ALL_REQUIRED_TESTS_PASSED
```

## Czego smoke test nie dowodzi

Sam start ISO w VirtualBox nie jest dowodem na:

- poprawną interaktywną instalację na VDI;
- persistence po restarcie;
- recovery;
- wszystkie opcjonalne ścieżki networking/USB;
- zgodność z rzeczywistym sprzętem UEFI.

Referencyjne automatyczne testy storage/installera pozostają w QEMU. Instrukcja instalacji: [INSTALLATION.md](INSTALLATION.md).
