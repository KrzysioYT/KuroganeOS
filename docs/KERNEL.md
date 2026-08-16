# Kernel KuroganeOS

## Model

Kernel jest monolityczny, freestanding i kompilowany jako PIE dla x86-64. Wszystkie usługi, shell i widoki graficzne wykonują się w ring 0. GDT zawiera przygotowane selektory użytkownika, a moduł ABI opisuje przyszły kontrakt, lecz obecnie nie istnieje przejście do ring 3, syscall ani proces.

Obecna pętla działa na jednym CPU. Projekt nie uruchamia AP ani nie posiada SMP. Aktywny backend VMM wykonuje lokalne `invlpg`; nie ma jeszcze mechanizmu cross-CPU shootdown.

## Inicjalizacja

`kmain` traktuje dane loadera jako niezaufane i sprawdza kompletność protokołu, mapy pamięci i framebufferu. Następnie uruchamia moduły w kolejności opisanej w [BOOT_PROCESS.md](BOOT_PROCESS.md). Wynik inicjalizacji PMM, RAMFS, schedulera, aplikacji, timera, klawiatury i normalnego loopbacku jest sprawdzany; błąd wymaganego modułu nie jest ignorowany.

Kernel publikuje na serialu i terminalu znaczniki self-testów. `ALL_REQUIRED_TESTS_PASSED` oznacza wyłącznie przejście wymagań rozruchowych aktualnego kernela, nie kompletność systemu operacyjnego.

## Deskryptory i przerwania

- GDT zawiera segmenty kernela, przygotowane segmenty użytkownika oraz deskryptor TSS.
- `TSS.RSP0` używa dedykowanego, wyrównanego 64 KiB entry stack zamiast boot stack; setter odrzuca zero, adres niekanoniczny i złe wyrównanie.
- TSS ustawia `RSP0` i trzy stosy IST: double fault, NMI oraz machine check.
- IDT ma 256 wpisów, wspólne stuby zapisujące rejestry i centralny dispatcher.
- Legacy PIC jest przemapowany na wektory `0x20`–`0x2f`, obsługuje maskowanie i spurious IRQ7/IRQ15.
- PIT pracuje z żądaną częstotliwością 100 Hz; IRQ0 tylko aktualizuje zegar i gotowość callbacków.
- Klawiatura używa IRQ1, z ograniczoną konfiguracją kontrolera PS/2 i ścieżką polling fallback.

Handler wyjątku drukuje nazwę, wektor, kod błędu, `RIP`, stos, flagi, rejestry ogólnego przeznaczenia i `CR2` dla page fault. Ograniczony stack trace śledzi do 16 ramek wyłącznie wewnątrz znanego stosu kernela. Diagnostyka pokazuje `PID0:TID0`, ponieważ prawdziwy model procesów i wątków jeszcze nie istnieje. Rekurencyjna panika jest wykrywana i kończy się natychmiastowym zatrzymaniem.

## Pamięć

### Heap kernela

Kernel ma statyczną pulę 2 MiB. Allocator obsługuje wyrównane `kmalloc`/`kfree`, dzielenie i scalanie bloków oraz statystyki. Nie jest to heap per proces, nie rośnie przez mapowanie nowych stron i nie zapewnia synchronizacji SMP.

### Pamięć fizyczna

PMM wybiera największy region `USABLE` z mapy UEFI, ograniczony pojemnością statycznej bitmapy 128 KiB, czyli 1 048 576 ramek 4 KiB. Zarządza tym jednym ciągłym zakresem. Self-test rezerwuje dwie różne ramki, sprawdza je i oddaje.

To nie jest pełny zarządca całej mapy fizycznej: pozostałe regiony użyteczne nie są łączone w jedną pulę, a aktywne mapowania firmware/kernela nie są przebudowywane przez ten moduł.

### Pamięć wirtualna

