# KuroganeOS — stan rozwoju

Aktualizacja: 2026-10-25. Gałąź robocza: `codex/road-to-15-stability`,
[Draft PR #55](https://github.com/KrzysioYT/KuroganeOS/pull/55).
Bazą integracji jest `gpt/road-to-15-consolidation`.

## Wersja i najbliższy cel

Kod w `common/version.h` identyfikuje system jako **7.0.0-dev**.
Bieżący priorytet to **domknięcie Flux Modern 8.0**, przy zachowaniu regresji
kernela, sterowników i Classic. **15.0.0 jest docelowym pierwszym wydaniem
produktowym stable. Ani 8.0, ani 15.0 nie są obecnie ukończone.**
Numer etapu roadmapy, wersja skompilowanego obrazu i wynik konkretnego testu
są odrębnymi informacjami.

## Zasady projektu

- Własny kernel x86-64 UEFI, sterowniki, Ring-3 ELF64, usługi i SDK.
- Sprzęt opisują możliwości i identyfikatory urządzeń; VM jest profilem testowym,
  nie architekturą systemu. Brak opcjonalnego urządzenia ma degradować funkcję.
- Zachowujemy Windows/WSL, Linux oraz macOS jako ścieżki rozwoju.
  Apple Silicon uruchamia gościa x86-64 przez emulację QEMU/TCG.
- Działające logowanie, rzeczywisty proces i widoczne okno mają pierwszeństwo
  przed zmianą numeru wersji. Nie omijamy autoryzacji kont ani sesji.
- Nie kasujemy istniejących VDI, profili ani danych jako sposobu naprawy.
- Kod, build, test hostowy, QEMU, VirtualBox i fizyczny sprzęt mają oddzielne
  dowody. PASS dotyczy wskazanego SHA i profilu, nie wszystkich komputerów.
- Niedostępny test pozostaje UNVERIFIED; nie blokuje niezależnej pracy,
  ale nie zwalnia z obowiązkowego testu przed zamknięciem etapu.

## Potwierdzone przyrosty i ograniczenia dowodów

| Zakres | Źródło | Wynik / granica |
| --- | --- | --- |
| Steel 5.0 po audycie współistnienia | `569bae4c33aa0c04db87f0cbe785da1aa45cc084`, run `37661212373` | Historyczna kwalifikacja zakresu 5.0 |
| Core Steel 6.0 | `717e003b34d47c81495e8ac5c580ae57c5fb16b3`, run `37710493815` | Historyczna kwalifikacja kernela i SMP |
| Obsługiwany podzbiór urządzeń 7.0 | `faceb4526acbbee3bc3ab544d7af76f12132766d`, run `40055252545` | Podzbiór, nie całe 7.0 ani fizyczny sprzęt |
| Flux: Login, Home, Files, Terminal, USB-only + host sanitizer | `7f06237be046f782e8c4bb18ef097aab6f34988a`, run `40163835705` | Dwa zadania host/QEMU zakończone sukcesem; nie pełna akceptacja 8.0 |
| Windows: komplet 33 programów i zakaz SIMD w userspace | `9c8700ce5129f716fabe9558fe7050aaedf8b5ae` | 5 testów zgodności + Linux release --no-image PASS; workflow Flux 8 `40164065199` PASS; pełny natywny Windows/VirtualBox nadal niezweryfikowany |

Wyniki Actions: [Flux QEMU i host](https://github.com/KrzysioYT/KuroganeOS/actions/runs/40163835705),
[kwalifikacja poprawki Windows](https://github.com/KrzysioYT/KuroganeOS/actions/runs/40164065199).
Workflow `40164065199` zakończył się sukcesem dla `9c8700c`. Dokument nie nadaje
nowym commitom statusu PASS z poprzedniego uruchomienia.

## Otwarte blokery

1. VirtualBox: zgłoszone restarty usług i `flux_account_lookup: FAIL` na starym
   zainstalowanym obrazie. Brak 22 programów w buildzie Windows został naprawiony,
   ale związek z całym incydentem wymaga świeżego logu. Nowe ISO nie aktualizuje
   automatycznie plików starego zainstalowanego rootfs.
2. Pełna akceptacja Flux: wszystkie aplikacje, lifecycle, focus/capture,
   restart/recovery, clipboard, interakcje i trwałość ustawień na tym samym SHA.
3. Skalowalne fonty, HiDPI i zmiany trybu wyświetlania, dostępność, drag/drop
   oraz pozostałe otwarte pozycje kontraktu 8.0.
4. Produktowe security/KDF, aktualizacje/rollback, szersza zgodność sprzętowa
   i długotrwałe testy pozostają pracą do 15.0, nie ukończonymi funkcjami.

## Dokumenty nadrzędne

- [Indeks dokumentacji](docs/README.md).
- [Jedyna numeracja etapów](docs/roadmap/MASTER_ROADMAP_15.md).
- [Kontrakt ukończenia 8.0](docs/roadmap/FLUX8_MODERN_COMPLETION.md).
- [Plan realizacji](docs/roadmap/KUROGANEOS_REBUILD_7_TO_15.md).
- [Historyczne dowody etapów](docs/ROAD_TO_15_STATUS.md).

Historyczny QUALIFIED oznacza zakres wskazanej rewizji, a nie bezwarunkowe
potwierdzenie obecnego HEAD. Oryginalne audyty i informacje o wydaniach zachowują
daty i znaczenie historyczne.

## 2026-10-25 — macOS runtime image parity (active)

macOS image creation was independently reported to omit 22 baseline Ring-3
programs despite passing image/UEFI tests. `scripts/build-macos.sh` is being
aligned with the 33-program Linux/Windows inventory; a mandatory ELF/overlay
preflight rejects missing accountd, sessiond, notificationd and GUI applications
before IMG/ISO publication. Actual macOS host execution and boot verification
remain UNVERIFIED until tested on the matching source SHA.

## 2026-10-25 — Try startup and no-target recovery (working tree)

The current uncommitted work is based on `3da0696`; evidence below is useful
engineering progress, not an exact-SHA release qualification.

- Normal non-safe startup now initializes the compositor before PID 1 starts
  Login. If optional compositor initialization fails, PID 1 retains a visible
  recovery path; explicit desktop-only startup remains fatal on that error.
- Setup-media Try no longer requires a writable target disk. Selecting Install
  without a target mounts the read-only live package, explains why installation
  is unavailable, and waits for input before entering Try without writing media.
- Both Foundation builders now inspect the published FAT32/VFS root and require
  all 44 declared runtime ELF64 paths, including account/session services and
  GUI apps. Windows/Linux/macOS inventory parity has host regression coverage;
  those three native builder environments have not all been executed here.
- On the arm64 macOS host, the optimized host suite passed under Rosetta.
  The root-volume validator and a targeted UBSan build both passed against the
  isolated release IMG; the clean media build and those image checks are from a
  staging worktree, not yet a committed source SHA.
- QEMU/TCG verified setup ISO Try and no-disk Install recovery, plus USB-only
  Login -> authenticated session -> first Home frame. A live ISO interaction
  opened Terminal from the Dock, presented its native surface and produced a
  QMP screenshot. A prior Foundation IMG run also passed Files/Terminal launch,
  wheel input and two presented surfaces.
- Two later Foundation IMG retries stopped after `userspace ABI probe passed`,
  before the Ring-3 preemption acceptance marker. This unresolved intermittent
  boot failure blocks reliability claims despite the earlier successful run.
- Actual 1280x800 guest screenshots for setup, Login, Home, Files and Terminal
  were visually inspected. The available boot capture is firmware/loader text,
  not a graphical OS splash; no graphical splash/progress implementation is
  evidenced. The visible UI remains on transitional bitmap typography.

The complete Flux redesign and 8.0 qualification remain open: licensed scalable
font and full text layout, HiDPI/responsive behavior, app/window lifecycle and
failure recovery, settings persistence, blank-disk install/boot, VirtualBox
interactive acceptance, and the remaining acceptance matrix. No version
promotion is implied.

## Exact-SHA runtime checkpoint — 4ca69f57

Source: `4ca69f5797904f4152ad4a74ef3b8ceb3d807847` on
`codex/road-to-15-stability`. The host was Apple Silicon macOS `arm64`;
x86-64 guest runs used QEMU/TCG. The clean release build and all generated
media were kept in an isolated worktree at
`/tmp/kuroganeos-release-4ca69f57`.

| Evidence | Result |
| --- | --- |
| macOS release IMG and ISO build | PASS; both generated from the exact source SHA |
| Published root FAT32/VFS payload | PASS; all 44 required ELF64 programs validated in the image |
| `arch -x86_64 /bin/bash scripts/run-host-tests.sh` | PASS; optimized host regression suite, including package VFS listing; one pre-existing mbedTLS `x509_crt.c` compiler warning |
| `bash scripts/validate-foundation-root-image.sh dist/KuroganeOS-7.0.0-dev-macos-qemu.img` | PASS; 44 required ELF64 payloads |
| Targeted root-image test with `-fsanitize=undefined -fno-sanitize-recover=all` | PASS |
| IMG QEMU/TCG, USB keyboard + tablet, no PS/2 | PASS; Try → Login → authenticated owned session → first Home frame; Files enumerated and received wheel input; Terminal launched as Ring-3; two child spawns and two presented surfaces |
| ISO QEMU/TCG, USB keyboard + tablet, no PS/2 | PASS; Install without a target disk showed the explicit refusal, entered the read-only live package and reached the authenticated Home frame |

Generated media and SHA256:

- `dist/KuroganeOS-7.0.0-dev-macos-qemu.img` —
  `48723c4f84de9d28897cafc4fb5bf2862d435b0a8112855de64de86ad0a856cf`
- `dist/KuroganeOS-7.0.0-dev-x86_64.iso` —
  `53586c4f5fc98a8a983c9ab36d439de3ab41bbdd56740deb15c58930b674cd14`

QMP guest screenshots for setup refusal, Login, Home, Files and Terminal were
captured from this build and visually reviewed. They show the existing bitmap
typography and no graphical OS splash; these images do not constitute full
layout, accessibility or clipping acceptance. The VFS change fixes the
reproduced live-Try Files failure: directory enumeration now works through
the read-only package adapter, allowing Files to present a surface.

This exact candidate did not receive complete 8.0 acceptance. In particular,
the blank-disk install/boot, VirtualBox interaction, persistence across
restart, full window/input/clipboard lifecycle, crash and service recovery
matrix, licensed scalable typography, HiDPI, and the complete application
suite remain unverified or incomplete. Version remains `7.0.0-dev`; 8.0 is
**ACTIVE / NOT QUALIFIED** and 15.0.0 stable remains the product goal.

## 2026-10-27 — Road to 15: kernel-host portability and FAT32 packaging

On `58c98d62`, the exact-SHA GitHub checks returned 10 successes and 24
failures. Many failed independently at the same `kernel/core/log.hpp`
hosted-GCC `-Werror=builtin-declaration-mismatch` warning. The TLS HTTP
harness separately failed to link the kernel diagnostic functions. These
were test/toolchain integration defects; this result is **not** evidence of
24 separate defective hardware drivers.

New candidate commits:
- `b76577cf`: narrowly scope the hosted compiler warning to the legacy
  kernel logging namespace, supply a real host TLS diagnostic sink and tighten
  the VirtualBox documentation assertion.
- `facb1593`: reject case-insensitive FAT32 file aliases and file-as-directory
  collisions in both installer packager and kernel package parser.
- Current CI additionally tests package reading under UBSan, with a hostile
  path matrix in Python and C++.

Observed during candidate runs: `http-lifecycle`, `host-regression` and
multiple service checks passed on `b76577cf`; most hardware/QEMU jobs were
still in progress when recorded. These earlier successes **do not qualify
the new head**, which requires its own checks. No clean-boot evidence for the
new head was collected here. Compiler, parser and unit tests are necessary,
but not sufficient for product-level 15.0.0. The version remains `7.0.0-dev`.

### 2026-10-27 — HTTP host logger dual-linkage repair

After the prior hosted compiler fix, standalone `http-lifecycle` passed, but
17 broader CI jobs converged on a **new** link failure in
`tests/test_tls_http_io.cpp`: its local `log::write/write_hex` definitions
collided with `tests/host_log_stub.cpp` supplied by the complete host suite.
The test-only logger functions are now weak where supported so the shared
host fixture overrides them; the test's HTTP response/result assertions
remain in place. Exact-SHA CI must revalidate the fix before any qualification.
