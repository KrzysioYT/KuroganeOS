# Userspace, ABI i Flux Console — KuroganeOS 2.2

## Aktualny model wykonania

KuroganeOS posiada działający userspace x86-64. Dokument zastępuje stary opis
z czasów 1.0, który błędnie twierdził, że Ring 3 i procesy nie istnieją.

Aktualnie zaimplementowane są:

- prywatne przestrzenie adresowe procesów;
- ELF64 ET_EXEC loader;
- `/system/init` jako PID 1;
- procesy i wątki z PID/TID;
- timer preemption;
- syscall gate `int 0x80`;
- spawn/wait/exit;
- read-only file handles;
- pamięć użytkownika alloc/free;
- sleep/yield;
- tworzenie, prezentacja i eventy okien GUI;
- aplikacje Ring 3 budowane tym samym SDK na Windows i macOS.

## Start userspace

Normalny boot:

```text
kernel
  -> /system/init (PID 1)
  -> /apps/shell
  -> Flux Console
```

Tryb desktop dodatkowo uruchamia WindowManager oraz aplikacje `/gui/*`.
Safe mode pozostawia awaryjny kernel developer console i nie jest zwykłą sesją
userspace.

## Syscall ABI

Publiczne numery znajdują się w `sdk/include/kurogane/syscall.h`.
Aktualna powierzchnia obejmuje:

```text
exit write getpid read open close alloc free sleep yield gettid
spawn wait ui_create ui_present ui_poll ui_close
```

`open` jest obecnie read-only. Brak `readdir/stat/write/create/unlink` w ABI jest
świadomym ograniczeniem i powodem, dla którego nie wszystkie stare polecenia
kernel shella są jeszcze dostępne jako pełne Ring-3 implementacje.

## Flux Console 2.2

Prompt:

```text
KRG::/ >
KRG::/apps >
```

Powłoka posiada historię, status ostatniej komendy, logiczny CWD, uruchamianie
ELF, foreground wait i ograniczoną tabelę zadań w tle.

### Workspace

```text
help clear version uname pid whoami status history jobs
pwd cd <path> cat <path> read <path> which <name>
```

`cat/read` korzysta z read-only VFS handle syscall. `cd` utrzymuje logiczny CWD;
do czasu dodania `stat/readdir` userspace nie może jeszcze potwierdzić typu
katalogu tak dokładnie jak kernel developer console.

### Uruchamianie aplikacji

```text
apps
run <name|/path>
open <name|/path>
gui <terminal|files|sysmon|settings|about>
jobs
wait <pid>
```

`run test` oznacza `/apps/test`. `run /system/tool` używa dokładnej ścieżki.
`open`/`gui` rejestrują PID w małej tabeli jobów i shell okresowo wywołuje
`wait`, aby sprzątać zakończone dzieci.

### Utility

```text
echo <text>
calc <a> <+|-|*|/|%> <b>
sleep <ticks>
yield
true
false
exit
```

`calc` posiada kontrolę dzielenia przez zero i signed overflow.

### Skróty diagnostyczne

```text
mem free tasks pci device driver diskinfo
```

W 2.2 prowadzą do Ring-3 System Monitor surface zamiast udawać bezpośredni
Ring-0 odczyt.

## Polecenia jeszcze nieprzeniesione

Stary kernel developer console nadal posiada m.in. pełniejsze VFS mutations,
network diagnostics, RTC/platform i reboot/poweroff. Flux Console rozpoznaje
te nazwy i raportuje brak dedykowanego capability syscall.

Nie dodajemy ogólnego syscalla wykonującego arbitralne polecenie kernela, bo
zniweczyłby izolację Ring 3. Zamiast tego brakujące funkcje będą trafiały do ABI
jako ograniczone capabilities (`readdir`, `stat`, system-info, network, power).

## Kernel developer console

Awaryjny shell Ring 0 nadal istnieje dla safe mode i debugowania kernela.
Posiada bogatszy zestaw niskopoziomowych poleceń. Nie jest to jednak docelowa
powłoka użytkownika i nie należy mieszać jego uprawnień z Flux Console.
