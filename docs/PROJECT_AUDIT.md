# Audyt projektu KuroganeOS

Data audytu: 6 sierpnia 2026 r.  
Audytowana gałąź robocza: lokalny katalog `E:\KuroganeOS` (repozytorium nie zawiera metadanych Git).

## Podsumowanie

KuroganeOS jest rzeczywistym, samodzielnym projektem systemowym dla x86-64. Nie używa kernela Linux podczas rozruchu. W repozytorium znajduje się własny loader UEFI, kernel freestanding, sterowniki niskiego poziomu, allocator, RAMFS, powłoka oraz proste aplikacje rysowane bezpośrednio do framebufferu.

Stan zastany nie odpowiadał jednak opisowi stabilnego systemu użytkowego. Najważniejsza różnica polega na tym, że wszystkie funkcje wykonują się w ring 0 i w jednej przestrzeni adresowej. Nie istnieją jeszcze procesy użytkownika, przełączanie kontekstu, interfejs syscall, izolacja pamięci, VFS z trwałym nośnikiem ani pełny stos GUI. Wbudowany „desktop” jest zestawem pełnoekranowych widoków kernela, a scheduler obsługuje wywołania zwrotne, nie konteksty CPU.

Najkrótsza bezpieczna droga rozwoju to zachowanie działającego toru UEFI i modułów możliwych do testowania, utwardzenie kernela, dodanie pamięci wirtualnej, a dopiero potem budowa procesów i userspace. Próba równoczesnego dodania trwałego systemu plików i rozbudowanego pulpitu utrudniłaby wykrywanie regresji.

## Zakres i metoda

Sprawdzono:

- strukturę źródeł, dokumentację i artefakty;
- loader UEFI, protokół startowy, ELF PIE i linker script;
- wejście kernela, GDT/IDT, wyjątki, PIC, PIT i PS/2;
- allocator sterty, manager ramek fizycznych oraz rozpoczęty manager stron;
- RAMFS, shell, scheduler, warstwę aplikacji, framebuffer, RTC, PCI i sieć;
- skrypty PowerShell/Bash, generowanie FAT32 i ISO;
- testy hostowe i testy rozruchowe przez port szeregowy;
- dostępność QEMU, WSL2, narzędzi obrazu oraz VirtualBox.

