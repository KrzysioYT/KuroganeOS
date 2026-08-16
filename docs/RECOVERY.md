# Recovery i odporność startu — KuroganeOS 2.1

## Dostępne w 2.1

KuroganeOS 2.1 posiada mechanizmy awaryjne potrzebne do diagnostyki podstawowego bootu:

- normal boot;
- safe mode;
- diagnostics mode;
- awaryjny kernel/console shell w ograniczonych trybach;
- structured boot/fatal logs;
- kontrolowane zatrzymanie przy błędzie required test zamiast fałszywego sukcesu.

Safe mode ogranicza inicjalizację sprzętu, pomija normalną ścieżkę PCI/networking i pozostawia dostęp do poleceń diagnostycznych.

## Czego 2.1 jeszcze nie posiada

Pełny recovery environment **nie jest częścią ukończonego zakresu 2.1**. Nie należy dokumentować poniższych funkcji jako działających:

- automatyczny filesystem check/repair;
- naprawa GPT/FAT32 z poziomu osobnego recovery UI;
- automatyczny reinstall bootloadera z recovery menu;
- rollback systemu;
- sloty A/B;
- reset konfiguracji systemowej;
- persistent Boot Health z automatyczną decyzją o recovery.

Są to kierunki dalszego rozwoju, a nie obecne gwarancje wydania.

## Planowany dalszy model

Docelowy recovery może rozszerzyć menu o boot logs, filesystem check/repair, reinstall `EFI/BOOT/BOOTX64.EFI`, reset konfiguracji, terminal oraz rollback. Boot Health może przechowywać informacje takie jak `boot_attempts`, `last_boot_success`, `last_panic` i `last_failure_stage`.

Planowany model aktualizacji transakcyjnych może wykorzystywać slot A/B: przygotowanie nieaktywnego slotu, weryfikację, pojedynczy boot próbny i commit dopiero po `BOOT_SUCCESS`. Nie jest to jeszcze implementacja ani obietnica kompatybilności formatu obrazu.

## Bezpieczne wyłączanie

Dla trwałego systemu plików prawidłowa ścieżka poweroff powinna przed wyłączeniem zatrzymać zapisy aplikacji/usług, wykonać VFS `sync`, opróżnić warstwę block/storage i dopiero potem zakończyć pracę urządzeń. Obecne porty emulatorowego poweroff nie powinny być traktowane jako pełny produkcyjny shutdown pipeline.
