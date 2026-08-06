# Testowanie w VirtualBox

## Zakres testu

`scripts/run-virtualbox.ps1` automatyzuje headless smoke test gotowego `kurogane.iso`. Potwierdza, że firmware EFI VirtualBox startuje ISO i że serial osiąga stabilny prompt shella. Nie instaluje systemu na VDI, nie testuje trwałości danych i nie wykonuje scenariusza komend klawiaturowych.

## Wymagania

1. Oracle VirtualBox z dostępnym `VBoxManage.exe` — na `PATH` albo w standardowym katalogu instalacji.
2. Aktualne `kurogane.iso`, zbudowane na przykład przez:

```bash
./scripts/build-iso.sh release
```

3. PowerShell uruchomiony z katalogu głównego repozytorium.

## Uruchomienie automatyczne

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-virtualbox.ps1
```

Opcjonalnie:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-virtualbox.ps1 -TimeoutSeconds 90 -MemoryMiB 512 -KeepOnFailure
```

`TimeoutSeconds` ma zakres 5–600 i domyślnie wynosi 45. `MemoryMiB` ma zakres 64–4096 i domyślnie wynosi 256. `-KeepOnFailure` zachowuje kopię diagnostyki, ale maszyna nadal jest wyłączana, wyrejestrowywana i usuwana.

## Co robi skrypt

Skrypt:

1. sprawdza podpis ISO-9660 `CD001`;
2. tworzy maszynę o unikalnej nazwie w `build/virtualbox/`;
3. ustawia EFI64, 1 vCPU, chipset ICH9, IO-APIC, VMSVGA i 16 MiB VRAM;
4. ustawia urządzenia wejściowe maszyny jako PS/2, wyłącza NIC, audio, USB i parawirtualizację;
5. dołącza ISO tylko do odczytu jako napęd optyczny SATA;
6. kieruje COM1 16550A do unikalnego pliku i uruchamia VM headless;
7. odrzuca serial zawierający `fatal:`, panikę, wyjątek, triple fault lub wymagany test `FAIL`;
8. uznaje test po utrzymaniu promptu `kurogane:/ $` przez co najmniej 750 ms;
9. odłącza zewnętrzne ISO, wyłącza i usuwa wyłącznie utworzoną przez siebie VM.

Ustawienie `--mouse ps2` opisuje urządzenie po stronie VirtualBox; KuroganeOS nadal **nie ma sterownika myszy**.

## Logi i diagnostyka

Serial pozostaje w:

```text
build/logs/KuroganeOS-vbox-<unikalny-id>-serial.log
```

Przy błędzie i `-KeepOnFailure` skrypt kopiuje `showvminfo` oraz logi VirtualBox do katalogu `build/logs/<nazwa-vm>-diagnostics/`. Sama VM nie jest zachowywana, ponieważ obowiązkowe sprzątanie chroni hosta przed pozostawianiem zarejestrowanych maszyn testowych.

## Test ręczny

Jeżeli potrzebny jest ekran, utwórz tymczasową maszynę typu 64-bit, włącz EFI, przydziel co najmniej 64 MiB RAM, dołącz `kurogane.iso` jako napęd optyczny i użyj klawiatury PS/2. NIC można wyłączyć — kernel nie ma sterownika karty. Do safe mode naciśnij `S` albo `F8` natychmiast po bannerze loadera.

Nie traktuj testu promptu jako potwierdzenia GUI, wyłączania ACPI, dysku SATA, sieci, SMP ani realnego sprzętu. Zakres testów QEMU jest szerszy i opisany w [QEMU_TESTING.md](QEMU_TESTING.md).
