# VirtualBox — Flux Modern 8

Host x86-64 z Windows 11, Oracle VirtualBox, Git, cross toolchain i WSL.
[Przygotowanie builda](BUILDING.md). Wersja runtime: `7.0.0-dev`.

## Świeże media

Z katalogu repozytorium:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-flux8-virtualbox.ps1
```

Helper przełącza na `codex/road-to-15-stability`, pobiera zmiany przez
fast-forward, buduje release ISO i wypisuje jego SHA256. Nie wykonuje resetu
Git ani usuwania VDI. Przed ponownym uruchomieniem wyłącz VM normalnie.
`-SkipGitUpdate` zachowuje lokalną rewizję, `-NoBuild` pomija kompilację;
nie używaj starego obrazu jako dowodu naprawy nowego kodu.

Profil generuje `scripts/create-virtualbox-vm.ps1`; ten skrypt jest źródłem
ustawień EFI, dysku, NIC, grafiki i input. Nie instaluj Guest Additions.
Do testu instalacji używaj osobnego, pustego VDI. Potwierdzenie `INSTALL`
kasuje wybrany dysk; nie stosuj reinstalacji jako automatycznej naprawy danych.

## Diagnostyka i akceptacja

Profil wejścia VM musi pozostawać spójny z kontraktem: `PS/2 Mouse` jest
używany jako profil myszy zgodny z BIOS/PS/2, a `USB Tablet` jest zachowany
jako alternatywny urządzenie dla płynnego pointera i inputu z hosta.

```powershell
.\scripts\check-flux8-ui.ps1 -Name 'KuroganeOS-Flux8-Modern'
```

Log znajduje się w `$HOME\VirtualBox VMs\<nazwa>\kurogane-serial.log`.
Zapisz nazwę VM, SHA kodu, hash ISO oraz pełny log od bootu, nie tylko końcówkę.
Sukces spawn/liveness nie dowodzi poprawnej odpowiedzi accountd ani widocznego
okna. Sprawdź Login → Home, rzeczywiste Files/Terminal oraz input, focus,
minimalizację, przywracanie, resize, wheel i recovery.

## Znany incydent zainstalowanego systemu

Stary obraz zgłaszał restarty usług i `flux_account_lookup: FAIL`.
Od `9c8700c` Windows buduje 33 zamiast 11 programów, w tym accountd/sessiond.
To potwierdzona naprawa buildera; pełna przyczyna incydentu wymaga nowego testu.
Dołączenie nowego ISO **nie aktualizuje automatycznie zainstalowanego rootfs**.
Zachowaj istniejący dysk i dane; porównaj zachowanie świeżego Try/live
z instalacją. Nie kasuj kont ani VDI w celu ukrycia błędu.

QEMU PASS nie zastępuje wymaganej interaktywnej akceptacji VirtualBox.
[Kontrakt ukończenia](roadmap/FLUX8_MODERN_COMPLETION.md) i
[stan dowodów](../DEVELOPMENT_STATE.md).
