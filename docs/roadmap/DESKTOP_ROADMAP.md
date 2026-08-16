# KuroganeOS Desktop Roadmap — 2.3 → 3.6

Ta roadmapa opisuje faktyczną kolejność rozwoju KuroganeOS po 2.2.x. Zasada jest
prosta: najpierw działająca sesja i API, później wygląd, compositor i polerka.
Warstwa wizualna nie może udawać gotowego subsystemu, którego nie ma.

## Fundament już dostępny

- [x] x86-64 UEFI i boot protocol v3;
- [x] Ring 3, prywatne address spaces i `int 0x80` ABI;
- [x] procesy ELF64, PID/TID, spawn/wait/exit i preempcja PIT;
- [x] `/system/init` jako PID 1;
- [x] AHCI, GPT, persistent writable FAT32/VFS;
- [x] PS/2 keyboard/mouse i wspólna kolejka input;
- [x] framebuffer i software rendering;
- [x] WindowManager: focus, z-order, drag, minimize/maximize/restore/close;
- [x] pierwsze aplikacje GUI Ring 3 i `libui`;
- [x] SDK oraz Windows/WSL/macOS build workflow;
- [x] natywne development IMG na macOS;

## 2.3.0 — Desktop Boot Repair — ZREALIZOWANE

Cel: normalny userspace boot ma kończyć się realną sesją graficzną, a nie tylko
`/apps/shell`.

- [x] kernelowy host sesji `flux-session`;
- [x] automatyczna inicjalizacja WindowManagera przy normalnym userspace boot;
- [x] Safe Mode pozostaje tekstowym trybem awaryjnym;
- [x] PID1 uruchamia `/gui/terminal`, `/gui/files`, `/gui/sysmon`,
  `/gui/settings` i `/gui/about`;
- [x] PID1 nadzoruje i restartuje zakończone aplikacje desktopowe;
- [x] szybka awaria większości GUI przełącza PID1 na console fallback;
- [x] `rootfs/etc/system.cfg` deklaruje `BOOT_MODE=desktop`;
- [x] markery runtime `desktop_session`, `desktop_userspace_apps` i
  `userspace_desktop_session`.

## 2.4 — Flux Window Core — CORE ZREALIZOWANE

Cel: usunąć elementy starego Desktop Alpha z głównego WindowManagera i zbudować
własny język zarządzania oknami Kurogane Flux.

### 2.4.0 / 2.4.1 — wykonane

- [x] usunięcie klasycznego taskbara z głównego WindowManagera;
- [x] usunięcie tekstowych kontrolek `-`, `[]`, `X` z głównej ścieżki WM;
- [x] dynamiczny Signal Spine oparty o realny z-order i focus;
- [x] pływający Pulse Ribbon dla aktywnych/minimalizowanych powierzchni;
- [x] click-to-focus / click-to-restore z Pulse Ribbon;
- [x] geometryczny Flux control rail: minimize / expand / dismiss;
- [x] rozróżnienie focused/background surface;
- [x] jawny `work_area` używany przez maximize i drag clamp;
- [x] wspólne `WorkspaceGeometry` i `ChromeGeometry` dla renderingu i hit-testu;
- [x] wydzielony resize grip jako przygotowanie pod 2.7;
- [x] rozszerzony hosted test WindowManagera;
- [x] 2.4.1 usuwa okresowy pełnoekranowy repaint powodujący miganie w QEMU;
- [x] zwykły `KU_SYS_UI_PRESENT` korzysta z content repaint zamiast wymuszać clear;
- [x] kursor nie wymusza już pełnego repaintu workspace.

### Cleanup równoległy 2.4.x

- [ ] usunąć komunikaty `DESKTOP ALPHA` z bootloadera UEFI;
- [ ] ujednolicić wybór desktop/console/safe/diagnostics na poziomie boot flags;
- [ ] usunąć legacy `ui::taskbar()` z diagnostycznych Ring-0 surfaces;
- [ ] dodać QEMU input smoke test dla Pulse Ribbon i Flux control rail;
- [ ] dopracować problematyczny tor instalowalnego ISO na macOS.

Te zadania mogą być naprawiane jako hotfixy i nie blokują userspace UI runtime.

## 2.5 — Flux UI Runtime — W TRAKCIE

Cel: aplikacje przestają ręcznie składać `ku_ui_frame` jako numerowane linie.

### 2.5.0 — wykonane

- [x] `kui_scene` jako właściciel sceny aplikacji;
- [x] `kui_view` ze stabilnym ID i parent ID;
- [x] parent-child view tree budowane bez cykli przez kolejność insercji;
- [x] pionowy `kui_flow` jako pierwszy layout primitive;
- [x] view types: panel, label, button, input, list item, progress, separator;
- [x] mutacja tekstu, flag i value/maximum;
- [x] selection/focus traversal dla elementów interaktywnych;
- [x] logiczne scrollowanie sceny;
- [x] kompatybilny backend serializujący scenę do istniejącego `ku_ui_frame`;
- [x] migracja Files do scene/list model;
- [x] migracja Settings do scene/button/focus model;
- [x] migracja System Monitor do scene/progress model;
- [x] migracja About do hierarchicznej sceny;
- [x] markery runtime `flux_scene_*` dla migrowanych aplikacji.

### Pozostałe 2.5.x

