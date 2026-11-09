# Flux 8 — kandydat do ręcznych testów

Obraz służy do kwalifikacji Flux Modern 8.0.0. Runtime zgłasza wersję
`7.0.0-dev`. Wyniki ręczne zapisuj osobno dla każdego obrazu.

1. Otwórz [PR #58](https://github.com/KrzysioYT/KuroganeOS/pull/58) i odczytaj
   pełny SHA gałęzi `integration/flux8-reconciliation`.
2. W [Actions](https://github.com/KrzysioYT/KuroganeOS/actions/workflows/qualify-8.0-flux-desktop-foundation.yml?query=branch%3Aintegration%2Fflux8-reconciliation)
   wybierz zakończony powodzeniem przebieg dla tego samego SHA. Zaloguj się,
   pobierz artefakt `flux-8-interactive-iso-<pełny SHA>` i rozpakuj ZIP.
3. Użyj [macierzy VirtualBox](FLUX8_VIRTUALBOX_MATRIX.md), aby porównać sumę
   obrazu z manifestem i przygotować widoczną VM z pustym VDI.

Jeżeli dla wskazanego SHA przebieg wciąż trwa lub nie ma artefaktu, kandydat
dla tego commita czeka na publikację. Wyniki QEMU i headless VirtualBox są
zapisywane oddzielnie od wyników widocznej sesji.
