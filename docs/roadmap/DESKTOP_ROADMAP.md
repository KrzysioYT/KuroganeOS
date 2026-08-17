# KuroganeOS Desktop Roadmap — 2.3 → 3.6

Roadmapa opisuje faktyczny kierunek Red Flux Desktop. `3.0` ustanowił desktop
jako główny interfejs, `3.1` ustabilizował rendering/interakcję, `3.2` dodał
boot splash, Login i systemowy Dock, a `3.3.x` rozwija **DEV BETA Media,
Installer, VirtualBox qualification i platform enablement**. Celem `3.6`
pozostaje stabilny system do regularnego używania.

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

Aktualna rewizja: `3.3.3-dev`, kanał: **DEV BETA**.

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

### 3.3.0 — media/setup foundation — ZREALIZOWANE

- [x] oznaczenie DEV BETA;
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

### 3.3.1 — VirtualBox qualification & enablement — WDROŻONE / ACCEPTANCE OTWARTE

Dokumentacja i onboarding:

- [x] beginner-first `docs/START_HERE.md`;
- [x] kompletna instrukcja `docs/VIRTUALBOX.md`;
- [x] `docs/NETWORKING.md`, `docs/AUDIO.md` i graphics compatibility status;
- [x] developer hub `docs/DEVELOPERS/`;
- [x] tutorial pierwszej aplikacji, GUI i public API reference;
- [x] kernel contribution guide;
- [x] README prowadzi początkującego najpierw do START HERE.

VirtualBox / UEFI media:

- [x] zidentyfikowany i usunięty historyczny 64 MiB El Torito EFI image;
- [x] wspólny 30 MiB FAT16 boot image = 61440 sektorów po 512 B;
- [x] removable-media path `EFI/BOOT/BOOTX64.EFI`;
- [x] El Torito platform EFI / no-emulation;
- [x] EFI boot image wystawiony jako GPT EFI System Partition;
- [x] obowiązkowy 20-pass verifier blokujący publikację wadliwego ISO;
- [x] verifier sprawdza El Torito, GPT ESP, FAT, PE AMD64, kernel, package,
      sector limit i stabilny SHA-256;
- [x] drugi niezależny 20-pass verifier w CI;
- [x] realny optical UEFI smoke przez OVMF/QEMU do markera kernela;
- [x] helper tworzący referencyjną VirtualBox VM;
- [x] Windows `-VirtualBoxSmoke` tworzący rzeczywistą Oracle VBox VM i
      wymagający markera kernela przez COM1;
- [ ] zapisać wynik realnego Oracle VirtualBox smoke na hoście x86-64;
- [ ] `ISO -> Try -> Login -> Home` w Oracle VirtualBox;
- [ ] `ISO -> Install -> SATA VDI -> reboot without ISO -> Login`.

Automated qualification aktualnego kodu:

- [x] Linux full media build;
- [x] installer ESP `30 MiB / 61440 sectors`;
- [x] mandatory builder verifier `20/20`;
- [x] workflow second verifier `20/20`;
- [x] OVMF/QEMU optical UEFI boot -> kernel marker;
- [x] qualification artifacts upload.

VirtualBox network/audio target:

- [x] referencyjny NIC: Intel PRO/1000 MT Desktop / 82540EM / `8086:100E`;
- [x] E1000 + Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS/basic TCP probe;
- [x] DHCP/link failure nie zatrzymuje desktopu — loopback fallback;
- [x] referencyjny audio controller: Intel ICH AC'97 / `8086:2415`;
- [x] kernel AC'97 PCM S16LE stereo 48 kHz;
- [x] bus-master DMA32 backend;
- [x] AC'97 rejestruje się w centralnym driver managerze;
- [ ] realny VirtualBox NAT -> DHCP/gateway/DNS acceptance;
- [ ] realny VirtualBox AC'97 audible PCM acceptance;
- [ ] publiczny async userspace network/socket service;
- [ ] publiczny async userspace audio stream service.

Graphics / Direct3D:

- [x] dokumentacja uczciwie rozdziela native Kurogane Graphics i D3D compatibility;
- [x] zakaz fałszywego raportowania niezaimplementowanych feature levels;
- [ ] native Kurogane Graphics resource/runtime API;
- [ ] software 3D raster backend;
- [ ] accelerated GPU backend;
- [ ] D3D9/10/11 compatibility frontend;
- [ ] D3D12 compatibility frontend;
- [ ] shader/resource/command model potrzebny do realnej zgodności.

### Runtime acceptance do zamknięcia 3.3.x

- [ ] Windows: pełny media build + real Oracle VirtualBox smoke;
- [ ] macOS: pełny media build IMG + ISO na docelowym toolchainie;
- [x] Linux x86-64: media build IMG + ISO w CI;
- [ ] IMG -> Try -> Login -> Home;
- [ ] ISO -> Try -> Login -> Home w Oracle VirtualBox;
- [ ] EN install bez hasła -> reboot -> Login/Home;
- [ ] PL install z hasłem -> reboot -> reject bad / accept good password;
- [ ] brak zapisu na dysk przed `INSTALL`;
- [ ] installer verification + persistence po reboot;
- [x] niezależny QEMU/OVMF optical ISO smoke;
- [ ] Oracle VirtualBox optical ISO smoke;
- [ ] VirtualBox networking/audio smoke.

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
- native Kurogane Graphics resource API;
- fundament pod GPU acceleration.

## 3.5 — Connected Desktop + Developer Platform + Reliability

- async userspace UDP/TCP sockets i DNS;
- network settings/status service;
- async audio service nad AC'97 i przyszłymi backendami;
- dynamiczne app registry, manifests i resources;
- software 3D / pierwsza graphics compatibility layer;
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
- NVMe/HDA/SMP preparation;
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
- podstawowe file/process/network/audio capabilities są dostępne w Ring 3;
- Windows/macOS/Linux buildy są powtarzalne;
- VirtualBox reference profile ma automatyczny i realny host smoke;
- regularne korzystanie z systemu nie wymaga kernel developer console.
