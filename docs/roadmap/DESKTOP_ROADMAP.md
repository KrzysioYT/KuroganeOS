# KuroganeOS Desktop Roadmap — 2.3 → 3.6

Ta roadmapa opisuje rzeczywisty stan rozwoju Kurogane Desktop. `3.0` wprowadził
normalny model sesji, `3.1` ustabilizował Red Flux i interakcję, a `3.2`
przebudowuje całość w desktop shell z boot splash, login/session gate i
systemowym dockiem. Celem `3.6` pozostaje stabilny desktop, którego regularne
używanie nie wymaga znajomości wewnętrznych mechanizmów kernela.

## Fundament

- [x] x86-64 UEFI i boot protocol v3;
- [x] Ring 3, prywatne address spaces i `int 0x80` ABI;
- [x] ELF64 processes, PID/TID, spawn/wait/exit i preempcja PIT;
- [x] `/system/init` jako PID 1;
- [x] AHCI, GPT i persistent FAT32/VFS;
- [x] PS/2 keyboard/mouse i wspólna kolejka input;
- [x] framebuffer/software rendering;
- [x] WindowManager: focus, z-order, drag, resize, minimize/maximize/restore/close;
- [x] Ring-3 `libui` scene/view runtime;
- [x] Windows/WSL/macOS SDK i development IMG workflow;
- [x] jeden właściciel GOP podczas desktop session: Flux renderuje ekran, logi idą serialem;
- [x] software full-frame backbuffer i clipping content area;
- [x] PID1 + userspace session lifecycle.

## 2.3 — Desktop Boot Repair — ZREALIZOWANE

- [x] kernelowy `flux-session`;
- [x] normalny boot kończy się sesją graficzną;
- [x] Safe Mode pozostaje konsolą awaryjną;
- [x] PID1 i userspace desktop path;
- [x] runtime markers dla startu sesji.

## 2.4 — Flux Window Core — ZREALIZOWANE

- [x] geometryczne Flux controls;
- [x] focus/drag/minimize/maximize/restore/close;
- [x] workspace/chrome geometry jako wspólne źródło hit-testu;
- [x] software cursor;
- [x] późniejszy 3.0.1 usuwa błędny content-only repaint powodujący ghosting.

## 2.5 — Flux UI Runtime — ZREALIZOWANE JAKO WARSTWA KOMPATYBILNOŚCI

- [x] `kui_scene`, `kui_view`, parent ID i stabilne view ID;
- [x] `kui_flow`;
- [x] panel/label/button/input/list/progress/separator;
- [x] selection traversal i logiczne scrollowanie;
- [x] migracja głównych desktop apps;
- [x] terminal kernelowy przechodzi w serial-only przy aktywnym Flux.

## 2.6 — Desktop Applications — PIERWSZY ETAP ZREALIZOWANY

- [x] Terminal: filesystem read, process launch, jobs, wait, history, status;
- [x] Files: quick access, VFS preview i uruchamianie ELF GUI;
- [x] System Monitor jako żywa aplikacja Ring 3;
- [x] Settings i About jako aplikacje scenowe;
- [x] kernel bounce buffer dla persistent VFS reads do procesu Ring 3.

Pełne `ls/stat/readdir/write/mkdir/rm/mv/cp/touch` wymaga rozszerzonego publicznego
VFS capability ABI.

## 2.7 — Interaction Core — ZREALIZOWANY

- [x] focus i z-order;
- [x] header drag;
- [x] interactive resize;
- [x] minimize/maximize/restore/close;
- [x] Alt+Tab i Alt+F4;
- [x] software pointer;
- [x] named public GUI key codes;
- [x] arrow-first navigation w Launcher/Files/Settings;
- [x] Left/Right/Home/End/Delete oraz Up/Down history w Terminalu;
- [x] Tab jako podstawowy focus/navigation key w aplikacjach scenowych.

Dalsze prace:

- [ ] double click i context actions;
- [ ] wheel routing do userspace;
- [ ] clipboard;
- [ ] natywne keyboard focus traversal między widgetami.

## 2.8 — Flux Launcher — ZREALIZOWANE

- [x] `/gui/launcher` jako Home/session root;
- [x] Terminal/Files/Monitor/Settings/About startują na żądanie;
- [x] aplikacje są dziećmi Launchera i są reapowane po zakończeniu;
- [x] zamknięcie aplikacji naprawdę ją zamyka;
- [x] selection + Enter oraz szybkie skróty aplikacji.

## 2.9 — Desktop Beta — MODEL SESJI ZREALIZOWANY

- [x] boot → PID1 → Flux session → WindowManager;
- [x] zwykłe aplikacje nie są automatycznie respawnowane przez PID1;
- [x] console fallback pozostaje dostępny;
- [x] framebuffer ma jednego właściciela podczas aktywnego GUI;
- [x] desktop nie startuje z pięcioma nakładającymi się oknami.

## 3.0 — Kurogane Desktop — WYDANE

- [x] Desktop jest podstawowym interfejsem normalnego bootu;
- [x] Launcher jest rootem sesji użytkownika;
- [x] Terminal jest aplikacją, nie interfejsem całego OS;
- [x] Files/Monitor/Settings/About startują na żądanie;
- [x] console/safe mode pozostaje awaryjnym subsystemem;
- [x] 3.0.1: deterministyczny repaint usuwa ghost trails;
- [x] 3.0.1: interactive resize jest aktywny.

## 3.1 — Red Flux Interaction Update — WDROŻONE, RUNTIME ACCEPTANCE OTWARTE