- [ ] natywne kernelowe widget records zamiast line serialization;
- [ ] widget ID zwracane przez pointer hit testing;
- [ ] wheel routing do scroll view;
- [ ] modal surfaces i dialogs;
- [ ] custom surface primitive;
- [ ] dokładniejsze dirty/damage regions;
- [ ] migracja Flux Terminal z legacy frame API;
- [ ] rozszerzenie layoutów poza pionowy flow.

## 2.6 — Desktop Applications

Cel: aplikacje mają być użytecznymi programami, nie demonstratorami ABI.

### Flux Terminal

- `ls`, `stat`, `mkdir`, `touch`, `rm`, `cp`, `mv`;
- `ps`, `kill`, `uptime`, `date`, `free`;
- `net`, `ping`, `ip`, DNS query;
- `reboot` i `shutdown` przez kontrolowane capabilities;
- pełniejsze job control i historię.

### Files

- prawdziwe `readdir/stat` z persistent VFS;
- nawigacja katalogów;
- create/rename/delete;
- informacje o plikach;
- uruchamianie ELF z GUI;
- podstawowe file associations.

### Settings / Monitor

- trwałe ustawienia sesji;
- realne memory/process/device snapshots;
- konfiguracja wyglądu Flux i input.

## 2.7 — Interaction Update

- resize okien;
- double click i context actions;
- scrolling i wheel routing;
- keyboard focus i Tab navigation;
- skróty systemowe;
- clipboard foundation;
- selection model;
- bardziej kompletna obsługa myszy i klawiatury.

## 2.8 — Flux Launcher

- launcher/search palette zamiast Start Menu/Launchpad/Activities;
- app manifests;
- registry `/apps`, `/gui` i przyszłych packaged apps;
- recent applications i favourites;
- ikony/resources;
- uruchamianie aplikacji i dokumentów z jednego interfejsu.

## 2.9 — Desktop Beta

Warunki wydania:

- boot → PID1 → Flux session → WindowManager;
- jednoczesny Terminal, Files, Settings, Monitor i About;
- launch/move/resize/minimize/restore/close;
- desktop przeżywa crash pojedynczej aplikacji Ring 3;
- brak wymaganej obsługi przez kernel developer console;
- automatyczne QEMU smoke tests dla całej sesji.

Wymagane markery docelowe:

```text
[TEST] desktop_session: PASS
[TEST] desktop_userspace_apps: PASS
[TEST] gui_input: PASS
[TEST] desktop_survives_app_crash: PASS
```

## 3.0 — Kurogane Desktop

Pierwszy release, w którym GUI jest pełnoprawnym podstawowym interfejsem systemu.
Terminal staje się zwykłą aplikacją desktopową, a nie głównym interfejsem OS.

- Flux Desktop jako domyślna sesja;
- Launcher i podstawowe system surfaces;
- Files, Terminal, Settings i Monitor jako normalny zestaw użytkowy;
- console/safe mode jako subsystem awaryjny;
- stabilniejszy publiczny desktop ABI.

## 3.1 — System Services

- userspace session service;
- settings service;
- notification service;
- event broker / IPC foundation;
- application lifecycle service;
- podstawowe identity/permissions;
- kontrolowane power/session capabilities.

## 3.2 — Flux Compositor

- niezależne application surfaces;
- backbuffers i double buffering;
- clipping;
- damage tracking;
- software transparency tam, gdzie ma sens;
- shadows i depth cues;
- płynniejsze przenoszenie i resize;
- architektura gotowa pod przyszłe GPU acceleration.

## 3.3 — Connected Desktop

- publiczne userspace network API;
- UDP/TCP sockets;
- DNS;
- network status service;
- GUI network settings;
- narzędzia diagnostyczne dostępne z Terminala bez Ring-0 shella.

## 3.4 — Developer Platform

Docelowy workflow SDK:

```text
kurogane new
kurogane build
kurogane run
kurogane package
```

Zakres:

- stabilniejsze C/C++ SDK;
- rozszerzone `libui`;
- application manifest/resources;
- generator projektu;
- debug/log API;
- prosty package format i instalacja aplikacji.

## 3.5 — Hardware & Reliability

- stabilizacja USB HID/xHCI;
- storage recovery i lepsze raportowanie awarii;
- więcej display modes;
- crash reports i watchdog foundation;
- real-hardware qualification;
- szerszy ACPI;
- przygotowanie NVMe/audio/SMP jako niezależnych torów rozwoju.

## 3.6 — Flux Stable

Cel końcowy tej roadmapy: pierwszy dopracowany, spójny Kurogane Flux Desktop,
którego nie trzeba nazywać Developer Preview.

Warunki 3.6:

- normalny power-on kończy się Flux Desktop bez ręcznej interwencji;
- podstawowe aplikacje są użyteczne i wieloprocesowe;
- aplikacje mogą się wywrócić bez zabicia sesji;
- system ma spójny launcher, Files, Settings, Terminal i Monitor;
- compositor i input są wystarczająco stabilne do codziennego testowania;
- build/test/release działa na Windows/WSL i macOS;
- kernel logs i developer console nie są wymagane do zwykłego korzystania.

## Zasada kolejności

```text
2.3 session/boot
 -> 2.4 WindowManager
 -> 2.5 UI runtime
 -> 2.6 real apps
 -> 2.7 interaction
 -> 2.8 launcher
 -> 2.9 beta
 -> 3.0 desktop release
 -> 3.1-3.6 platform/compositor/stability
```
