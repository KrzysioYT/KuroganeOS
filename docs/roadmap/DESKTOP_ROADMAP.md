# KuroganeOS Desktop Roadmap — 2.3 → 3.6

Roadmapa opisuje faktyczny kierunek Red Flux Desktop. `3.0` ustanowił desktop
jako główny interfejs, `3.1` ustabilizował rendering/interakcję, `3.2` dodał
boot splash, Login i systemowy Dock, a `3.3` jest **DEV BETA Media & Installer**.
Celem `3.6` pozostaje stabilny system do regularnego używania.

## Fundament — ZREALIZOWANY

- [x] x86-64 UEFI i boot protocol v3;
- [x] GDT/TSS/IST, IDT, VMM i prywatne address spaces;
- [x] Ring 3 + `int 0x80` ABI;
- [x] ELF64 processes, PID/TID, spawn/wait/exit i preempcja;
- [x] `/system/init` jako PID 1;
- [x] AHCI, GPT i FAT32/VFS;
- [x] PS/2 keyboard/mouse, PCI, ACPI/APIC;
- [x] WindowManager: focus/z-order/drag/resize/minimize/maximize/restore/close;
- [x] software backbuffer, clipping i damage-style GOP scanout;
- [x] Ring-3 `libui` scene/view runtime;
- [x] Red Flux Home + Dock + session lifecycle;
- [x] wspólny `FluxShellCore` dla console i GUI Terminala.

## 2.3–2.9 — Desktop Foundation — ZREALIZOWANE

- [x] normalny userspace desktop boot;
- [x] Flux Window Core;
- [x] scene/view compatibility runtime;
- [x] pierwsze aplikacje desktopowe;
- [x] arrow-first interaction i resize;
- [x] Launcher/Home jako root sesji;
- [x] PID1 supervision i console fallback.

## 3.0 — Kurogane Desktop — WYDANE

- [x] desktop jako podstawowy normalny UI;
- [x] aplikacje startują na żądanie;
- [x] Terminal jako aplikacja zamiast całego interfejsu OS;
- [x] 3.0.1 deterministic repaint i interactive resize.

## 3.1 — Red Flux Interaction — WDROŻONE

- [x] czarno-grafitowo-czerwony Red Flux;
- [x] software full-frame backbuffer;
- [x] content clipping i text scale limit;
- [x] wspólny `FluxShellCore`;
- [x] pełniejsze sterowanie klawiaturą GUI Terminala;
- [x] uproszczenie starego pseudo-DOSowego scene renderingu.

## 3.2 — Red Flux Desktop Shell — WDROŻONE

- [x] Red Flux jako domyślny boot;
- [x] graficzny boot splash;
- [x] `/gui/login` jako session gate;
- [x] PID1 model `Login -> Home -> Login`;
- [x] pełniejszy Red Flux Dock z przypiętymi aplikacjami;
- [x] running/focus state i dynamiczna sekcja okien;
- [x] GUI session ownership przez drzewo Home;
- [x] logout/session cleanup;
- [x] damage-style GOP scanout ograniczający flash System Monitora.

## 3.3 — DEV BETA Media & Installer — AKTYWNY

Wersja: `3.3.0-dev`, kanał: **DEV BETA**.

Docelowy media flow:

```text
IMG / ISO
 -> Red Flux Setup
    -> Try KuroganeOS
       -> read-only install.pkg VFS
       -> Login -> Home
    -> Install KuroganeOS
       -> EN / PL
       -> local username
       -> password / no password
       -> target SATA/AHCI disk
       -> INSTALL confirmation
       -> GPT + FAT32 + system + profile
       -> reboot -> Login
```

Zrealizowane w kodzie:

- [x] oznaczenie `3.3.0-dev / DEV BETA`;
- [x] read-only VFS backend bezpośrednio nad `install.pkg`;
- [x] Try mode bez zapisu na dysk;
- [x] Red Flux setup UI przed userspace session;
- [x] English / Polski;
- [x] lokalny username;
- [x] konto bez hasła / z hasłem;
- [x] graficzny wybór AHCI target disk;
- [x] dokładne `INSTALL` przed destrukcyjnym zapisem;
- [x] istniejący verified GPT/ESP/root FAT32 installer backend;
- [x] `/etc/locale.cfg` + `/etc/user.cfg`;
- [x] Login czyta zainstalowany profil;
- [x] tymczasowy `FNV1A64-DEV` verifier hasła jawnie oznaczony jako niestabilny;
- [x] `build-media.ps1` dla Windows/WSL;
- [x] `build-media-macos.sh` dla macOS;
- [x] natywny `setup/build/build-installer/build-media` dla Linux x86-64;
- [x] media IMG dostaje `install.pkg`, tak samo jak ISO.

Runtime acceptance do zamknięcia 3.3:

- [ ] Windows: media build IMG + ISO;
- [ ] macOS: media build IMG + ISO;
- [ ] Linux x86-64: media build IMG + ISO;
- [ ] IMG -> Try -> Login -> Home;
- [ ] ISO -> Try -> Login -> Home;
- [ ] EN install bez hasła -> reboot -> Login/Home;
- [ ] PL install z hasłem -> reboot -> reject bad / accept good password;
- [ ] brak zapisu na dysk przed `INSTALL`;
- [ ] installer verification + persistence po reboot;
- [ ] VirtualBox i QEMU smoke test.

Bezpieczeństwo DEV BETA:

- [ ] zastąpić `FNV1A64-DEV` prawdziwym password KDF;
- [ ] account service + credential store;
- [ ] lock screen i zmiana hasła;
- [ ] recovery/update path dla zainstalowanego systemu.

## 3.4 — Native Flux Compositor + System Services

- natywne application surfaces zamiast `ku_ui_frame`;
- widget records i pointer `widget_id`;
- per-window buffers i dokładniejsze damage regions;
- płynny drag/resize bez składania wszystkich aplikacji;
- userspace IPC/event broker;
- settings/notification service;
- account service + bezpieczny credential store;
- publiczne `stat/readdir/write/create/unlink/rename/mkdir/rmdir`;
- clipboard, wheel i context actions;
- fundament pod GPU acceleration.

## 3.5 — Connected Desktop + Developer Platform + Reliability

- userspace UDP/TCP sockets i DNS;
- network settings/status service;
- dynamiczne app registry, manifests i resources;
- docelowy SDK workflow:

```text
kurogane new
kurogane build
kurogane run
kurogane package
```

- crash reports/watchdog;
- storage recovery;
- USB HID/xHCI stabilization;
- więcej display modes;
- NVMe/audio/SMP preparation;
- real-hardware qualification;
- powtarzalny build/release na Windows, macOS i Linux.

## 3.6 — Flux Stable

Warunki wydania:

- normalny power-on kończy się stabilnym Boot -> Login -> Red Flux Desktop;
- oficjalne media oferują działające Try i Install;
- install/reboot/persistence działa na zweryfikowanych konfiguracjach;
- bezpieczna account/credential service zastępuje DEV verifier;
- compositor/input nie powodują flickera, ghostingu ani artefaktów;
- Dock, Home, Files, Terminal, Settings i Monitor tworzą spójny UX;
- awaria jednej aplikacji nie zabija sesji;
- podstawowe file/process/network capabilities są dostępne w Ring 3;
- Windows/macOS/Linux buildy są powtarzalne;
- regularne korzystanie z systemu nie wymaga kernel developer console.
