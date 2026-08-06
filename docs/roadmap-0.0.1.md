# KuroganeOS — roadmap 0.0.1–1.0

Ten dokument opisuje rzeczywisty zakres pierwszego zintegrowanego wydania.
Znacznik „zrealizowano” oznacza ukończenie zakresu demonstracyjnego danego
kamienia milowego, nie implementację wszystkich funkcji spotykanych w
systemach ogólnego przeznaczenia.

| Wersja | Cel | Status w wydaniu 1.0 |
| --- | --- | --- |
| **0.0.1** | Boot + kernel + ekran | Zrealizowano |
| **0.1** | Zarządzanie pamięcią | Zrealizowano |
| **0.2** | Klawiatura + terminal | Zrealizowano |
| **0.3** | System plików | Zrealizowano jako RAMFS |
| **0.4** | Wielozadaniowość | Zrealizowano jako planista kooperacyjny |
| **0.5** | Sterowniki | Zrealizowano podstawowy zestaw |
| **0.6** | Sieć | Zrealizowano stos z loopbackiem |
| **0.7** | GUI | Zrealizowano podstawowe GUI framebufferowe |
| **0.8** | Framework | Zrealizowano framework aplikacji kernela |
| **0.9** | Aplikacje | Zrealizowano cztery aplikacje wbudowane |
| **1.0** | Pierwsze zintegrowane wydanie | Zrealizowano i uruchomiono w QEMU |

## 0.0.1 — boot, kernel i ekran

- [x] Bootloader jest aplikacją UEFI PE32+ dla AMD64.
- [x] Bootloader ładuje segmenty kernela ELF64.
- [x] Kernel otrzymuje zweryfikowany framebuffer GOP.
- [x] Protokół startowy przekazuje mapę pamięci i adres RSDP.
- [x] Bootloader wywołuje `ExitBootServices` przed przekazaniem sterowania.

## 0.1 — zarządzanie pamięcią

- [x] Kernel ma własną stertę z wyrównanymi alokacjami, dzieleniem i łączeniem
  wolnych bloków.
- [x] Bitmapowy alokator zarządza ramkami z ciągłego obszaru pamięci fizycznej.
- [x] Podczas startu wykonywany jest test alokacji i zwalniania pamięci.

Obecny kernel wybiera jeden największy, obsługiwany obszar pamięci dostępnej.
Nie ma jeszcze pamięci wirtualnej per proces ani stronicowania na żądanie.

## 0.2 — klawiatura i terminal

- [x] Działa IDT oraz obsługa wyjątków i IRQ.
- [x] PIC i PIT dostarczają przerwania oraz licznik czasu.
- [x] Sterownik klawiatury PS/2 przekazuje znaki do shella i aplikacji.
- [x] Terminal renderuje tekst w framebufferze i przewija ekran.
- [x] Wyjście diagnostyczne jest powielane na port szeregowy.

## 0.3 — system plików

- [x] RAMFS obsługuje hierarchiczne ścieżki, pliki i katalogi.
- [x] Dostępne są odczyt, zapis, tworzenie, listowanie i usuwanie.
- [x] Shell udostępnia polecenia `ls`, `cat`, `touch`, `mkdir`, `write`
  i `rm`.

RAMFS nie jest trwałym systemem plików. Dane istnieją tylko do restartu.

## 0.4 — wielozadaniowość

- [x] Planista obsługuje zadania jednorazowe i okresowe.
- [x] Dostępne są tworzenie, anulowanie, wstrzymywanie i wznawianie zadań.
- [x] Budżet wykonania chroni główną pętlę przed nieograniczonym dispatchingiem.

Jest to model kooperacyjny oparty na callbackach, bez przełączania kontekstu
CPU i bez preempcji.

## 0.5 — sterowniki

- [x] Framebuffer GOP i terminal.
- [x] Port szeregowy.
- [x] IDT, PIC i PIT.
- [x] Klawiatura PS/2.
- [x] Skanowanie konfiguracji PCI.
- [x] Zegar RTC.

To zestaw potrzebny do demonstracji w QEMU, a nie ogólna warstwa sterowników.

## 0.6 — sieć

- [x] Serializacja i walidacja ramek Ethernet II.
- [x] Obsługa ARP, IPv4 i komunikatów ICMP echo.
- [x] Tablica sąsiadów oraz statystyki stosu.
- [x] Interfejs loopback z adresem `127.0.0.1`.
- [x] Test `net ping` działający wewnątrz kernela.

Nie ma sterownika fizycznego NIC ani komunikacji poza maszyną.

## 0.7 — GUI

- [x] Prymitywy rysowania i podstawowe elementy interfejsu.
- [x] Pulpit uruchamiany poleceniem `gui`.
- [x] Obsługa klawiatury w aplikacjach.

GUI nie ma myszy, okien nakładających się ani akceleracji sprzętowej.

## 0.8 — framework aplikacji

- [x] Rejestr do 16 aplikacji kernela.
- [x] Callbacki startu, klawiatury, timera i zatrzymania.
- [x] Uruchamianie aplikacji przez `run <nazwa>`.
- [x] Powrót do terminala po zamknięciu aplikacji.

Framework działa wyłącznie w przestrzeni kernela i nie stanowi ABI dla
programów użytkownika.

## 0.9 — aplikacje

- [x] `desktop` — prosty launcher graficzny.
- [x] `monitor` — podgląd pamięci, czasu i urządzeń.
- [x] `files` — przeglądarka zawartości RAMFS.
- [x] `about` — informacje o systemie.

## 1.0 — pierwsze zintegrowane wydanie

- [x] Powtarzalny build kernela i bootloadera przez `scripts/build.ps1`.
- [x] Automatyczne przygotowanie drzewa startowego UEFI w `iso`.
- [x] Walidacja ELF64 kernela oraz PE32+ bootloadera podczas budowania.
- [x] Start przez UEFI/EDK2 w QEMU.
- [x] Testy startowe pamięci, przerwań, urządzeń i loopbacku.
- [x] Osiągnięcie interaktywnego promptu shella.

Wydanie 1.0 jest stabilnym punktem odniesienia dla obecnego scenariusza QEMU,
ale nie jest systemem produkcyjnym.

## Kierunki po 1.0

Następne prace powinny objąć:

- tablice stron, ochronę pamięci i tryb użytkownika;
- preempcję oraz pełne przełączanie kontekstu zadań;
- trwały system plików i sterowniki AHCI/NVMe;
- sterownik karty sieciowej oraz UDP, TCP, DHCP i DNS;
- USB, mysz, audio i lepszy model urządzeń;
- bardziej rozbudowany GUI i izolowane aplikacje;
- uruchamianie na wielu implementacjach UEFI i rzeczywistym sprzęcie.
