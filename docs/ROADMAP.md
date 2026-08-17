# KuroganeOS active roadmap

Status bazowy: **3.3.3-dev / DEV BETA**.

Ten plik jest kanoniczną listą prac po aktualnej linii 3.3.x. Każdy punkt jest
odznaczany dopiero wtedy, gdy kod, publiczne API (jeśli dotyczy), dokumentacja i
właściwe testy są spójne. Sam stub albo samo skompilowanie pliku nie oznacza
ukończenia funkcji.

## Zasady prowadzenia projektu

- Rozwój odbywa się **bezpośrednio na `main`**. Nie tworzymy roboczych gałęzi ani
  dodatkowych PR-ów, chyba że właściciel projektu jawnie zmieni tę zasadę.
- Ten plik jest jedyną kanoniczną roadmapą. Status funkcji ma być aktualizowany
  w tym samym cyklu zmian, w którym zmienia się jej rzeczywisty stan.
- Nie oznaczamy jako gotowych atrap API. Browser, Direct3D, sieć i sterowniki
  dostają ✅ dopiero po działającym kodzie oraz odpowiedniej kwalifikacji.
- Priorytet produktu: **działający Web -> przenośna sieć -> stabilny graphics /
  Direct3D compatibility -> pełniejszy Chromium runtime -> hardware enablement**.

## Inwarianty stabilności i wydajności

Każdy nowy subsystem ma przestrzegać poniższych zasad:

- bounded buffers, kolejki i command lists; brak nieograniczonego wzrostu RAM;
- bounded work per tick/syscall/frame oraz jawne timeouty operacji I/O;
- brak aktywnych spin-loopów przy bezczynności; użycie sleep/event/HLT, gdy CPU
  nie ma użytecznej pracy;
- ownership + cleanup zasobów per PID i generation-checked handles tam, gdzie
  zasób przeżywa pojedyncze wywołanie;
- brak wykonywalnych mapowań danych bez potrzeby, walidacja Ring-3 pointerów i
  rozmiarów oraz fail-closed dla nieobsługiwanych stanów;
- degradacja pojedynczego sterownika/usługi nie może bez potrzeby zatrzymywać
  bootowania całego systemu;
- test hostowy/ABI dla logiki możliwej do testowania bez VM oraz runtime smoke
  dla ścieżek wymagających prawdziwego urządzenia/hypervisora;
- `main` ma wracać do zielonego pełnego gate po każdej serii zmian.

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
- [x] ✅ Gate na `main`: kernel test build, host ABI/SDK, media build,
  FAT32/VFS validation, verifier 20/20 i OVMF smoke.
- [x] ✅ QEMU user-NAT network qualification dla E1000 i AMD PCnet: boot,
  DHCP, gateway i DNS są wymagane przez CI.
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
- [ ] 🟡 Publiczny monotoniczny zegar 100 Hz (`ticks` + bezpieczna konwersja do
  ms) jest gotowy; high-resolution timer source i waitable timer objects pozostają TODO.
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
- [x] ✅ Publiczne `seek`/pozycjonowanie pliku z `BEGIN/CURRENT/END` i
  overflow-checked VFS offsets.
- [x] ✅ Process-local cwd/chdir/getcwd, relative path resolution oraz
  dziedziczenie cwd przez dziecko przy `spawn`.
- [ ] ⬜ File-backed mmap po ukończeniu VM mapping API.
- [x] ✅ ABI v1 świadomie nie implementuje symlinków/hard linków na FAT32;
  polityka i warunki przyszłego rozszerzenia są zapisane w
  `docs/DEVELOPERS/FILESYSTEM_POLICY.md`.
- [ ] ⬜ Model owner/group/permissions/ACL.
- [ ] ⬜ Settings/profile service zapisujący trwałe ustawienia aplikacji i profilu.
- [ ] ⬜ Recovery + transakcyjne aktualizacje systemu.

## R3 — IPC i sandbox

- [x] ✅ Generation-checked IPC endpoint/channel handles z nazwanym
  `bind/connect/accept` i ownership per PID.
- [x] ✅ Bounded 256-byte message send/receive, queue backpressure,
  stale-handle checks, peer-close semantics i cleanup po PID.
- [x] ✅ Shared-memory objects: PMM-backed zero-filled pages, generation-checked
  handles, owner/grant per PID, writable+NX Ring-3 map/unmap i refcount cleanup.