`kernel/memory/virtual_memory.*` implementuje czteropoziomowy walker x86-64 dla stron 4 KiB: inicjalizację przestrzeni, `map_page`, `unmap_page`, `query_page` i `translate`. Backend dostarcza przydział i zwolnienie tablic, dostęp fizyczny→wirtualny oraz synchroniczną invalidację TLB. Kod waliduje adresy kanoniczne, wyrównanie, szerokość adresu fizycznego, konflikty huge pages i uprawnień. Operacje alokujące wycofują częściowe zmiany po OOM; puste tablice są odłączane, invalidowane i dopiero zwalniane.

`kernel/memory/kernel_virtual_memory.*` integruje walker z uruchomionym kernelem w ograniczonym modelu preview. Odrzuca 5-level paging, odczytuje i maskuje fizyczny korzeń z bieżącego `CR3`, kopiuje PML4 do nowej ramki PMM i przełącza procesor na ten prywatny root. Wykrywa szerokość adresu przez CPUID, używa PMM do nowych tablic i lokalnego `invlpg`. Ponieważ niższe tablice, pamięć fizyczna i ich dostęp nadal pochodzą z mapowań UEFI, adapter jawnie polega na identity mapping pozostawionym przez firmware.

Self-test wybiera wolny kandydat w górnej połowie przestrzeni, mapuje jedną ramkę PMM, sprawdza translację i wspólny wzorzec danych przez alias wirtualny, następnie usuwa mapowanie i potwierdza brak wycieku ramek. Nie nadpisuje istniejącego wpisu.

Istotne ograniczenia integracyjne:

- kernel ma prywatną kopię PML4 i przełącza `CR3`, ale nie buduje od zera niższych tablic ani kompletnej, chronionej mapy kernela;
- poprawność adaptera zależy od identity mappingu pamięci fizycznej pozostawionego przez konkretne firmware;
- caller musi zapewnić zewnętrzną synchronizację;
- aktywny callback wykonuje tylko lokalne `invlpg`; przed SMP potrzebny jest synchroniczny cross-CPU shootdown;
- moduł rezerwuje bit software 9 we wpisach niebędących liśćmi do oznaczania własnych tablic;
- przy przekazywaniu aktywnego korzenia należy odseparować od `CR3` bity flag/PCID.

Ta integracja potwierdza działanie podstawowego mapowania stron na aktualnej platformie startowej, ale nie oznacza ochrony sekcji kernela, HHDM, izolacji ani przestrzeni procesów.

## Scheduler i główna pętla

Scheduler przechowuje do 32 nazwanych callbacków: jednorazowych lub okresowych. Obsługuje stan oczekiwania/gotowości/wykonania/wstrzymania, anulowanie, budżet dispatchu i metryki. `tick()` nie uruchamia kodu zadania w IRQ. `run_pending()` wywołuje callback już w kontekście pętli kernela.

Nie ma osobnych stosów, zapisu rejestrów, kwantów procesora ani preempcji. Callback musi wrócić. `yield()` tylko planuje ponowny dispatch.

Główna pętla:

- przetwarza nowe tyknięcie i polling loopbacku;
- wykonuje maksymalnie osiem gotowych callbacków na iterację;
- odczytuje wszystkie dostępne znaki klawiatury;
- kieruje je do aktywnej aplikacji albo shella;
- wykonuje `hlt`, gdy nie ma pracy i przerwania są gotowe.

## Usługi kernela

- Hierarchiczny RAMFS: [FILESYSTEM.md](FILESYSTEM.md).
- Shell i brak userspace: [USERSPACE.md](USERSPACE.md).
- Framebuffer oraz widoki: [GUI.md](GUI.md).
- Stos Ethernet/ARP/IPv4/ICMP działa obecnie z loopbackiem, nie z urządzeniem sieciowym.
- `kernel/diagnostics/profiler.*` zbiera spójne snapshoty statystyk modułów dla testów i dalszej diagnostyki; nie jest profilerem próbkowym CPU.

Pełna lista braków znajduje się w [CURRENT_LIMITATIONS.md](CURRENT_LIMITATIONS.md).
