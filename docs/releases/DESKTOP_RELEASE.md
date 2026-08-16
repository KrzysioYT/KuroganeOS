# KuroganeOS 2.2 — Desktop Developer Preview

KuroganeOS 2.2 jest pierwszym wydaniem, w którym pulpit jest traktowany jako
rzeczywisty **Desktop Developer Preview**, a nie jako obietnica z roadmapy.
Fundament instalowalnego systemu 2.1 pozostaje bez zmian: UEFI, Ring 3, PID 1,
AHCI, GPT, writable FAT32/VFS oraz boot z zainstalowanego dysku.

## Kurogane Flux

2.2 wprowadza własny język wizualny **Kurogane Flux**. Celem nie jest
odtwarzanie Windows, macOS, GNOME, KDE ani innego środowiska desktopowego.

Flux opiera się na:

- ciemnej, grafitowej przestrzeni roboczej bez metafory tapety;
- bocznym `signal spine` zamiast klasycznego menu/start/docka;
- asymetrycznych powierzchniach z krótkimi akcentami i znacznikami narożnymi;
- pływającym `pulse ribbon` w obszarze zarezerwowanym wcześniej dla taskbara;
- segmentowych wskaźnikach stanu zamiast klasycznych progressbarów;
- kolorach sygnałowych: jade, violet i amber;
- zachowaniu mechaniki focus/z-order/drag/minimize/maximize bez kopiowania
  wyglądu kontrolek popularnych desktopów.

Renderer nadal jest lekki i framebufferowy. 2.2 nie udaje jeszcze pełnego
kompozytora GPU.

## Desktop runtime

Tryb `boot=desktop` uruchamia istniejący WindowManager oraz powierzchnie Ring 3.
Dostępne są między innymi:

- Terminal;
- Files;
- System Monitor;
- Settings;
- About.

WindowManager posiada focus, z-order, drag, minimize, maximize/restore, close,
Alt+Tab i Alt+F4. Aplikacje GUI Ring 3 korzystają z syscalli UI, a ich awaria nie
jest zwykłym callbackiem aplikacji w kernelu.

Część starych widoków kernelowych pozostaje jako kod developerski/legacy i nie
jest wzorcem docelowej architektury aplikacji.

## Shell 2.2

Domyślną powłoką po starcie PID 1 jest Ring-3 **Flux Console**. W 2.1 shell został
zredukowany do małego testu ABI, przez co większość komend starego kernel shella
pozostała odcięta. 2.2 naprawia tę regresję po stronie userspace.

Działające przez obecne ABI polecenia obejmują m.in.:

```text
help clear version uname pid whoami status history jobs
pwd cd cat read which
apps run open gui wait
hello external files monitor about
echo calc sleep yield true false exit
mem free tasks pci device driver diskinfo
```

`run app` rozwija nazwę do `/apps/app`, a `run /pełna/ścieżka` wykonuje podany
ELF. `open` i `gui` mogą uruchamiać śledzone zadania w tle, a `jobs`/`wait`
obsługują ich cykl życia, aby zakończone procesy nie były celowo zostawiane jako
zombie przez powłokę.

## Granica capability ABI

Stary kernel shell nadal posiada komendy uprzywilejowane, których nie wolno
udawać w Ring 3 bez odpowiedniego API. Dotyczy to obecnie części operacji VFS,
network diagnostics, czasu platformy i reset/poweroff.

Flux Console rozpoznaje takie polecenia i informuje o braku dedykowanego
capability syscall zamiast zwracać mylące `command not found` albo dodawać
niebezpieczny syscall "wykonaj dowolną komendę kernela".

Docelowo funkcje te będą przenoszone jako małe, jawne ABI capabilities.

## Status wydania

2.2 pozostaje **Developer Preview**. Nie oznacza stabilnego desktopu do
codziennego użycia. Krytyczne fundamenty 2.1 są zachowane, ale nadal brakuje
m.in. pełnego compositora, resize okien, pełnego userspace VFS API, audio,
pełnego recovery oraz szerokiej kwalifikacji na realnym sprzęcie.
