# KuroganeOS active roadmap

Status bazowy: **3.3.3-dev / DEV BETA**.

Ten plik jest kanoniczną listą prac dla bieżącej linii KuroganeOS. Punkt dostaje
✅ dopiero wtedy, gdy kod, publiczne API (jeżeli dotyczy), dokumentacja oraz
odpowiednia kwalifikacja runtime są ze sobą zgodne. Sam stub, host-test albo
samo skompilowanie pliku nie oznacza ukończenia funkcji.

## Stan po stabilizacji 2026-08-20

- PR #4 został scalony do `main` po dużym stabilization/audit pass.
- QEMU kwalifikuje E1000, PCnet i VirtIO-net przez rzeczywisty user-NAT runtime.
- Oracle VirtualBox potrafi uruchomić ISO, przejść instalator, zapisać VDI i
  ponownie uruchomić z dysku do persistent ROOT, PID 1, DHCP, gateway i DNS.
- Pełny release-smoke VirtualBox nie jest jeszcze zielony: pozostaje
  `[TEST] fat32_persistence: FAIL` oraz trzeba domknąć deterministyczne
  raportowanie post-install smoke.
- Mbed TLS 3.6.7, X.509, entropy, SNI, RTC/trust validation i systemowy bundle
  CA są podłączone do ścieżki HTTPS. Aktualny runtime dochodzi do handshake,
  ale kończy się błędem TCP/BIO send (`net::Status::InterfaceError`).
- Terminal został oddzielony od Red Flux GUI: aktywny shell core nie zawiera
  komend `gui`, `home`, automatycznego `/gui/*` ani skrótów otwierających GUI.

## Zasady

- Roadmapa ma opisywać rzeczywisty stan źródeł i testów, a nie planowany marketing.
- Strategia branch/PR pozostaje decyzją właściciela projektu; niezależnie od niej
  `main` ma wracać do zielonego pełnego gate po każdej serii zmian.
- Nie oznaczamy jako ukończonych atrap Browser/Direct3D/network/hardware API.
- Dla VM/hardware host-test nie zastępuje runtime smoke na właściwym urządzeniu.
- Bounded buffers, jawne timeouty, cleanup per PID, NX/W^X i fail-closed pozostają
  inwariantami nowych subsystemów.

## Legenda

- ✅ ukończone i objęte bieżącą kwalifikacją;
- 🟡 działający fundament istnieje, ale brak pełnej kwalifikacji lub stabilności;
- ⬜ do wykonania;
- 🔒 wymaga zewnętrznego hosta/sprzętu i nie może zostać uczciwie zamknięte samym CI.

## R0 — build, repo i release qualification

- [x] ✅ UEFI x86-64 bootloader + boot protocol v3.
- [x] ✅ Powtarzalny build kernela oraz pełny host regression gate.
- [x] ✅ Linux IMG + UEFI ISO, El Torito EFI + GPT ESP verifier 20/20.
- [x] ✅ OVMF/QEMU optical boot smoke.
- [x] ✅ QEMU user-NAT qualification: E1000 + PCnet + VirtIO-net, DHCP + gateway + DNS.
- [x] ✅ Oracle VirtualBox x86-64: realny UEFI ISO boot do kernela.
- [ ] 🟡 VirtualBox Install -> SATA VDI -> reboot bez ISO działa do persistent ROOT,
  PID 1 i sieci, ale release-smoke blokuje `fat32_persistence: FAIL`.
- [x] ✅ VirtualBox NAT/PCnet: DHCP + gateway + DNS po bootowaniu z zainstalowanego VDI.
- [ ] 🔒 VirtualBox: osobny manualny Try -> Login -> Home qualification.
- [ ] 🔒 VirtualBox VirtIO-net: DHCP + gateway + DNS runtime smoke.
- [ ] 🔒 VirtualBox AC'97: słyszalny PCM runtime smoke.

## R1 — kernel, procesy i pamięć

- [x] ✅ VMM, PMM, heap, GDT/TSS/IST, IDT i izolacja Ring 3.
- [x] ✅ ELF64 ET_EXEC, prywatny CR3, NX/W^X i izolacja wyjątków userspace.
- [x] ✅ PID/TID, spawn/wait/exit, sleep/yield i PIT round-robin preemption.
- [ ] ⬜ Publiczne tworzenie wielu wątków w jednym procesie Ring 3.
- [ ] ⬜ Mutex/condvar/futex-like wait oraz pełne waitable synchronization primitives.
- [ ] 🟡 Publiczny monotoniczny zegar 100 Hz działa; brak high-resolution timer source
  i waitable timer objects.
- [ ] ⬜ Demand paging.
- [ ] ⬜ Copy-on-write.
- [ ] ⬜ File-backed mmap.
- [ ] ⬜ Swap/paging backend.
- [ ] ⬜ SMP i rzeczywiste wykorzystanie wielu CPU.

## R2 — filesystem i persistent application state