- [x] Red Flux: czarne/grafitowe surfaces + czerwony focus/active;
- [x] software full-frame backbuffer dla bieżących GOP do 1600x1200;
- [x] gotowa klatka trafia do GOP dopiero po zakończeniu renderu;
- [x] content clipping per window;
- [x] body text scale limit zależny od content width;
- [x] wspólny `FluxShellCore` dla recovery shell i GUI Terminala;
- [x] wspólne `help/history/jobs/cat/run/gui/calc/...` w obu frontendach;
- [x] GUI Terminal: arrows/history/cursor editing/Home/End/Delete/Escape;
- [x] Red Flux jako domyślna paleta `libui` dla aplikacji SDK;
- [x] prostszy compatibility scene rendering bez `[> ]`, `>>` i `::`.

Pozostała walidacja runtime 3.1 jest wchłonięta przez acceptance 3.2.

## 3.2 — Red Flux Desktop Shell — WDROŻONE, RUNTIME ACCEPTANCE OTWARTE

Docelowy normalny flow 3.2:

```text
UEFI
 -> Red Flux boot
 -> graphical boot splash
 -> kernel / persistent root
 -> /system/init PID 1
 -> /gui/login
 -> /gui/launcher (Red Flux Home)
 -> aplikacje Ring 3
```

Zrealizowane:

- [x] Red Flux jest domyślnym bootem, bez obowiązkowego klawisza `D`;
- [x] `S`/`F8` zachowuje Safe Mode, `X` Diagnostics;
- [x] boot splash z progressem rzeczywistych checkpointów kernela;
- [x] serial zachowuje pełne logi/testy podczas graficznego bootu;
- [x] service-console fallback dla Safe/Diagnostics/Installer/boot failure;
- [x] `/gui/login` jako prawdziwa brama lifecycle sesji;
- [x] PID1 nadzoruje `Login -> Home -> Login`;
- [x] Login obsługuje Enter oraz kliknięcie;
- [x] logout/new login czyści poprzednie userspace surfaces i kończy ich procesy;
- [x] Red Flux Dock zastępuje dawny prosty Pulse Ribbon;
- [x] przypięte Home/Terminal/Files/Monitor/Settings/About;
- [x] własne geometryczne ikony KuroganeOS;
- [x] running/focus state dla dock items;
- [x] klik w uruchomioną aplikację focusuje/przywraca okno;
- [x] klik w nieuruchomioną aplikację uruchamia ją przez Home;
- [x] dynamiczna sekcja żywych okien w Docku;
- [x] session ownership — aplikacje GUI muszą należeć do drzewa Home;
- [x] historyczne anonimowe Ring-0 `/gui/*` launch requests są no-op;
- [x] stare Ring-0 test surfaces nie są renderowane, focusowane ani pokazywane w Docku;
- [x] nowe tło, top identity rail, branding geometry i Red Flux chrome;
- [x] orphan zombie reclamation dla zakończonych sesji.

Runtime acceptance:

- [ ] QEMU/macOS: splash → Login → Home bez ręcznego `D`;
- [ ] Safe Mode/Diagnostics nadal dostępne z UEFI;
- [ ] Dock launch/focus/restore dla wszystkich przypiętych aplikacji;
- [ ] logout i druga sesja bez powrotu starych okien;
- [ ] drag/resize bez ghostingu lub partial-frame flickera;
- [ ] Files/Terminal raw VFS read z `/etc/system.cfg`.

Prawdziwe hasła nie są atrapą w 3.2: account service, credential store i lock
screen przechodzą do następnej warstwy usług.

## 3.3 — Native Flux Compositor + System Services

- natywne application surfaces zamiast `ku_ui_frame` jako głównego transportu;
- widget records i pointer `widget_id`;
- per-window backbuffers;
- damage tracking i clipping regions;
- płynny drag/resize bez pełnego redraw wszystkich aplikacji;
- account/credential service + lock screen;
- userspace settings/notification service;
- IPC/event broker foundation;
- application lifecycle registry;
- publiczne `stat/readdir/write/create/unlink/rename/mkdir/rmdir`;
- architektura pod przyszłe GPU acceleration.

## 3.4 — Connected Desktop

- publiczne userspace network API;
- UDP/TCP sockets i DNS;
- network status service;
- GUI network settings;
- Terminal network tools bez kernela Ring 0;
- dynamiczne app registry i resources dla Home/Dock.

## 3.5 — Developer Platform + Reliability

Docelowy workflow:

```text
kurogane new
kurogane build
kurogane run
kurogane package
```

- stabilniejsze SDK i ABI versioning;
- app manifests/resources;
- generator projektu;
- debug/log API;
- package format;
- crash reports/watchdog;
- storage recovery;
- USB HID/xHCI stabilization;
- więcej display modes;
- ACPI rozszerzenia i przygotowanie NVMe/audio/SMP.

## 3.6 — Flux Stable

Warunki wydania:

- normalny power-on kończy się stabilnym Boot → Login → Red Flux Desktop;
- compositor/input nie powodują artefaktów ani flickera;
- Dock, Home i podstawowe aplikacje tworzą jeden spójny UX;
- prawdziwa account/session service obsługuje login/lock/logout;
- awaria jednej aplikacji nie zabija sesji;
- podstawowe file/process/network capabilities są dostępne w Ring 3;
- build/test/release jest powtarzalny na Windows/WSL i macOS;
- real-hardware qualification obejmuje podstawowy storage/input/display;
- regularne korzystanie z desktopu nie wymaga kernel developer console.