- [ ] 🟡 Waitable event objects: generation-checked owner/grant handles,
  auto/manual reset, signal/reset/poll/close i sleeping `ku_event_wait`; bezpośrednie
  wake-up z `Blocked` i automatyczne readiness z IPC/async I/O pozostają do spięcia.
- [ ] ⬜ Capability/permission model dla usług systemowych.
- [ ] ⬜ Sandbox primitives potrzebne przez model browser/renderer.

## R4 — networking

- [x] ✅ E1000 82540EM + Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS.
- [x] ✅ AMD PCnet jako drugi runtime-selectable NIC backend; physical network
  layer wybiera E1000 albo PCnet zależnie od wykrytego urządzenia.
- [x] ✅ QEMU Windows/WSL i QEMU macOS używają user-NAT; E1000 jest wspólnym
  profilem przenośnym, a Linux CI kwalifikuje także PCnet.
- [x] ✅ QEMU E1000 i PCnet user-NAT runtime smoke w CI z wymaganym DHCP,
  gateway i DNS.
- [x] ✅ Minimalny TCP backend i bounded HTTP/80 transport.
- [x] ✅ Ring-3 network status snapshot.
- [x] ✅ Ring-3 bounded HTTP GET dla bootstrap Kurogane Web.
- [ ] ⬜ VirtIO-net backend dla nowoczesnego QEMU/KVM.
- [ ] ⬜ Dalsze fizyczne NIC backends (co najmniej popularny Intel/Realtek) dla
  realnego sprzętu; „wszystkie maszyny” oznacza rozszerzaną macierz driverów,
  nie pojedynczy uniwersalny sterownik.
- [ ] ⬜ Publiczne async socket handles.
- [ ] ⬜ Connect/send/recv/close bez długiego blocking syscall.
- [ ] ⬜ DNS resolver jako async/public service.
- [ ] ⬜ Poll/event integration z IPC/thread wait primitives.
- [ ] 🟡 Mbed TLS 3.6.7 jest przypięty jako submodule, istnieje freestanding
  TLS 1.2/X.509 client config i compile-probe; brakuje jeszcze platform glue,
  TCP BIO, entropy, trust store i runtime handshake.
- [ ] ⬜ Aktualizowalny systemowy CA trust store + hostname/time validation.
- [ ] ⬜ HTTPS transport dostępny przez publiczne Ring-3 API.
- [ ] ⬜ Stabilniejszy TCP: retransmission, ordering, timeout i większe transfery.

## R5 — audio

- [x] ✅ Intel ICH AC'97 `8086:2415` kernel PCM S16LE/stereo/48 kHz backend.
- [x] ✅ Ring-3 audio status.
- [x] ✅ Ring-3 master volume/mute.
- [x] ✅ Bezpieczny bounded Ring-3 PCM playback foundation: 48 kHz S16LE stereo,
  max 1024 frames, kernel DMA copy, per-PID ownership, poll i stop.
- [ ] ⬜ Per-process audio stream handles i mixer.
- [ ] ⬜ Buffer scheduling / underrun handling dla ciągłego streamingu.
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
- [x] ✅ Piny desktopu są zapisywane przez Ring 3 do `/home/desktop.cfg`,
  odtwarzane przy starcie Home i synchronizowane przez publiczne writable FS ABI.
- [ ] ⬜ Szerszy settings/profile service dla ustawień desktopu i aplikacji.
- [ ] ⬜ Clipboard.
- [ ] ⬜ Unicode/text shaping/font discovery.
- [ ] ⬜ HiDPI/scaling.
- [ ] ⬜ Multi-monitor.
- [ ] ⬜ Eliminacja compatibility `ku_ui_frame` na rzecz docelowego surface API.
- [ ] ⬜ Natywne per-window accelerated surfaces.

## R8 — graphics runtime / Direct3D compatibility

- [x] ✅ PCI display-class discovery i driver-manager binding.
- [x] ✅ GOP/software compositor capability reporting bez fałszywego 3D support.
- [ ] 🟡 Publiczny bounded software surface runtime istnieje w SDK: XRGB8888,
  clear, clipped rect/line i integer filled-triangle rasterizer bez FPU/SSE.
- [ ] 🟡 Wspólny source-level Direct3D compatibility foundation dla frontendów
  9/11/12 istnieje nad software surface; ma frame/draw budgets oraz bounded
  D3D12-style command listę. Nie jest to jeszcze Windows COM ABI.
