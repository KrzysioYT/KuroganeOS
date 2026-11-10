# KuroganeOS 8.0.0 — Flux Modern

Status **BLOCKED**. To szkic informacji o wydaniu. Zakwalifikowany w poniższym
zakresie obraz zgłasza wersję `7.0.0-dev`.

## Co obejmuje kandydat

- Natywny pulpit Modern z Home, Dockiem, wyszukiwaniem i zachowanym trybem
  Classic. Files i Terminal są osobnymi aplikacjami Ring 3.
- Ustawienia palety, tapety, stylu i skali 100/125/150/200%. Na VDI ustawienia
  są odtwarzane po restarcie. Try stosuje je w bieżącej sesji.
- Wyszukiwanie i nawigacja w Files, tworzenie katalogów i zmiana nazw na
  zapisywalnym woluminie oraz Terminal z historią i przewijaniem.
- Instalator UEFI, logowanie hasłem na zainstalowanym systemie, odrzucenie
  uszkodzonego profilu oraz powrót do Try, gdy brak zapisywalnego celu.
- Obsługa błędów w Settings, w tym ponowne połączenie po niejednoznacznej
  odpowiedzi IPC i próba przywrócenia poprzedniego stanu.

## Dowody kwalifikacji

Źródła `d0bb7511ebd4ab1a67928d02e7d9b364defffab9`. SHA-256 obrazu
`5f24b9c49811f4137e7a92cd26e97e5bf0bc8cbe4ddbde529fe6de22e2ad6d5d`.
[Obraz kandydata](https://github.com/KrzysioYT/KuroganeOS/actions/runs/42546543990/artifacts/13643782633)

Widoczna kwalifikacja VirtualBox z PS/2 daje 9/9 w sprawdzonym zakresie.
QEMU z klawiaturą i tabletem USB ma osobny przebieg. W macierzy rozróżniono
logi, zrzuty i obserwacje z pierwotnej sesji.
[Macierz VirtualBox](../FLUX8_VIRTUALBOX_MATRIX.md) ·
[Flux USB](https://github.com/KrzysioYT/KuroganeOS/actions/runs/42546543990) ·
[UEFI, ABI i system plików](https://github.com/KrzysioYT/KuroganeOS/actions/runs/42546548720)

Powtórzona kwalifikacja lokalna dla tego SHA obejmowała Debug i Release,
regresję hosta oraz core UBSan. QEMU uruchomiło pobrany obraz w trybie
USB-only, zainstalowało go i uruchomiło dwa razy z dysku. Przeszły też próby
odzyskiwania po wyborze celu tylko do odczytu, bezpiecznego anulowania
instalacji i odrzucenia brakującego profilu. Po restarcie sprawdzono kolor,
tapetę, skalę i nową nazwę katalogu.

Projekt `fluxdemo` wygenerowany z publicznego szablonu GUI i zbudowany
przy użyciu sysroota z tego SHA przeszedł osobną próbę na kopii dysku QEMU.
Terminal uruchomił go poleceniem `open /apps/fluxdemo`. Okno wyświetliło
się, przyjęło klawisz i kliknięcie oraz zakończyło pracę po zamknięciu.
Kopia różniła się od dysku kwalifikacyjnego dodanym plikiem aplikacji.

## Otwarte bramki

| Priorytet | Pozostały zakres |
| --- | --- |
| P0, kwalifikacja | Końcowe ISO z runtime `8.0.0` wymaga osobnej kwalifikacji. Wyniki powyżej odnoszą się do SHA i sumy kandydata `7.0.0-dev`. |
| P1, kontrakt | Skala obejmuje tekst i geometrię natywnych kontrolek. Trzy rozdzielczości GOP sprawdzono w osobnych startach. Pełne skalowanie pulpitu i zmiana rozdzielczości podczas sesji pozostają otwarte. |
| P1, kontrakt | Piny Docka i ustawienia wyglądu są trwałe. Zmiana kolejności Docka oraz odtwarzanie pozycji i rozmiarów okien po restarcie wymagają implementacji i próby. |
| P1, dowód | Testy hosta obejmują awarie IPC, zapisu i przywracanie stanu. Przebieg na końcowym ISO musi potwierdzić wymagany zakres odzyskiwania sesji i Settings po awarii usługi. |
| P2, ograniczenia | Ustawienia wyglądu są globalne. Renderowanie używa ograniczonego atlasu znaków i kerningu par. Kwalifikacja fizycznego sprzętu i szeroka zgodność aplikacji pozostają w późniejszych zakresach roadmapy. |

[Kontrakt Flux 8](../roadmap/FLUX8_MODERN_COMPLETION.md) ·
[Roadmapa](../roadmap/MASTER_ROADMAP_15.md)

## Sekwencja wydania

1. Domknąć punkty blokujące kontrakt. Zaktualizować tożsamość w
   `common/version.h` i odpowiadającą jej kontrolę w workflow Flux.
2. Zbudować ISO i zapisać SHA źródeł oraz SHA-256 obrazu. Na tym obrazie
   powtórzyć m.in. UEFI, Try, logowanie, instalację, start z VDI, ustawienia
   oraz aplikacje i geometrię zależną od wyświetlanej wersji.
3. Uzyskać niezależną akceptację końcowego HEAD. Ochrona `main` wymaga
   aktualnej gałęzi i kontroli `iso-qualification`, `reliable-file` oraz
   `Verify contributor CLA acceptance`. [PR #58](https://github.com/KrzysioYT/KuroganeOS/pull/58)
4. Po scaleniu zakwalifikować obraz z dokładnego SHA na `main`. Dopiero dla
   potwierdzonego obrazu utworzyć `v8.0.0` i dołączyć ISO, sumę, changelog
   oraz informacje o wydaniu.
