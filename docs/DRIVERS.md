# Sterowniki i obsługa platformy

## Zaimplementowane elementy

| Moduł | Stan i rzeczywisty zakres |
| --- | --- |
| Framebuffer | korzysta z liniowego bufora GOP przekazanego przez loader; akceptuje 32 bpp RGBX8/BGRX8; rysuje piksele, prostokąty, tekst i przewijanie programowo |
| Serial | inicjalizuje COM1/16550 i wysyła logi; brak interaktywnego wejścia szeregowego |
| GDT/TSS/IST | własna GDT, `RSP0` i awaryjne stosy dla double fault, NMI i machine check |
| IDT | 256 wpisów, obsługa wyjątków i rejestracja IRQ przez centralny dispatcher |
| 8259 PIC | remap, maskowanie, EOI oraz spurious IRQ7/IRQ15 |
| PIT | kanał 0 w trybie square wave, IRQ0 i monotoniczne tyknięcia około 100 Hz |
| Klawiatura PS/2 | konfiguracja kontrolera w ograniczonym czasie, IRQ1, dekoder scancode, modyfikatory i bufor 128 zdarzeń; polling fallback |
| RTC/CMOS | stabilizowany odczyt daty i czasu oraz konwersja BCD/12 h |
| PCI | odczyt i zapis konfiguracji, enumeracja bus/slot/function, BAR, wyszukiwanie klasy/ID oraz włączenie bus mastering |

Enumeracja PCI nie jest sterownikiem urządzenia. Znalezienie kontrolera SATA, NIC albo GPU nie powoduje obsługi tego sprzętu.

## Framebuffer i terminal

Loader nie przełącza arbitralnie trybu graficznego; przekazuje aktualny wspierany tryb GOP. Kernel waliduje bazę, wymiary, pitch, format i zakres adresów. Sterownik wykonuje bezpośrednie zapisy CPU do framebufferu. Nie ma akceleracji 2D/3D, bufora tylnego, synchronizacji pionowej ani zarządzania trybami po `ExitBootServices`.

Terminal i warstwa UI współdzielą tę samą powierzchnię. Szczegóły interfejsu: [GUI.md](GUI.md).

## Klawiatura

Sterownik próbuje skonfigurować kontroler PS/2 i instaluje IRQ1. Jeżeli ścieżka sprzętowa jest zdegradowana, decoder może odpytywać dostępne bajty, lecz aktualny rozruch traktuje niedostępność wymaganej klawiatury lub PIT jako błąd krytyczny. Udostępniane są zdarzenia press/release, kod klawisza, znak oraz stany Shift, Caps Lock, Ctrl i Alt.

Nie ma sterownika myszy PS/2 ani USB HID. Ustawienie urządzenia myszy w konfiguracji emulatora nie zmienia tego stanu.

## PCI i safe mode

W trybie normalnym kernel skanuje PCI i prezentuje wynik przez `pci` oraz widok monitora. W safe mode skan jest całkowicie pomijany, więc liczba urządzeń pozostaje zerowa. Brak skanu nie wyłącza elektrycznie urządzeń; po prostu kernel nie czyta ich konfiguracji.

## Sieć

Kod w `kernel/net/` implementuje serializację, walidację i obsługę Ethernet, ARP, IPv4 oraz ICMP echo, z tabelą sąsiadów i statystykami. To warstwa protokołów, nie sterownik NIC. Usługa uruchomiona przez `kmain` używa kolejki loopback, a `net ping` potwierdza drogę do `127.0.0.1` wewnątrz pamięci kernela.

Nie ma E1000, VirtIO-net, DMA, przerwań karty, DHCP ani ruchu do hosta. QEMU jest celowo uruchamiane z `-nic none`.

## Restart i wyłączanie

Shell próbuje resetu przez historyczne porty kontrolera PS/2, PCI reset control oraz System Control Port A. Wyłączanie zapisuje wartości rozpoznawane przez część emulatorów do portów `0x604` i `0xB004`. To nie zastępuje ACPI i może nie działać w VirtualBox albo na fizycznym komputerze.

## Brakujące sterowniki

Nie zaimplementowano:

- kontrolera dysku AHCI, NVMe, IDE ani VirtIO-blk;
- sterownika FAT32 po stronie kernela i ogólnej warstwy blokowej;
- fizycznej lub wirtualnej karty sieciowej;
- myszy PS/2, USB HID i kontrolerów USB;
- APIC/IOAPIC, HPET i uruchamiania wielu CPU;
- ACPI do zasilania, enumeracji i zarządzania energią;
- sterownika GPU, zmiany trybu, akceleracji i kompozytora;
- audio oraz sterowników urządzeń ładowanych w runtime.

Stan platformy należy oceniać razem z [CURRENT_LIMITATIONS.md](CURRENT_LIMITATIONS.md) i testami emulatorów.
