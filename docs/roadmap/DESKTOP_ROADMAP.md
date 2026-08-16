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
- [x] natywne IMG i instalowalne ISO na macOS.

## 2.3.0 — Desktop Boot Repair — ZREALIZOWANE

Cel: normalny userspace boot ma kończyć się realną sesją graficzną, a nie tylko
`/apps/shell`.

- [x] nowy kernelowy host sesji `flux-session`;
- [x] automatyczna inicjalizacja WindowManagera przy normalnym userspace boot;
- [x] Safe Mode nadal pozostaje tekstowym trybem awaryjnym;
- [x] PID1 uruchamia `/gui/terminal`, `/gui/files`, `/gui/sysmon`,
  `/gui/settings` i `/gui/about`;
- [x] PID1 nadzoruje i restartuje zakończone aplikacje desktopowe;
- [x] szybka awaria większości GUI przełącza PID1 na console fallback;
- [x] `rootfs/etc/system.cfg` deklaruje `BOOT_MODE=desktop`;
- [x] markery runtime `desktop_session`, `desktop_userspace_apps` i
  `userspace_desktop_session`.

## 2.4 — Flux Window Core

Cel: usunąć ostatnie elementy starego Desktop Alpha i zbudować własny język
zarządzania oknami Kurogane Flux.

- usunięcie klasycznego taskbara i kontrolek `- [] X`;
- Signal Spine jako systemowy pionowy pas aktywności;
- Pulse Ribbon jako dynamiczna powierzchnia aktywnych aplikacji;
- nowe Flux window controls bez kopiowania Windows/macOS/GNOME/KDE;
- spójne stany focus/active/background;
- dopracowany drag, minimize, maximize i restore;
- przygotowanie geometrii pod resize;
- testy wejścia i focus routing dla wielu procesów GUI.

## 2.5 — Flux UI Runtime

Cel: odejść od obecnego stałego `ku_ui_frame` z kilkoma liniami tekstu.

- widget/view tree;
- layout engine;
- label, button, input, list, progress, scroll view i custom surface;
- dirty regions i częściowe repaint;
- modal surfaces i dialogs;
- focus traversal;
- rozszerzenie `libui` i publicznego UI ABI bez kernel-shell backdoorów.

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

Nie wracamy do schematu „ładny screenshot → dokumentacja mówi desktop → boot nadal
kończy się terminalem”. Kolejność pozostaje:

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
