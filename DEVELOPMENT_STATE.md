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
