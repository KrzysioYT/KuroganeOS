# Architektura KuroganeOS 1.0

KuroganeOS jest monolitycznym, freestandingowym kernelem C++17 dla x86-64.
Bootloader jest niewielką aplikacją UEFI napisaną w C11. Wszystkie obecne
usługi i aplikacje działają w jednej przestrzeni adresowej i na tym samym
poziomie uprawnień.

## Przebieg startu

```text
Firmware UEFI/EDK2
        |
        v
iso/EFI/BOOT/BOOTX64.EFI
  - otwarcie \kernel.elf
  - walidacja relokowalnego ELF64 ET_DYN
  - przydział wolnego zakresu i załadowanie segmentów
  - zastosowanie bezpiecznych relokacji R_X86_64_RELATIVE
  - odczyt i walidacja bieżącego trybu GOP
  - pobranie mapy pamięci i RSDP
  - ExitBootServices
        |
        v
KuroganeBootInfo (protokół v1)
        |
        v
kmain
  - terminal i diagnostyka
  - pamięć, RAMFS, PCI i sieć
  - planista i aplikacje
  - IDT/PIC/PIT/klawiatura
        |
        v
główna pętla zdarzeń -> shell lub aktywna aplikacja
```

Bootloader i kernel mają jawnie określone ABI wywołania. UEFI używa ABI
Microsoft x64, natomiast wejście kernela używa System V AMD64. Wspólne
struktury protokołu znajdują się w `common/boot_protocol.h`.

Kernel nie wymaga stałego adresu fizycznego. Loader przydziela dostępne strony
przez UEFI, ładuje obraz względem wybranej bazy i akceptuje wyłącznie relokacje
względne wskazujące do zweryfikowanych, zapisywalnych segmentów.

`KuroganeBootInfo` zawiera:

- sygnaturę, wersję i rozmiar struktury;
- adres, rozmiar, pitch i format pikseli framebuffera;
- znormalizowaną mapę regionów pamięci;
- adres RSDP ACPI;
- fizyczny zakres zajęty przez kernel.

Kernel odrzuca niespójne lub przepełniające zakresy dane wejściowe.

## Warstwy systemu

| Obszar | Katalog | Odpowiedzialność |
| --- | --- | --- |
| Bootloader | `boot/efi` | UEFI, plik ELF64, GOP, mapa pamięci, `ExitBootServices` |
| Protokół startowy | `common` | ABI między bootloaderem a kernelem |
| Wejście x86-64 | `kernel/arch/x86_64` | stos kernela, IDT, stuby przerwań i operacje CPU/I/O |
| Sterowniki | `kernel/drivers` | framebuffer, serial, PIC, PIT, PS/2, PCI i RTC |
| Pamięć | `kernel/memory` | sterta kernela i bitmapowy alokator ramek |
| Terminal i shell | `kernel/terminal.*`, `kernel/shell` | tekst, edycja linii i polecenia |
| System plików | `kernel/fs` | ulotny, hierarchiczny RAMFS |
| Zadania | `kernel/task` | kooperacyjny planista callbacków |
| Sieć | `kernel/net` | Ethernet II, ARP, IPv4, ICMP i loopback |
| UI i aplikacje | `kernel/ui`, `kernel/apps` | prymitywy GUI, framework i aplikacje wbudowane |

## Model wykonania

Timer PIT działa z częstotliwością 100 Hz. Handler przerwania aktualizuje
licznik, a główna pętla kernela:

1. publikuje nowy tick do planisty i aktywnej aplikacji;
2. przetwarza ograniczoną liczbę zadań oraz ramek loopback;
3. odbiera znaki z kolejki sterownika klawiatury;
4. przekazuje znaki do shella albo aktywnej aplikacji;
5. usypia CPU instrukcją `hlt`, gdy nie ma pracy.

Callback zadania musi wrócić do planisty. `yield()` zleca kolejne wykonanie,
ale nie przełącza stosu w środku callbacka. To istotna różnica względem
preempcji i wielozadaniowości procesowej.

## Pamięć

Kernel ma statyczną stertę o rozmiarze 2 MiB. Alokator sterty obsługuje
wyrównanie, dzielenie oraz scalanie bloków. Alokator pamięci fizycznej używa
bitmapy i podczas startu otrzymuje największy obsługiwany ciągły region typu
`usable` z mapy UEFI.

Wydanie 1.0 nie tworzy procesów użytkownika, osobnych przestrzeni adresowych
ani polityki wymiany stron. Dane i kod aplikacji wbudowanych pozostają częścią
kernela.

## Wejście/wyjście i diagnostyka

Terminal renderuje własną czcionkę do 32-bitowego framebuffera RGBX/BGRX.
Każdy komunikat terminala jest także wysyłany przez port szeregowy, dzięki
czemu bezokienkowy test QEMU może sprawdzić przebieg startu.

Wyjątki CPU trafiają do wspólnego handlera diagnostycznego, który wypisuje
wektor, kod błędu, RIP, a dla page fault także CR2, po czym bezpiecznie
zatrzymuje kernel.

## Dane i aplikacje

RAMFS jest tworzony przy każdym starcie. Kernel zakłada katalogi `/system`,
`/home` i `/apps` oraz pliki informacyjne. Shell i przeglądarka plików
korzystają z tego samego API ścieżek.

Framework przechowuje definicje aplikacji i przekazuje aktywnej aplikacji
zdarzenia klawiatury i timera. Nie ładuje plików wykonywalnych: wszystkie
cztery aplikacje są skompilowane bezpośrednio z kernelem.

## Sieć

Warstwa sieciowa potrafi analizować i generować Ethernet II, ARP, IPv4 oraz
ICMP echo. W 1.0 jest podłączona wyłącznie do ograniczonej kolejki ramek w
pamięci. Ping `127.0.0.1` przechodzi przez te same warstwy protokołu, ale nie
opuszcza kernela ani maszyny wirtualnej.

## Granice zaufania

Bootloader dokładnie waliduje nagłówki i zakresy segmentów ELF, a kernel
sprawdza strukturę startową przed użyciem. Po starcie nie istnieje jednak
granica bezpieczeństwa między subsystemami. Błąd aplikacji, sterownika albo
systemu plików może uszkodzić cały kernel. Z tego powodu KuroganeOS 1.0 należy
traktować jako środowisko badawcze i demonstracyjne.
