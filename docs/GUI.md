# Interfejs graficzny

## Rzeczywisty model

Kernel otrzymuje od loadera liniowy framebuffer GOP i rysuje do niego bezpośrednio. Terminal tekstowy, shell i warstwa `ui` współdzielą jedną pełnoekranową powierzchnię. Jest to framebufferowy interfejs kernela, nie system okienkowy.

`kernel/ui/ui.*` dostarcza prostokąty, panele, etykiety, wizualne przyciski, pasek postępu, separatory i pasek stanu. „Przycisk” jest wyłącznie narysowanym komponentem — nie można go kliknąć, ponieważ nie ma myszy ani hit-testingu zdarzeń wskaźnika.

## Wbudowane widoki

W trybie normalnym rejestrowane są cztery wpisy:

| Nazwa | Zawartość | Sterowanie |
| --- | --- | --- |
| `desktop` | launcher z zegarem RTC i skrótami do pozostałych widoków | `M` monitor, `F` files, `A` about, `Q` powrót |
| `monitor` | uptime, heap, liczba alokacji, wolne ramki PMM i liczba urządzeń PCI | automatyczne odświeżenie, `Q` powrót |
| `files` | tylko odczyt listy katalogu głównego RAMFS | `R` odświeżenie, `Q` powrót |
| `about` | wersja i skrócony opis możliwości kernela | `Q` powrót |

Uruchomienie:

```text
apps
gui
run monitor
run files
run about
```

Warstwa aplikacji mieści maksymalnie 16 definicji, ale wbudowane są cztery. W danej chwili aktywny jest co najwyżej jeden widok. Zamknięcie czyści ekran, drukuje `KuroganeOS application closed.` i przywraca prompt shella.

Nie istnieją osobne aplikacje **Terminal** ani **Settings**. „Terminal” to zwykły ekran shella framebufferowego. Ustawień systemu nie da się zmieniać przez GUI.

## Tryb safe

Safe mode inicjalizuje framework aplikacji, lecz celowo nie rejestruje widoków wbudowanych. Shell nadal działa, a `apps` nie wypisuje wpisów. Jest to zamierzone ograniczenie powierzchni diagnostycznej.

## Czego nie ma

Nie zaimplementowano:

- sterownika myszy, kursora i zdarzeń wskaźnika;
- kompozytora, serwera wyświetlania ani menedżera okien;
- wielu powierzchni, z-order, focusu, przeciągania, zmiany rozmiaru, minimalizacji i maksymalizacji;
- double buffering, damage tracking, animacji i synchronizacji pionowej;
- fontów skalowalnych, Unicode, schowka, drag-and-drop i dostępności;
- osobnego procesu dla GUI oraz izolacji awarii aplikacji;
- eksploratora z nawigacją i operacjami — `files` tylko listuje `/`;
- terminal emulatora uruchamiającego proces i edytora tekstowego;
- zapisu konfiguracji motywu albo układu między restartami.

Kod interfejsu ma własny spójny motyw, ale obecnego zestawu pełnoekranowych widoków nie należy opisywać jako kompletnego desktopu. Zależności od framebufferu opisuje [DRIVERS.md](DRIVERS.md).
