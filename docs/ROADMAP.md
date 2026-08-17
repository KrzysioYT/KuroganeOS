# KuroganeOS active roadmap

Status bazowy: **3.3.3-dev / DEV BETA**.

Ten plik jest kanoniczną listą prac po aktualnej linii 3.3.x. Każdy punkt jest
odznaczany dopiero wtedy, gdy kod, publiczne API (jeśli dotyczy), dokumentacja i
właściwe testy są spójne. Sam stub albo samo skompilowanie pliku nie oznacza
ukończenia funkcji.

## Legenda

- ✅ ukończone i objęte bieżącą kwalifikacją repozytorium;
- 🟡 działający fundament istnieje, ale brak pełnej warstwy publicznej albo
  kwalifikacji runtime;
- ⬜ do wykonania;
- 🔒 wymaga testu na zewnętrznym hoście/sprzęcie i nie może zostać uczciwie
  oznaczone jako PASS wyłącznie przez CI.

## R0 — build, repo i release qualification

- [x] ✅ UEFI x86-64 bootloader + boot protocol v3.
- [x] ✅ Powtarzalny build kernela w konfiguracji testowej.
- [x] ✅ Hostowe testy ABI/SDK uruchamiane przez `make test`.
- [x] ✅ Czysty checkout bez śledzonych `build/*.o`, `*.d`, `*.elf` i cache.
- [x] ✅ Linux IMG + wspólne ISO.
- [x] ✅ El Torito EFI + GPT ESP verifier 20/20.
- [x] ✅ OVMF/QEMU optical UEFI smoke do markera kernela.
- [ ] 🔒 Oracle VirtualBox x86-64: realny ISO boot -> kernel marker.
- [ ] 🔒 VirtualBox: Try -> Login -> Home.
- [ ] 🔒 VirtualBox: Install -> SATA VDI -> reboot bez ISO -> Login.
- [ ] 🔒 VirtualBox NAT/E1000: DHCP + gateway + DNS runtime smoke.
- [ ] 🔒 VirtualBox AC'97: słyszalny PCM runtime smoke.

## R1 — kernel, procesy i pamięć

- [x] ✅ VMM, PMM, heap, GDT/TSS/IST, IDT i izolacja Ring 3.
- [x] ✅ ELF64 ET_EXEC, prywatny CR3, NX/W^X i izolacja wyjątków userspace.
- [x] ✅ PID/TID, spawn/wait/exit, sleep/yield i PIT round-robin preemption.
- [ ] ⬜ Publiczne tworzenie wielu wątków w jednym procesie Ring 3.
- [ ] ⬜ Synchronizacja userspace: mutex/condvar/waitable event/futex-like wait.
- [ ] ⬜ Wysokiej rozdzielczości zegar monotoniczny i timery userspace.
- [ ] ⬜ Demand paging.
- [ ] ⬜ Copy-on-write.
- [ ] ⬜ File-backed mmap.
- [ ] ⬜ Swap/paging backend.
- [ ] ⬜ SMP i rzeczywiste wykorzystanie wielu CPU.

## R2 — filesystem i persistent application state

- [x] ✅ Writable FAT32/VFS persistent root.
- [x] ✅ Ring-3 open/read/write/append/close.
- [x] ✅ Ring-3 stat/readdir.
- [x] ✅ Ring-3 create/unlink/rename/mkdir/rmdir/sync.
- [x] ✅ Read-only live-package root odrzucający mutacje.
- [ ] ⬜ Publiczne `seek`/pozycjonowanie pliku.
- [ ] ⬜ File-backed mmap po ukończeniu VM mapping API.
- [ ] ⬜ Process-local cwd/chdir/getcwd zamiast globalnego kontekstu ścieżki.
- [ ] ⬜ Symlinks/hard links albo jawna decyzja o ich braku w stabilnym ABI.
- [ ] ⬜ Model owner/group/permissions/ACL.
- [ ] ⬜ Settings/profile service zapisujący trwałe ustawienia desktopu i aplikacji.
- [ ] ⬜ Recovery + transakcyjne aktualizacje systemu.

## R3 — IPC i sandbox

- [ ] ⬜ Generation-checked IPC endpoint/channel handles.
- [ ] ⬜ Bounded message send/receive z walidacją Ring-3 pointerów.
- [ ] ⬜ Shared-memory objects z kontrolą mapowania/ownership.
- [ ] ⬜ Process event/wait integration dla IPC i async I/O.
- [ ] ⬜ Capability/permission model dla usług systemowych.
- [ ] ⬜ Sandbox primitives potrzebne przez model browser/renderer.

## R4 — networking

- [x] ✅ E1000 82540EM + Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS.
- [x] ✅ Minimalny TCP backend i bounded HTTP/80 transport.
- [x] ✅ Ring-3 network status snapshot.
- [x] ✅ Ring-3 bounded HTTP GET dla bootstrap Kurogane Web.
- [ ] ⬜ Publiczne async socket handles.
- [ ] ⬜ Connect/send/recv/close bez długiego blocking syscall.
- [ ] ⬜ DNS resolver jako async/public service.
- [ ] ⬜ Poll/event integration z IPC/thread wait primitives.
- [ ] ⬜ TLS 1.2/1.3 provider.
- [ ] ⬜ Certificate validation + trust store.
- [ ] ⬜ HTTPS transport.
- [ ] ⬜ Stabilniejszy TCP: retransmission, ordering, timeout i większe transfery.

## R5 — audio

