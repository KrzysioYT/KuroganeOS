# KuroService Manager

Service Manager będzie procesem userspace uruchamianym przez `/system/init`, nie częścią logiki kernela.

## Odpowiedzialność

- odczyt `/etc/services.conf`;
- zależności i kolejność startu;
- timeout start/stop;
- restart policy z limitem prób i backoff;
- stan `starting/running/degraded/failed/stopped`;
- logowanie przyczyny awarii;
- przejście do console recovery, gdy usługa krytyczna nie startuje.

Awaria usługi opcjonalnej nie może zatrzymać systemu. Usługi muszą mieć jawnych właścicieli zasobów i być sprzątane przy exit.

## Stan

Niezaimplementowane. Wymaga process model, syscalls, VFS, config parsera i KuroLibC. Obecne callbacki schedulera nie są usługami.