Pierwszy niezmieniony build wykonano poleceniem:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
```

Build zakończył się powodzeniem, a obraz uruchomił się w QEMU z UEFI. Automatyczny test powłoki na obrazie dysku oraz test ISO również osiągnęły prompt i nie wykryły paniki. Oznacza to, że problemem nie była całkowita niesprawność kompilacji, lecz luki architektoniczne i kilka błędów niezawodności ukrytych przez zbyt słabe testy.

## Struktura i języki

Istotne katalogi:

- `boot/efi` — samodzielny loader UEFI w C;
- `common` — wspólny protokół startowy i wersja;
- `kernel/arch/x86_64` — wejście asemblerowe, GDT, IDT i stuby ISR;
- `kernel/memory` — heap, bitmapowy PMM i manager stron;
- `kernel/drivers` — serial, framebuffer, PIC, PIT, PS/2, PCI i RTC;
- `kernel/fs` — pamięciowy system plików;
- `kernel/task` — kooperacyjny scheduler zadań zwrotnych;
- `kernel/net` — stos pakietów testowany głównie na loopback;
- `kernel/apps` i `kernel/ui` — aplikacje i renderer pełnoekranowy;
- `kernel/shell` — powłoka kernela;
- `sdk` — nagłówki eksperymentalnego ABI i generator projektu;
- `scripts` — build, obraz FAT32, ISO, QEMU i testy hostowe;
- `tests` — testy uruchamiane jako procesy hosta.

Kod właściwy jest napisany w C++, C, asemblerze GNU, PowerShell, Bash i Pythonie. Dołączone lokalnie toolchainy QEMU i GCC są duże, ale nie są częścią wykonywanego systemu.

## Elementy działające

### Rozruch i ABI

- Loader jest natywną aplikacją PE/COFF UEFI x86-64.
- Loader odnajduje `kernel.elf`, waliduje ELF64, ładuje segmenty `PT_LOAD`, stosuje obsługiwane relokacje PIE i przekazuje sterowanie do kernela.
- Przekazywane są: framebuffer GOP, znormalizowana mapa pamięci, zakres fizyczny kernela i flagi startowe.
- Wyjście z Boot Services jest wykonywane z ponowieniem po zmianie klucza mapy pamięci.
- Stos kernela jest ustawiany w kodzie startowym, a kierunek napisów i stan rejestrów istotny dla ABI są normalizowane.

### Diagnostyka i przerwania

- Port COM1 jest inicjalizowany wcześnie i służy do automatycznej oceny rozruchu.
- Istnieje 256-wpisowa IDT oraz stuby zapisujące pełną ramkę rejestrów.
- PIC jest remapowany, PIT zapewnia monotoniczne ticki, a klawiatura PS/2 obsługuje IRQ i awaryjne odpytywanie.
- Dodano GDT, 64-bitowy TSS oraz osobne stosy IST dla double fault, NMI i machine check.
- Panic raportuje wektor, kod błędu, RIP, RSP, RBP, CS, RFLAGS, CR2 dla page fault i rejestry ogólne. PID/TID pozostają zerowe, ponieważ nie ma jeszcze modelu procesów.

### Pamięć

- Kernel posiada statyczną stertę z wyrównanymi alokacjami i operacjami odpowiadającymi `malloc`, `calloc`, `realloc` i `free`.
- PMM wykorzystuje bitmapę ramek 4 KiB z największego obsługiwanego regionu wolnej pamięci przekazanego przez UEFI.
- Testy hostowe sprawdzają wielokrotne alokacje, wyrównanie, ponowne użycie, duże bloki i podstawowe zachowanie przy błędach.
- Manager pamięci wirtualnej jest wdrażany jako niezależny, testowalny walker czteropoziomowych tablic x86-64. Jego integracja z aktywnym CR3 jest osobnym etapem i nie jest w tym raporcie oznaczona jako ukończona.

### Pliki i terminal

- RAMFS rzeczywiście przechowuje katalogi, pliki i dane w pamięci kernela.
- Dostępne są walidowane operacje tworzenia, odczytu, zapisu, usuwania, kopiowania, przenoszenia i zmiany nazwy.
- Ścieżki bezwzględne i względne są kanonizowane; shell posiada rzeczywisty katalog roboczy.
- Skrócenie pliku odzyskuje niepotrzebną pojemność zamiast trwale zużywać pulę RAMFS.
- Shell obsługuje między innymi `help`, `clear`, `echo`, `pwd`, `cd`, `ls`, `cat`, `touch`, `write`, `mkdir`, `rmdir`, `cp`, `mv`, `rm`, `stat`, `free`, `uptime`, `date`, `uname`, `whoami`, `history`, `version`, `reboot` i `shutdown`.
- Lista `help` nie reklamuje poleceń, których parser nie obsługuje.

### Pozostałe moduły

- Framebuffer GOP i renderer tekstu działają dla obsługiwanych formatów 32-bitowych.
- PCI jest enumerowane mechanizmem configuration I/O.
- RTC dostarcza datę i czas po walidacji stabilnego odczytu.
- Stos sieciowy posiada testowane struktury Ethernet, ARP, IPv4 i ICMP, lecz aktywny transport kernela jest wyłącznie loopback. UDP, DHCP i DNS nie są zaimplementowane.
- Wbudowane widoki `desktop`, `monitor`, `files` i `about` wykonują rzeczywiste odczyty lub operacje na istniejących usługach kernela, ale nie są osobnymi procesami. Terminal jest ekranem powłoki framebufferowej; osobna aplikacja Terminal i aplikacja Settings nie istnieją.
- Kooperacyjny scheduler zadań zwrotnych ma identyfikatory, stany, budzenie, anulowanie i budżet wykonania.

## Elementy częściowo działające

### Manager pamięci fizycznej

PMM zarządza tylko jednym wybranym regionem UEFI i jest ograniczony statyczną pojemnością bitmapy. Nie agreguje wszystkich wolnych regionów, nie śledzi typów cache ani NUMA i nie rezerwuje zakresów dynamicznie po starcie. Jest wystarczający dla obecnego małego kernela, ale wymaga przebudowy przed ładowaniem procesów.

### Pamięć wirtualna

Loader/firmware pozostawia aktywne mapowanie, dzięki któremu kernel działa z adresami przekazanymi przez UEFI. To nie jest zarządzana przestrzeń wirtualna KuroganeOS. Moduł mapowania 4 KiB ma testowany backend, lecz dopóki kernel nie przejmie CR3 i nie zbuduje własnej mapy kernela, nie wolno deklarować ochrony stron ani izolacji.

### Scheduler i model aplikacji

Scheduler wykonuje funkcje do zakończenia i nie zapisuje kontekstu CPU. Nie zapewnia wywłaszczania, stosów per zadanie, ring 3, procesów potomnych, sygnałów ani ochrony kernela przed aplikacją. Nazwy „task” i PID w widokach diagnostycznych nie oznaczają procesów systemowych.

### System plików

RAMFS jest funkcjonalny, lecz ulotny i nie ma warstwy VFS, inode, punktów montowania, uprawnień, deskryptorów per proces ani sterownika blokowego. Katalogi `/system`, `/home`, `/apps`, `/etc`, `/tmp` i `/var` porządkują dane sesji, ale nie są odtwarzane z dysku.

### GUI

Renderer rysuje pulpit i pełnoekranowe aplikacje, lecz nie ma kursora myszy, niezależnych powierzchni, okien, przesuwania, zmiany rozmiaru, minimalizacji, warstw, damage tracking ani compositora. Obecne widoki należy traktować jako konsolę framebufferową, nie kompletny pulpit.

### Sieć

Kod protokołów jest testowalny, ale nie istnieje sterownik karty E1000/VirtIO, DMA ani kolejki RX/TX. Polecenie ping potwierdza działanie loopback, nie łączność z siecią QEMU lub hostem.

### SDK/ABI

Nagłówki i generator projektu definiują kierunek przyszłego ABI, ale nie istnieje loader programów użytkownika ani transport syscall. Przykłady są testami kompilacji, nie programami uruchamianymi przez KuroganeOS.

## Elementy brakujące lub niedziałające

Poniższe elementy nie miały rzeczywistej implementacji w audytowanym stanie:

- ring 3, proces `init` i procesy użytkownika;
- przełączanie kontekstu i scheduler wywłaszczający;
- wejście/wyjście syscall oraz walidacja wskaźników użytkownika;
- osobne przestrzenie adresowe i ochrona kernela przed aplikacją;
- VFS, punkty montowania, urządzenia plikowe i trwały zapis;
- sterownik AHCI, VirtIO-blk lub kontroler dysku VirtualBox;
- sterownik myszy PS/2;
- ACPI, kontrolowane wyłączenie przez ACPI i pełna detekcja platformy;
- sterownik sieciowy, prawdziwy DHCP/DNS i sockety;
- compositor, window manager i terminal działający jako proces;
- edytor tekstu jako niezależna aplikacja;
- użytkownicy, grupy, uprawnienia i uwierzytelnianie;
- `mount`, `unmount`, pełne `ps`/`kill`, potoki, przekierowania, środowisko i skrypty powłoki;
- instalator i zapis zmian między restartami;
- potwierdzona kompatybilność z realnym sprzętem.

## Błędy i ryzyka wykryte podczas audytu

1. Brakowało jawnej inicjalizacji GDT/TSS i stosów awaryjnych. Wyjątek w uszkodzonym kontekście mógł zakończyć się potrójnym błędem zamiast diagnostyką.
2. Self-test PMM mógł zostać zaliczony mimo braku gotowego managera ramek. Inicjalizacja pamięci i RAMFS ignorowała część błędów zwrotnych.
3. Inicjalizacja klawiatury zgłaszała sukces po odmaskowaniu IRQ nawet wtedy, gdy konfiguracja kontrolera PS/2 się nie powiodła.
4. Obcięcie pliku w RAMFS nie zwalniało przydzielonej pojemności, co prowadziło do wyczerpania stałej puli po powtarzanych zapisach.
5. Shell nie miał semantyki CWD ani atomowych operacji `cp`/`mv`, przez co interfejs przypominał Unix bez zapewnienia jego podstawowych właściwości.
6. Obraz FAT32 wpisywał błędny numer klastra rodzica dla wpisu `..` katalogu `/EFI`; `fsck.fat` wykrywał niespójność.
7. ISO zawierało tylko obraz El Torito, bez czytelnej kopii `EFI/BOOT` w drzewie ISO. Część firmware i narzędzi mogła go nie rozpoznać.
8. Skrypty deklarowały konfiguracje debug/release i incremental build mniej precyzyjnie, niż wynikało z użytych flag i fingerprintu.
9. Test powłoki sprawdzał głównie pojawienie się promptu, a nie wynik operacji na ścieżkach, plikach, sieci i aplikacjach.
10. Istniejąca dokumentacja i artefakty `dist` sugerowały bardziej kompletny pulpit niż faktycznie zapewnia kod.

## Moduły zachowane

Do dalszego rozwoju nadają się bez przepisywania od zera:

- loader UEFI oraz walidacja ELF/relokacji;
- protokół startowy po wersjonowaniu i walidacji flag;
- serial, framebuffer, IDT/stuby ISR, PIC, PIT, RTC i enumeracja PCI;
- allocator sterty oraz PMM jako rozwiązanie przejściowe;
- RAMFS po naprawie operacji i testów;
- parser shell i terminal framebufferowy;
- struktury stosu sieciowego do dalszego podłączenia pod sterownik;
- framework aplikacji jako narzędzie diagnostyczne ring 0;
- testy hostowe i mechanizm markerów szeregowych.

## Moduły wymagające przebudowy lub nowych implementacji

- PMM: obsługa wielu regionów i rezerwacji;
- VMM: własne tablice kernela, HHDM lub inny spójny dostęp do pamięci fizycznej, CR3 i przestrzenie procesów;
- scheduler: zapis/odtworzenie kontekstu, stosy, wywłaszczanie i cleanup;
- procesy, loader ELF userspace, syscalle i walidacja pamięci użytkownika;
- VFS, deskryptory, sterownik blokowy i trwały filesystem;
- warstwa wejścia z myszą i zdarzeniami;
- GUI: powierzchnie, compositor, menedżer okien i rozdzielenie aplikacji od kernela;
- stos urządzeń sieciowych;
- ACPI i zarządzanie energią.

## Wprowadzone naprawy pierwszej kolejności

- dodano GDT, TSS i IST;
- rozszerzono panic oraz jednoznaczne zatrzymanie po błędzie startu;
- uszczelniono walidację BootInfo i dodano flagę trybu awaryjnego;
- sprawdzane są wyniki inicjalizacji PMM, RAMFS, schedulera, aplikacji i sieci;
- poprawiono semantykę RAMFS i shell, w tym CWD, `cp`, `mv`, `rmdir`, `stat` i historię;
- rozbudowano testy hostowe i integracyjny test powłoki QEMU;
- dodano rygorystyczne ostrzeżenia, prawdziwe konfiguracje debug/release i dokładniejszy fingerprint incremental build;
- naprawiono strukturę FAT32 oraz zawartość ISO;
- rozpoczęto implementację testowalnego managera stron x86-64;
- przygotowano automatyzację testu ISO pod UEFI VirtualBox.

## Kolejność dalszych prac

1. Zakończyć i zintegrować VMM z aktywnym kernelem; przetestować map/unmap i ochronę NX/W^X.
2. Rozbudować PMM o wszystkie regiony i bezpieczne rezerwowanie ramek tablic stron.
3. Dodać podstawowe prymitywy atomowe, spinlock i ochronę współdzielonych kolejek.
4. Zaimplementować przełączanie kontekstu ring 0, a następnie procesy i przestrzenie ring 3.
5. Ustabilizować ABI syscall i loader ELF userspace; przenieść `init` i shell poza kernel.
6. Wprowadzić VFS i sterownik blokowy, następnie trwały filesystem oraz testy po restarcie.
7. Dodać mysz i jednolity system zdarzeń wejściowych.
8. Dopiero na tej bazie zbudować compositor i menedżer okien.
9. Dodać sterownik sieciowy, ACPI i obsługę większej liczby platform.
10. Wykonać długie testy, pomiary zasobów i testy realnego sprzętu przed nazwaniem wydania stabilnym.

## Wniosek

Po pierwszym cyklu napraw KuroganeOS jest znacznie lepszą bazą eksperymentalnego systemu niż w stanie zastanym: buduje się, uruchamia, raportuje błędy i oferuje działającą powłokę z ulotnymi plikami. Nadal nie spełnia końcowych kryteriów „stabilnej wersji użytkowej”. Najpoważniejszym blokiem jest brak granicy kernel/userspace oraz trwałego stosu pamięci i przechowywania. Dokumentacja projektu musi konsekwentnie używać określenia „kernel preview” do czasu usunięcia tych ograniczeń.
