# Userspace, ABI i shell

## Najważniejszy fakt

KuroganeOS **nie ma obecnie userspace**. Nie istnieją procesy ring 3, przełączanie kontekstu, syscalle, loader programów, dynamiczne linkowanie, osobne przestrzenie adresowe ani izolacja uprawnień. Nazwy „aplikacja”, „task”, „ABI” i „SDK” opisują przygotowane interfejsy lub kod ring 0, a nie działający model programów użytkownika.

| Element | Gdzie się wykonuje | Stan |
| --- | --- | --- |
| Shell | ring 0, główna pętla kernela | działa interaktywnie |
| Callback schedulera | ring 0, na stosie pętli kernela | działa kooperacyjnie |
| `desktop`/`monitor`/`files`/`about` | ring 0, przez callbacki aplikacji | działają jako pełnoekranowe widoki |
| Program wygenerowany przez SDK | tylko kompilacja obiektu na hoście | nie można go zlinkować ani uruchomić w KuroganeOS |

## Deskryptor ABI

`abi` w shellu pokazuje aktualny `ku_abi_descriptor`:

- wersja ABI 1.0;
- struktura 48 bajtów;
- architektura x86-64;
- rozmiar strony 4096;
- `available_features == 0`;
- `application_transport_available() == false`.

Lista flag w `sdk/include/kurogane/abi.h` jest planowanym słownikiem możliwości. Sama obecność `KU_ABI_FEATURE_PROCESSES`, `FILES`, `GUI` itd. nie oznacza implementacji — kernel nie ustawia żadnej z tych flag.

## Eksperymentalny SDK

`scripts/build-sdk.sh` kopiuje publiczne nagłówki do `build/sdk/sysroot` i kompiluje przykład `abi-inspect` do obiektu. Generator:

```bash
python3 scripts/create-sdk-project.py --list-templates
python3 scripts/create-sdk-project.py --template console --name demo --output /tmp/demo
```

obsługuje szablony `console`, `gui` i `service` jako **compile-only capability probes**. Manifest wygenerowanego projektu jawnie ustawia `runtime_supported: false` oraz `link_supported: false`. Szablon sterownika jest celowo odrzucany, ponieważ nie ma ABI modułów kernela.

Szczegóły nagłówków opisuje również [SDK.md](SDK.md).

## Shell

Shell czyta znaki z bufora klawiatury w głównej pętli kernela. Linia ma maksymalnie 255 znaków, parser przyjmuje do 16 argumentów rozdzielonych białymi znakami i nie implementuje cudzysłowów ani escape sequences. Historia jest ulotnym ringiem 16 linii.

Prompt zawiera kanoniczny CWD:

```text
kurogane:/ $
kurogane:/home $
```

### Informacja i diagnostyka

```text
help clear version uname abi echo date uptime
mem free pci net [ping] tasks history whoami
```

`free` jest aliasem `mem`. `tasks` pokazuje callbacki schedulera, nie procesy systemowe; nazwa `ps` jest zarezerwowana dla przyszłej, prawdziwej tabeli procesów. `whoami` wypisuje `kernel`, co celowo ujawnia brak użytkowników.

### RAMFS

```text
pwd cd <path> ls [path] cat <path> stat <path>
touch <path> mkdir <path> rmdir <path>
write <path> <text> cp <src> <dst> mv <src> <dst>
rm [-r] <path>
```

Ścieżki względne oraz `.`/`..` są obsługiwane przez warstwę CWD shella. Zachowanie i limity opisuje [FILESYSTEM.md](FILESYSTEM.md).

### Aplikacje i pozostałe polecenia

```text
apps
run <app>
gui
calc <number> <+|-|*|/|%> <number>
reboot
poweroff
shutdown
```

`gui` uruchamia `desktop`; `shutdown` jest aliasem `poweroff`. W safe mode lista aplikacji jest pusta, więc `gui` i `run` zgłaszają brak wpisu. `net ping` działa wyłącznie przez loopback i w safe mode zgłasza niezainicjalizowany stos.

## Brakujące funkcje powłoki i procesu

Nie ma:

- uruchamiania plików wykonywalnych ani procesu `init`;
- `fork`/`exec`, wątków, sygnałów, PID i oczekiwania na dziecko;
- potoków, przekierowań, zadań w tle i job control;
- zmiennych środowiskowych, skryptów shellowych, globów i cytowania argumentów;
- użytkowników, grup, logowania i uprawnień;
- prawdziwego `kill`, `mount`, `unmount`, terminali procesowych lub wielu sesji;
- menedżera pakietów i trwałego katalogu domowego.

Planowana kolejność prac znajduje się w [PROJECT_AUDIT.md](PROJECT_AUDIT.md), a bieżące braki w [CURRENT_LIMITATIONS.md](CURRENT_LIMITATIONS.md).
