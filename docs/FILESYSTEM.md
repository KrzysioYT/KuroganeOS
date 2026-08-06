# System plików

## Aktualny zakres

Jedynym systemem plików dostępnym po starcie jest hierarchiczny RAMFS z `kernel/fs/ramfs.*`. Drzewo, metadane i treść plików są zaalokowane z 2 MiB heapu kernela. Obraz FAT32 służy firmware oraz loaderowi do dostarczenia `BOOTX64.EFI` i `kernel.elf`; kernel go nie montuje.

RAMFS obsługuje:

- katalogi i zwykłe pliki;
- tworzenie, odczyt, pełny zapis, stat i listowanie;
- usuwanie pustego katalogu lub rekursywne usunięcie poddrzewa;
- niezależną kopię pojedynczego zwykłego pliku;
- przenoszenie i zmianę nazwy pliku lub katalogu;
- ścieżki z separatorami, `.` i `..`, z ochroną przed wyjściem ponad root;
- wykrywanie cyklu przy przenoszeniu katalogu.

Root `/` jest chroniony przed usunięciem i przeniesieniem. `cp` i `mv` wymagają nieistniejącego celu — nie nadpisują go automatycznie. `cp` nie kopiuje katalogów.

## Drzewo startowe

`kmain` przy każdym rozruchu tworzy od nowa:

```text
/
├── apps/
├── etc/
├── home/
│   └── readme.txt
├── system/
│   └── version
├── tmp/
└── var/
```

Nie są to punkty montowania ani partycje. Nazwy jedynie porządkują dane bieżącej sesji.

## Limity

| Właściwość | Limit |
| --- | ---: |
| Nazwa komponentu | 63 znaki |
| Pełna ścieżka | 255 znaków |
| Głębokość logiczna | 32 komponenty |
| Dzieci jednego katalogu | 64 |
| Wszystkie węzły | 256 |
| Jeden plik | 64 KiB |
| Suma logicznych rozmiarów plików | 1 MiB |

Niezależnie od limitu 1 MiB operacja może zwrócić `OutOfMemory`, ponieważ struktury katalogów i pojemności buforów współdzielą stały heap z kernelem.

## Bezpieczeństwo operacji w pamięci

Zapis sprawdza przepełnienia i limity przed publikacją nowego stanu. Gdy plik jest wyraźnie skrócony, implementacja próbuje zmniejszyć jego bufor; najpierw alokuje nowy, a dopiero po skopiowaniu podmienia stary. Brak pamięci pozostawia poprzednią treść i rozmiar.

Kopia staje się widoczna dopiero po utworzeniu kompletnej treści. Przenoszenie z góry sprawdza cel, pojemność katalogu, cykle oraz długość całego przenoszonego poddrzewa. Dzięki temu spodziewane błędy limitu lub alokacji nie pozostawiają połowy operacji.

RAMFS nie ma współbieżnego lockingu; obecny kernel wykonuje jego operacje sekwencyjnie w jednej pętli. Przyszły scheduler preemptive albo SMP będzie wymagał synchronizacji.

## Shell i CWD

Shell utrzymuje kanoniczny katalog roboczy i pokazuje go w promptcie, np. `kurogane:/home $`. Akceptuje ścieżki bezwzględne i względne, usuwa powtarzające się separatory, normalizuje `.`/`..` i nigdy nie wychodzi ponad `/`. Przeniesienie katalogu zawierającego CWD aktualizuje ścieżkę promptu; usunięcie jego części naprawia CWD do najbliższego istniejącego katalogu.

Dostępne polecenia plikowe:

```text
pwd
cd <path>
ls [path]
cat <path>
stat <path>
touch <path>
mkdir <path>
rmdir <path>
write <path> <text>
cp <src> <dst>
mv <src> <dst>
rm [-r] <path>
```

## Czego nie ma

Nie istnieją jeszcze VFS, inode o trwałym identyfikatorze, deskryptory plików per proces, uprawnienia, użytkownicy, dowiązania, punkty montowania, cache bloków, journaling ani sterownik nośnika. Nie ma poleceń `mount`/`unmount`, a restart zawsze usuwa wszystkie zmiany RAMFS.

Ograniczenia nośników startowych i kernela opisują [BUILDING.md](BUILDING.md) oraz [CURRENT_LIMITATIONS.md](CURRENT_LIMITATIONS.md).