- [x] ✅ Intel ICH AC'97 `8086:2415` kernel PCM S16LE/stereo/48 kHz backend.
- [x] ✅ Ring-3 audio status.
- [x] ✅ Ring-3 master volume/mute.
- [ ] ⬜ Bezpieczny Ring-3 PCM playback/stream API.
- [ ] ⬜ Per-process audio stream handles i mixer.
- [ ] ⬜ Buffer scheduling / underrun handling.
- [ ] ⬜ Format conversion/resampling.
- [ ] ⬜ Capture/microphone.
- [ ] ⬜ Intel HDA backend.

## R6 — shell i POSIX/libc compatibility

- [x] ✅ Podstawowy shell i uruchamianie ELF64.
- [ ] ⬜ Pipes.
- [ ] ⬜ stdin/stdout redirection.
- [ ] ⬜ Environment variables.
- [ ] ⬜ Glob/wildcards.
- [ ] ⬜ Pełniejsze background jobs/job control.
- [ ] ⬜ Większy libc/libc++ compatibility surface.
- [ ] ⬜ POSIX-like errno/fd/time/thread adapters wymagane przez duże porty.

## R7 — desktop / UI

- [x] ✅ WindowManager, Red Flux Desktop, Dock i aplikacje GUI Ring 3.
- [x] ✅ Software backbuffer, clipping i damage-style GOP scanout.
- [x] ✅ Bounded Ring-3 UI event queue i cleanup zasobów procesu.
- [ ] ⬜ Persistent desktop pin/settings state przez settings service.
- [ ] ⬜ Clipboard.
- [ ] ⬜ Unicode/text shaping/font discovery.
- [ ] ⬜ HiDPI/scaling.
- [ ] ⬜ Multi-monitor.
- [ ] ⬜ Eliminacja compatibility `ku_ui_frame` na rzecz docelowego surface API.
- [ ] ⬜ Natywne per-window accelerated surfaces.

## R8 — graphics runtime

- [x] ✅ PCI display-class discovery i driver-manager binding.
- [x] ✅ GOP/software compositor capability reporting bez fałszywego 3D support.
- [ ] ⬜ Kurogane Graphics Runtime: surface/image/buffer/texture handles.
- [ ] ⬜ Command buffers + viewport/scissor + render/depth/blend state.
- [ ] ⬜ Present/swap + synchronization/fences.
- [ ] ⬜ Software 3D: triangles, depth, textures i prosty shader IR.
- [ ] ⬜ Pierwszy realny GPU command-submission backend.
- [ ] ⬜ D3D9 compatibility frontend.
- [ ] ⬜ D3D11 compatibility frontend.
- [ ] ⬜ D3D12 compatibility frontend.

## R9 — installer, security i accounts

- [x] ✅ Kernelowy GPT/FAT32 installer i Try/Install media.
- [x] ✅ Lokalny profil instalacji + DEV password verifier.
- [ ] ⬜ Bezpieczny password KDF zamiast `FNV1A64-DEV`.
- [ ] ⬜ Account service + credential store.
- [ ] ⬜ Lock screen/session authentication.
- [ ] ⬜ Per-user home/profile ownership.
- [ ] ⬜ Recovery environment.
- [ ] ⬜ Signed/transakcyjne aktualizacje.

## R10 — hardware enablement

- [x] ✅ AHCI/SATA.
- [x] ✅ PS/2 keyboard/mouse.
- [x] ✅ E1000 82540EM.
- [x] ✅ AC'97.
- [x] ✅ PCI + ACPI MADT/APIC discovery.
- [ ] ⬜ Stabilizacja xHCI/USB HID na większej liczbie konfiguracji.
- [ ] ⬜ NVMe jako równorzędny storage backend.
- [ ] ⬜ Intel HDA.
- [ ] ⬜ SMP/APIC interrupt routing jako aktywna ścieżka zamiast PIC-only profile.
- [ ] 🔒 Szersza kwalifikacja na realnym sprzęcie UEFI.

## R11 — Chromium / Kurogane Web

- [x] ✅ BrowserContext / NavigationController / PlatformDelegate bootstrap.
- [x] ✅ Plain HTTP navigation + bounded redirects + bootstrap text renderer.
- [x] ✅ Writable filesystem foundation dla przyszłego profile/cache.
- [ ] ⬜ Async socket ABI.
- [ ] ⬜ TLS/HTTPS.
- [ ] ⬜ Userspace threads/task primitives/timers.
- [ ] ⬜ Shared memory + browser/renderer IPC.
- [ ] ⬜ libc++/Chromium `base` platform layer.
- [ ] ⬜ GN `target_os = "kurogane"` toolchain definition.
- [ ] ⬜ Build/run Chromium `base` + `url` smoke target.
- [ ] ⬜ `content_shell` browser process.
- [ ] ⬜ Blink renderer.
- [ ] ⬜ V8/JavaScript.
- [ ] ⬜ GPU compositing.

## Najbliższa kolejność implementacji

Kolejność jest zależnościowa, nie marketingowa:

1. filesystem seek + process-local path groundwork;
2. Ring-3 PCM playback foundation;
3. IPC message channels;
4. shared memory + wait/event primitives;
5. userspace threads + monotonic time;
6. async sockets + DNS service;
7. TLS/trust store/HTTPS;
8. settings/account credential services;
9. shell pipes/redirection/env;
10. graphics runtime i software 3D;
11. Chromium platform layer;
12. SMP/NVMe/HDA i szerszy hardware qualification.

Po każdym ukończonym etapie ten plik ma zostać zaktualizowany w tym samym
cyklu zmian, a kwalifikacja `main` musi pozostać zielona.