- [x] ✅ Writable FAT32/VFS persistent root.
- [x] ✅ Ring-3 open/read/write/append/close/stat/readdir.
- [x] ✅ Ring-3 create/unlink/rename/mkdir/rmdir/sync/seek.
- [x] ✅ Process-local cwd/chdir/getcwd i dziedziczenie cwd przy spawn.
- [x] ✅ Read-only live-package root odrzucający mutacje.
- [x] ✅ Instalator tworzy kanoniczne bazowe katalogi:
  `/bin`, `/boot`, `/dev`, `/etc`, `/home`, `/proc`, `/system`, `/system/bin`,
  `/tmp`, `/var`, `/var/log`.
- [x] ✅ ABI v1 świadomie nie implementuje symlinków/hard linków na FAT32;
  polityka jest zapisana w `docs/DEVELOPERS/FILESYSTEM_POLICY.md`.
- [ ] 🟡 Domknąć test trwałości pierwszego bootu: obecnie `fat32_persistence: FAIL`.
- [ ] ⬜ File-backed mmap po ukończeniu VM mapping API.
- [ ] ⬜ Model owner/group/permissions/ACL.
- [ ] ⬜ Settings/profile service zapisujący trwałe ustawienia aplikacji i profilu.
- [ ] ⬜ Recovery + transakcyjne aktualizacje systemu.

## R3 — IPC i sandbox

- [x] ✅ Generation-checked IPC endpoint/channel handles z `bind/connect/accept`.
- [x] ✅ Bounded message queues, backpressure, peer-close i cleanup per PID.
- [x] ✅ Shared-memory objects z owner/grant, NX mapping i refcount cleanup.
- [ ] 🟡 Waitable event objects działają jako fundament; nadal trzeba spiąć
  bezpośrednie wake-up z `Blocked` oraz readiness z IPC/async I/O.
- [ ] ⬜ Capability/permission model dla usług systemowych.
- [ ] ⬜ Sandbox primitives potrzebne przez browser/renderer model.

## R4 — networking

- [x] ✅ E1000 82540EM + Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS.
- [x] ✅ AMD PCnet jako runtime-selectable backend i kanoniczny profil VirtualBox.
- [x] ✅ VirtIO-net PCI backend zakwalifikowany runtime pod QEMU.
- [x] ✅ QEMU E1000/PCnet/VirtIO user-NAT smoke z DHCP + gateway + DNS.
- [x] ✅ Ring-3 network status snapshot i bounded HTTP GET.
- [ ] 🟡 TCP ma SND.UNA/SND.NXT/RCV.NXT, retransmisję tego samego sequence,
  bounded out-of-order reassembly, advertised window i większy stream receive buffer;
  nadal nie jest wystarczająco stabilny dla pełnej ścieżki TLS/public Web.
- [ ] 🟡 Mbed TLS 3.6.7: freestanding TLS 1.2/X.509, entropy, SNI, TCP BIO,
  RTC/time validation i trust-store parse są zintegrowane; handshake runtime nadal
  kończy się błędem BIO send/TCP.
- [ ] 🟡 Systemowy CA bundle jest dołączany do media z hostowego Web PKI i parsowany;
  brakuje natywnego lifecycle/update policy wewnątrz KuroganeOS.
- [ ] 🟡 Publiczne Ring-3 `ku_https_get()` istnieje, ale HTTPS nie jest jeszcze
  release-qualified end-to-end.
- [ ] ⬜ Publiczne async socket handles.
- [ ] ⬜ Connect/send/recv/close bez długiego blocking syscall.
- [ ] ⬜ DNS resolver jako async/public service.
- [ ] ⬜ Poll/event integration z IPC/thread wait primitives.
- [ ] ⬜ Dalsze fizyczne NIC backends, przede wszystkim popularny Realtek/Intel.

## R5 — audio

- [x] ✅ Intel ICH AC'97 `8086:2415` kernel PCM S16LE/stereo/48 kHz.
- [x] ✅ Ring-3 audio status, master volume/mute i bounded PCM playback.
- [ ] ⬜ Per-process audio stream handles i mixer.
- [ ] ⬜ Buffer scheduling / underrun handling dla ciągłego streamingu.
- [ ] ⬜ Format conversion/resampling.
- [ ] ⬜ Capture/microphone.
- [ ] ⬜ Intel HDA backend.

## R6 — terminal, shell i POSIX/libc compatibility

- [x] ✅ Podstawowy shell i uruchamianie ELF64.
- [x] ✅ Wspólny terminal shell core używany przez recovery console i GUI Terminal.
- [x] ✅ Shell core jest niezależny od Red Flux GUI; stary Flux-aware core został usunięty.
- [x] ✅ Historia, cwd, cat/read, run/open aplikacji, jobs/wait i podstawowe utility.
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
- [x] ✅ Piny desktopu są zapisywane do `/home/desktop.cfg`.
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
- [ ] 🟡 Publiczny bounded software surface runtime: XRGB8888, clear, clipped
  rect/line i integer triangle rasterizer.
