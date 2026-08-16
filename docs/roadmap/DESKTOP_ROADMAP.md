# KuroganeOS Desktop Roadmap — 2.3 → 3.6

Ta roadmapa opisuje rzeczywisty stan rozwoju Kurogane Desktop. `3.0` wprowadził
normalny model sesji desktopowej, a `3.1` rozpoczyna etap jakości interakcji i
własnej identyfikacji Red Flux. Celem `3.6` pozostaje stabilny desktop, którego
regularne używanie nie wymaga znajomości wewnętrznych mechanizmów kernela.

## Fundament

- [x] x86-64 UEFI i boot protocol v3;
- [x] Ring 3, prywatne address spaces i `int 0x80` ABI;
- [x] ELF64 processes, PID/TID, spawn/wait/exit i preempcja PIT;
- [x] `/system/init` jako PID 1;
- [x] AHCI, GPT i persistent FAT32/VFS;
- [x] PS/2 keyboard/mouse i wspólna kolejka input;
- [x] framebuffer/software rendering;
- [x] WindowManager: focus, z-order, drag, resize, minimize/maximize/restore/close;
- [x] Signal Spine i Pulse Ribbon;
- [x] Ring-3 `libui` scene/view runtime;
- [x] Windows/WSL/macOS SDK i development IMG workflow;
- [x] jeden właściciel GOP podczas desktop session: Flux renderuje ekran, logi idą serialem.

## 2.3 — Desktop Boot Repair — ZREALIZOWANE

- [x] kernelowy `flux-session`;
- [x] normalny boot kończy się sesją graficzną;
- [x] Safe Mode pozostaje konsolą awaryjną;
- [x] PID1 i userspace desktop path;
- [x] runtime markers dla startu sesji.

## 2.4 — Flux Window Core — ZREALIZOWANE

- [x] brak klasycznego taskbara w głównym WM;
- [x] geometryczne Flux controls zamiast `- [] X`;
- [x] Signal Spine i Pulse Ribbon;
- [x] focus/drag/minimize/maximize/restore/close;
- [x] workspace/chrome geometry jako wspólne źródło hit-testu;
- [x] software cursor nie wymusza pełnego repaintu;
- [x] 3.0.1 usuwa błędny content-only repaint powodujący ghosting.

Legacy bootloader copy (`DESKTOP ALPHA`) i stare Ring-0 surfaces mogą być
czyszczone niezależnie od userspace desktopu.

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
- [x] interactive resize z bottom-right grip;
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
- [x] PID1 nadzoruje Launcher, a nie pięć niezależnych aplikacji;
- [x] selection + Enter oraz szybkie skróty Terminal/Files.

Manifesty, resources, favourites i dynamiczne registry przechodzą do 3.4.

## 2.9 — Desktop Beta — MODEL SESJI ZREALIZOWANY

- [x] boot → PID1 → Flux session → WindowManager → Launcher;
- [x] zwykłe aplikacje nie są automatycznie respawnowane przez PID1;
- [x] awaria/zamknięcie Launchera powoduje restart root session;
- [x] console fallback pozostaje dostępny, jeśli session root nie wystartuje;
- [x] framebuffer ma jednego właściciela podczas aktywnego GUI;
- [x] desktop nie startuje z pięcioma nakładającymi się oknami.

## 3.0 — Kurogane Desktop — WYDANE

Normalny model systemu:

```text
UEFI
 -> kernel
 -> persistent root
 -> WindowManager
 -> /system/init PID 1
 -> /gui/launcher
 -> aplikacje Ring 3 uruchamiane przez użytkownika
```

- [x] Desktop jest podstawowym interfejsem normalnego bootu;
- [x] Launcher jest rootem sesji użytkownika;
- [x] Terminal jest aplikacją, nie interfejsem całego OS;
- [x] Files/Monitor/Settings/About startują na żądanie;
- [x] console/safe mode pozostaje awaryjnym subsystemem;
- [x] 3.0.1: deterministyczny repaint usuwa ghost trails;
- [x] 3.0.1: interactive resize jest aktywny.

## 3.1 — Red Flux Interaction Update — WDROŻONE, RUNTIME ACCEPTANCE OTWARTE

3.1 zmienia priorytet z developer-preview na stabilność i spójność UX.

- [x] Red Flux: czarne/grafitowe surfaces + czerwony focus/active zgodny z logo;
- [x] usunięcie cyan/violet/amber jako głównej identyfikacji desktopu;
- [x] software full-frame backbuffer dla bieżących GOP do 1600x1200;
- [x] gotowa klatka jest kopiowana do GOP dopiero po zakończeniu renderu;
- [x] content clipping per window;
- [x] body text scale limit zależny od content width;
- [x] wspólny `FluxShellCore` dla recovery shell i GUI Terminala;
- [x] wspólne `help/history/jobs/cat/run/gui/calc/...` w obu frontendach;
- [x] GUI Terminal: arrows/history/cursor editing/Home/End/Delete/Escape;
- [x] Red Flux jako domyślna paleta `libui` również dla aplikacji SDK;
- [x] prostszy compatibility scene rendering bez `[> ]`, `>>` i `::`.

Do zamknięcia 3.1:

- [ ] QEMU/macOS runtime acceptance dla drag i resize bez flickera;
- [ ] potwierdzenie raw VFS read w Files/Terminal (`/etc/system.cfg`);
- [ ] test zachowania backbuffera na wszystkich używanych GOP modes.

## 3.2 — Flux Compositor + System Services

- natywne application surfaces zamiast `ku_ui_frame` jako głównego transportu;
- widget records i pointer `widget_id`;
- per-window backbuffers;
- damage tracking i clipping regions;
- płynny drag/resize bez pełnego redraw wszystkich aplikacji;
- userspace settings/notification service;
- IPC/event broker foundation;
- application lifecycle registry;
- publiczne `stat/readdir/write/create/unlink/rename/mkdir/rmdir`;
- architektura pod przyszłe GPU acceleration.

## 3.3 — Connected Desktop

- publiczne userspace network API;
- UDP/TCP sockets i DNS;
- network status service;
- GUI network settings;
- Terminal network tools bez kernela Ring 0.

## 3.4 — Developer Platform

Docelowy workflow:

```text
kurogane new
kurogane build
kurogane run
kurogane package
```

- stabilniejsze SDK;
- app manifests/resources;
- dynamiczne registry Launchera;
- generator projektu;
- debug/log API;
- package format.

## 3.5 — Hardware & Reliability

- USB HID/xHCI stabilization;
- storage recovery;
- więcej display modes;
- crash reports/watchdog;
- real-hardware qualification;
- ACPI rozszerzenia;
- przygotowanie NVMe/audio/SMP.

## 3.6 — Flux Stable

Warunki wydania:

- normalny power-on kończy się stabilnym Red Flux Desktop;
- compositor/input nie powodują artefaktów ani flickera;
- podstawowe aplikacje są funkcjonalne i mają spójne sterowanie;
- awaria jednej aplikacji nie zabija sesji;
- Launcher/Files/Settings/Terminal/Monitor tworzą jeden spójny UX;
- podstawowe file/process/network capabilities są dostępne w Ring 3;
- build/test/release jest powtarzalny na Windows/WSL i macOS;
- regularne korzystanie z desktopu nie wymaga kernel developer console.
