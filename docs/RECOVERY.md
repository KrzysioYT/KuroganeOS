# Recovery i odporność startu

## Stan działający

Loader oferuje normal boot, safe mode oraz diagnostykę. Safe mode uruchamia Console Mode, pomija PCI i networking, zachowuje memory/self-tests oraz shell. Build chroni working image przed przypadkowym resetem.

## Brakujące Recovery

Docelowe menu: normal boot, safe mode, boot logs, filesystem check/repair, reset konfiguracji, terminal, rollback, shutdown i reboot. Boot Health ma przechowywać `boot_attempts`, `last_boot_success`, `last_panic` i `last_failure_stage` oraz proponować recovery po serii nieudanych startów.

## Aktualizacje transakcyjne

Planowany model to slot A/B: przygotowanie nieaktywnego slotu, weryfikacja, pojedynczy boot próbny i commit dopiero po `BOOT_SUCCESS`; w przeciwnym razie rollback. Nie jest to jeszcze implementacja ani obietnica kompatybilności formatu obrazu.

## Warunki bezpiecznego poweroff

Przed wyłączeniem system musi zatrzymać usługi, zablokować nowe zapisy, wykonać VFS `sync`, opróżnić block cache i odmontować filesystemy. Obecne porty poweroff emulatora nie spełniają tego kryterium.