- [ ] 🟡 Source-level Direct3D compatibility foundation dla 9/11/12 istnieje,
  ale nie jest Windows COM ABI ani pełnym DirectX.
- [ ] ⬜ Natywne surface/image/buffer/texture handles.
- [ ] ⬜ Per-window command buffer present + viewport/scissor/state.
- [ ] ⬜ Present/swap + synchronization/fences do WindowManagera.
- [ ] ⬜ Software 3D: depth buffer, textures i shader IR.
- [ ] ⬜ Pierwszy realny GPU command-submission backend.
- [ ] ⬜ D3D9 pełniejsza semantyka device/resources/state.
- [ ] ⬜ D3D11 device/context/resources/shaders.
- [ ] ⬜ D3D12 queues/lists/barriers/descriptors.
- [ ] ⬜ Dopiero później ocena Windows-compatible COM ABI.

## R9 — installer, security i accounts

- [x] ✅ Kernelowy GPT/FAT32 installer i Try/Install media.
- [x] ✅ Transakcyjny install flow: ROOT najpierw, UEFI boot payload publikowany później,
  sync + byte-for-byte installed payload verification.
- [x] ✅ Lokalny profil instalacji + DEV password verifier.
- [x] ✅ Bazowy katalogowy filesystem contract tworzony podczas instalacji.
- [ ] 🟡 Naprawić trwałość/marker pierwszego bootu raportowany przez
  `fat32_persistence: FAIL`.
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
- [x] ✅ VirtIO-net pod QEMU.
- [x] ✅ AC'97.
- [x] ✅ PCI + ACPI MADT/APIC discovery.
- [ ] ⬜ Stabilizacja xHCI/USB HID na większej liczbie konfiguracji.
- [ ] ⬜ NVMe jako równorzędny storage backend.
- [ ] ⬜ Intel HDA.
- [ ] ⬜ SMP/APIC interrupt routing jako aktywna ścieżka zamiast PIC fallback.
- [ ] 🔒 Szersza kwalifikacja na realnym sprzęcie UEFI.

## R11 — Chromium / Kurogane Web

- [x] ✅ BrowserContext / NavigationController / PlatformDelegate bootstrap.
- [x] ✅ Plain HTTP navigation + bounded redirects + bootstrap text renderer.
- [x] ✅ Edytowalny omnibox i search-capable URL resolver.
- [x] ✅ Writable filesystem foundation dla przyszłego profile/cache.
- [ ] 🟡 HTTPS request path jest podpięty do browsera i Ring-3 API; runtime dochodzi
  do TLS handshake, lecz obecnie kończy się na TCP/BIO send failure.
- [ ] 🟡 Wyszukiwanie przez HTTPS ma poprawny target i nie robi downgrade do HTTP,
  ale nie jest jeszcze funkcjonalne end-to-end.
- [ ] ⬜ Async socket ABI + event readiness.
- [ ] 🟡 Monotonic time i IPC/shared-memory/event foundations istnieją; brak
  userspace threads, pełnych wait primitives i timer objects dla Chromium task model.
- [ ] ⬜ libc++/Chromium `base` platform layer.
- [ ] ⬜ GN `target_os = "kurogane"` toolchain definition.
- [ ] ⬜ Build/run Chromium `base` + `url` smoke target.
- [ ] ⬜ `content_shell` browser process.
- [ ] ⬜ Blink renderer.
- [ ] ⬜ V8/JavaScript.
- [ ] ⬜ GPU compositing.

## Najbliższe TODO — priorytet

1. Naprawić `fat32_persistence: FAIL` i doprowadzić pełny VirtualBox
   install -> detach ISO -> VDI reboot smoke do jednoznacznego PASS.
2. Naprawić TCP/BIO send failure podczas Mbed TLS handshake; dodać regresję,
   która odtwarza realny handshake zamiast tylko hostowego TCP unit testu.
3. Zakwalifikować HTTPS end-to-end: DNS -> TCP -> TLS -> cert/hostname/time ->
   HTTP response, najpierw na stabilnym kontrolowanym endpointcie, potem public Web.
4. Wykonać realny VirtualBox VirtIO-net NAT smoke i zdecydować, czy PCnet pozostaje
   profilem domyślnym.
5. Zaprojektować async socket handles + async DNS + poll/event readiness bez
   długich blocking syscalls.
6. Domknąć userspace threads, mutex/condvar/futex-like wait i direct event wake-up.
7. Rozwinąć terminal/POSIX layer: pipes, redirection, environment, glob i errno/fd adapters.
8. Zbudować docelowy native surface/present/synchronization model dla WindowManagera.
9. Rozszerzyć hardware: xHCI/USB HID, NVMe, Intel HDA i popularne fizyczne NIC.
10. Dopiero na stabilnej sieci/threading/graphics rozwijać Chromium `base`,
    `content_shell`, Blink i V8.

Po każdym ukończonym etapie ten plik ma zostać zaktualizowany w tym samym
cyklu zmian, a kwalifikacja `main` musi pozostać zielona.
