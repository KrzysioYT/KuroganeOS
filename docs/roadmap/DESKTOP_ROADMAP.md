# KuroganeOS Desktop Roadmap — 2.3 → 3.6

Ta roadmapa opisuje rzeczywisty stan rozwoju Kurogane Flux Desktop. `3.0` jest
pierwszym wydaniem z pełnoprawnym modelem sesji desktopowej: system startuje do
Flux Home/Launchera, a Terminal, Files, Monitor, Settings i About są zwykłymi
procesami uruchamianymi na żądanie. `3.0` nie oznacza jeszcze końca rozwoju
compositora, pełnego POSIX/VFS ABI ani sterowników sprzętowych.

## Fundament

- [x] x86-64 UEFI i boot protocol v3;
- [x] Ring 3, prywatne address spaces i `int 0x80` ABI;
- [x] procesy ELF64, PID/TID, spawn/wait/exit i preempcja PIT;
- [x] `/system/init` jako PID 1;
- [x] AHCI, GPT i persistent FAT32/VFS;
- [x] PS/2 input i wspólna kolejka input;
- [x] framebuffer/software rendering;
- [x] WindowManager: focus, z-order, drag, resize, minimize/maximize/restore/close;
- [x] Signal Spine i Pulse Ribbon;
- [x] Ring-3 `libui` scene/view runtime;
- [x] Windows/WSL/macOS SDK i development IMG workflow;
- [x] rozdzielenie framebuffer ownership: Flux renderuje ekran, logi idą serialem.

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
- [x] 3.0.1 usuwa błędny content-only repaint, który powodował ghosting.

Pozostały cleanup bootloadera (`DESKTOP ALPHA`) i starych Ring-0 surfaces może być
prowadzony niezależnie od userspace desktopu.

## 2.5 — Flux UI Runtime — ZREALIZOWANE JAKO WARSTWA KOMPATYBILNOŚCI

- [x] `kui_scene`, `kui_view`, parent ID i stabilne view ID;
- [x] `kui_flow`;
- [x] panel/label/button/input/list/progress/separator;
- [x] selection traversal i logiczne scrollowanie;
- [x] migracja Files/Settings/Monitor/About;
- [x] 2.5.1: terminal kernelowy przechodzi w serial-only przy aktywnym Flux.

Natywne kernelowe widget records, pointer widget IDs i compositor damage regions
pozostają pracą dla 3.1/3.2.

## 2.6 — Desktop Applications — PIERWSZY ETAP ZREALIZOWANY

- [x] Flux Terminal: `cat/read`, `which`, `run`, `gui`, `open`, jobs, wait,
  history, status i uruchamianie aplikacji;
- [x] Files: quick access, podgląd VFS i uruchamianie ELF GUI;
- [x] System Monitor jako żywa aplikacja Ring 3;
- [x] Settings i About jako aplikacje scenowe;
- [x] kernel bounce buffer dla odczytu persistent VFS do procesu Ring 3.

Pełne `ls/stat/readdir/write/mkdir/rm/mv/cp/touch` wymaga rozszerzonego publicznego
VFS capability ABI i pozostaje następnym subsystemem userspace.

## 2.7 — Interaction Update — RDZEŃ ZREALIZOWANY

Dostępne:

- [x] focus i z-order;
- [x] header drag;
- [x] interactive resize z bottom-right Flux grip;
- [x] minimize/maximize/restore/close;
- [x] Alt+Tab i Alt+F4;
- [x] keyboard selection w `libui`;
- [x] software pointer.

Dalsze prace:

- [ ] double click/context actions;
- [ ] wheel routing do userspace;
- [ ] clipboard;
- [ ] pełny keyboard focus traversal między widgetami.

## 2.8 — Flux Launcher — ZREALIZOWANE

- [x] `/gui/launcher` jako Flux Home;
- [x] Launcher jest jedynym normalnym userspace rootem sesji;
- [x] Terminal/Files/Monitor/Settings/About startują na żądanie;
- [x] aplikacje są dziećmi Launchera i są reapowane po zakończeniu;
- [x] zamknięcie aplikacji naprawdę ją zamyka;
- [x] PID1 nadzoruje Launcher, a nie pięć niezależnych aplikacji;
- [x] szybkie skróty Terminal/Files oraz selection + Enter.

Manifesty, resources, favourites i wyszukiwanie dynamicznego registry przechodzą
do 3.4 Developer Platform.

## 2.9 — Desktop Beta — MODEL SESJI ZREALIZOWANY

- [x] boot → PID1 → Flux session → WindowManager → Launcher;
- [x] zwykłe aplikacje nie są automatycznie respawnowane przez PID1;
- [x] awaria/zamknięcie Launchera powoduje restart root session;
- [x] console fallback pozostaje dostępny, jeśli session root nie wystartuje;
- [x] framebuffer ma jednego właściciela podczas aktywnej sesji GUI;
- [x] desktop nie musi startować z pięcioma nakładającymi się oknami.

Pełny automatyczny crash-survival QEMU test dla arbitralnej aplikacji pozostaje
do dopięcia w torze testowym.

## 3.0 — Kurogane Desktop — WYDANE

Normalny model systemu:

```text
UEFI
 -> kernel
 -> persistent root
 -> Flux Window Core
 -> /system/init PID 1
 -> /gui/launcher (Flux Home)
 -> aplikacje Ring 3 uruchamiane przez użytkownika
```

Zakres 3.0:

- [x] Flux Desktop jest podstawowym interfejsem normalnego bootu;
- [x] Flux Home/Launcher jest rootem sesji użytkownika;
- [x] Terminal jest aplikacją, nie interfejsem całego OS;
- [x] Files, Monitor, Settings i About są uruchamiane na żądanie;
- [x] aplikacje mogą zostać zamknięte bez natychmiastowego restartu przez PID1;
- [x] console/safe mode pozostaje awaryjnym subsystemem;
- [x] Ring-3 persistent reads używają kernel-owned bounce buffer przed kopiowaniem
  do pamięci procesu;
- [x] 3.0.1: deterministyczny repaint usuwa ghost trails z compatibility renderer;
- [x] 3.0.1: interaktywny resize okien jest aktywny.

### Znane ograniczenia 3.0.x

- software framebuffer rendering bez GPU acceleration;
- brak pełnego compositora/backbufferów;
- compatibility `ku_ui_frame` pozostaje transportem `libui`;
- publiczne VFS ABI nadal jest głównie read-only/open/read/close;
- brak pełnego directory browsera opartego o publiczne `readdir`;
- brak clipboard/multimonitor/audio/NVMe;
- tor instalowalnego ISO na macOS nadal wymaga osobnego dopracowania.

## 3.1 — System Services

- userspace settings service;
- notification service;
- event broker / IPC foundation;
- application lifecycle registry;
- rozszerzone file/process/power capabilities;
- publiczne `stat/readdir/write/create/unlink/rename/mkdir/rmdir`.

## 3.2 — Flux Compositor

- application backbuffers;
- double buffering;
- clipping i damage tracking;
- płynny drag/resize bez full repaint;
- software transparency, shadows i depth cues;
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

Cel: dopracowany Flux Desktop nadający się do regularnego testowania bez wiedzy o
wewnętrznych mechanizmach kernela.

Warunki:

- normalny power-on kończy się stabilnym Flux Desktop;
- compositor/input nie powodują artefaktów i flickera;
- podstawowe aplikacje są funkcjonalne;
- awaria jednej aplikacji nie zabija sesji;
- launcher/files/settings/terminal/monitor tworzą spójne UX;
- build/test/release jest powtarzalny na Windows/WSL i macOS.