- [ ] ⬜ Natywne surface/image/buffer/texture handles zarządzane przez kernel/runtime.
- [ ] ⬜ Per-window command buffer present + viewport/scissor + render/depth/blend state.
- [ ] ⬜ Present/swap + synchronization/fences do WindowManagera.
- [ ] ⬜ Software 3D: depth buffer, textures i shader IR ponad gotowym triangle rasterizerem.
- [ ] ⬜ Pierwszy realny GPU command-submission backend.
- [ ] ⬜ D3D9 pełniejsza semantyka device/resources/state + compatibility frontend.
- [ ] ⬜ D3D11 device/context/resources/shaders + compatibility frontend.
- [ ] ⬜ D3D12 queues/lists/barriers/descriptors + compatibility frontend.
- [ ] ⬜ Dopiero po powyższych warstwach ocena osobnego Windows-compatible COM ABI;
  samo podobieństwo nazw API nie może być raportowane jako pełny DirectX.

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
- [x] ✅ AMD PCnet.
- [x] ✅ AC'97.
- [x] ✅ PCI + ACPI MADT/APIC discovery.
- [ ] ⬜ Stabilizacja xHCI/USB HID na większej liczbie konfiguracji.
- [ ] ⬜ NVMe jako równorzędny storage backend.
- [ ] ⬜ Intel HDA.
- [ ] ⬜ VirtIO-net.
- [ ] ⬜ SMP/APIC interrupt routing jako aktywna ścieżka zamiast PIC-only profile.
- [ ] 🔒 Szersza kwalifikacja na realnym sprzęcie UEFI.

## R11 — Chromium / Kurogane Web

- [x] ✅ BrowserContext / NavigationController / PlatformDelegate bootstrap.
- [x] ✅ Plain HTTP navigation + bounded redirects + bootstrap text renderer.
- [x] ✅ Edytowalny omnibox: pełny URL, sama domena i zwykły tekst zapytania;
  Backspace edytuje, Escape czyści, Enter uruchamia nawigację.
- [x] ✅ Search-capable parser: tekst zapytania jest percent-encoded i zamieniany
  na HTTPS search target zamiast traktowania go jak nazwę domeny.
- [x] ✅ Writable filesystem foundation dla przyszłego profile/cache.
- [ ] ⬜ Faktyczne wykonanie wyszukiwania przez HTTPS — obecnie prawidłowo
  zatrzymuje się na braku TLS zamiast wykonywać niebezpieczny downgrade.
- [ ] ⬜ HTTPS dla zwykłych współczesnych stron oraz bezpieczne redirecty HTTP->HTTPS.
- [ ] ⬜ Async socket ABI.
- [ ] 🟡 Monotonic time foundation jest publiczny; nadal brakuje userspace threads,
  bezpośrednich wait primitives i timer objects potrzebnych przez Chromium task model.
- [ ] 🟡 Bounded message IPC, shared memory i waitable-event foundation istnieją;
  nadal brakuje bezpośredniego wake/event readiness i docelowego sandbox model.
- [ ] ⬜ libc++/Chromium `base` platform layer.
- [ ] ⬜ GN `target_os = "kurogane"` toolchain definition.
- [ ] ⬜ Build/run Chromium `base` + `url` smoke target.
- [ ] ⬜ `content_shell` browser process.
- [ ] ⬜ Blink renderer.
- [ ] ⬜ V8/JavaScript.
- [ ] ⬜ GPU compositing.

## Najbliższa kolejność implementacji

Kolejność jest zależnościowa i podporządkowana działającej przeglądarce,
przenośnej sieci oraz stabilności:

1. Mbed TLS platform glue + bezpieczne entropy + TCP BIO;
2. aktualizowalny CA trust store + certificate/hostname/time validation;
3. publiczny HTTPS GET i podpięcie go do omnibox/search w Kurogane Web;
4. wzmocnienie TCP oraz async sockets/DNS + event readiness;
5. VirtIO-net, potem popularne fizyczne NIC i dalsza macierz VM/hardware;
6. native graphics command-buffer/present path do WindowManagera;
7. depth/textures/shader IR i rozszerzanie D3D9 -> D3D11 -> D3D12;
8. direct blocked-thread wake + userspace threads/timery;
9. Chromium platform layer / `content_shell` / Blink / V8;
10. settings/account credential services, SMP/NVMe/HDA i recovery.

Po każdym ukończonym etapie ten plik ma zostać zaktualizowany w tym samym
cyklu zmian, a kwalifikacja `main` musi pozostać zielona.
