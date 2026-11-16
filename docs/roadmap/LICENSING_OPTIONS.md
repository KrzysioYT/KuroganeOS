# KuroganeOS — opcje licencji i adopcji

Status: propozycja do decyzji właściciela. Obowiązuje nadal
[KSAL-2.0](../../LICENSE) i obecny [CLA](../../CLA.md). Ten dokument
nie udziela licencji ani nie zmienia warunków wkładu.

KSAL-2.0 daje dostęp do źródła, lecz ograniczenia komercyjnego użycia
i redystrybucji nie spełniają [definicji Open Source OSI](https://opensource.org/osd).
Dla dystrybucji i pakujących to oznacza konieczność sprawdzania zgód nawet
wtedy, gdy sam build jest możliwy. Decyzja dotyczy warunków dystrybucji
KuroganeOS; nie zmienia kernela, ABI ani planu Flux.

| Model | Co daje projektowi | Główny koszt lub kompromis |
| --- | --- | --- |
| Zachować KSAL-2.0 | Właściciel utrzymuje kontrolę nad dystrybucją i użyciem komercyjnym. | Pakujący, dystrybucje i firmy muszą ustalić warunki. Czytelne FAQ i proces udzielania zgód pomagają, ale nie nadają statusu Open Source. |
| [MIT](https://opensource.org/license/mit) | Krótka licencja permisywna; pozwala na użycie, modyfikacje i dystrybucję z zachowaniem informacji o licencji. | Nie wymaga publikacji zmian i nie zawiera osobnej, wyraźnej klauzuli patentowej. Historyczne rewizje MIT nie dowodzą możliwości zmiany licencji obecnego drzewa. |
| [Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0) | Permisywna dystrybucja z wyraźnym grantem patentowym i zasadami informacji o zmianach i atrybucji; dobry punkt wyjścia dla kodu, SDK i pakujących. | Nie wymaga udostępniania wszystkich modyfikacji. Trzeba utrzymywać wymagane noty i zweryfikować zgodność przy łączeniu z kodem objętym silnym copyleft. |
| [MPL-2.0](https://www.mozilla.org/en-US/MPL/2.0/FAQ/) | Copyleft na poziomie pliku: przy dystrybucji źródło zmienionych plików MPL pozostaje dostępne; inne pliki mogą mieć inne warunki. | Trzeba pilnować granic plików, not i dostępu do źródła. Sam podział katalogów nie rozstrzyga wszystkich warunków połączenia. |
| [GPLv3](https://opensource.org/license/gpl-3.0) | Silniejsze zobowiązanie do zachowania wolności odbiorców dystrybuowanej wersji, w tym dostępu do odpowiadającego źródła. | Większy koszt weryfikacji połączeń kodu i dystrybucji binarnej. Przed wyborem trzeba sprawdzić także narzędzia, firmware i obowiązki dotyczące instalacji zmodyfikowanych wersji, gdy mają zastosowanie. |

**Rekomendacja do rozpatrzenia:** Apache-2.0 dla kodu, do którego projekt
ma potwierdzone prawa, jeśli celem jest łatwe pakowanie i niski próg wejścia
dla contributorów. Jeśli warunkiem jest dostępność publikowanych zmian w
plikach projektu, porównać Apache-2.0 bezpośrednio z MPL-2.0. W obu wariantach
można zachować oddzielną [politykę znaków towarowych](../../TRADEMARKS.md).
Nie stosować jednej etykiety licencyjnej do całego ISO.

## Zależności i dystrybucja

- **Mbed TLS:** przypięty submoduł z alternatywą Apache-2.0 lub
  GPL-2.0-or-later według jego LICENSE. Build linkuje wybrane moduły do
  kernela, więc dla przyszłego wydania trzeba udokumentować wybór warunków,
  zakres kodu i zawartość not w artefaktach; sam wpis Git nie wystarcza.
- **DejaVu:** pliki źródłowe oraz atlas mają osobne pochodzenie i notę
  [Bitstream Vera](../../third_party/fonts/dejavu/COPYRIGHT). Obraz zawiera
  notę w katalogu licencji. Zmiana licencji kernela nie zmienia licencji fontu.
- **Chromium:** [checkout portu](../../third_party/chromium/README.md) jest
  osobny, a obecny Web nie jest gotowym Chromium. Ewentualny port wymaga
  własnego wykazu komponentów i not; nie zakładać jednej licencji dla całego
  upstreamu.
- **Pozostałe materiały:** dla każdego publikowanego ISO/IMG, SDK i paczki
  narzędzi sprawdzić certyfikaty, firmware, toolchain, emulator, biblioteki,
  grafiki i wygenerowane dane. Rozróżnić zależność linkowaną, plik dystrybuowany
  razem z systemem i narzędzie używane tylko na hoście. Zapisać źródło, wersję,
  licencję, zmiany, wymagane noty i sposób udostępnienia źródła, jeśli wymagany.

## Wkład i droga do decyzji

1. Ustalić granice: kod kernela, publiczne nagłówki, SDK, aplikacje i znaki.
   W historii Git zidentyfikować autorów, podstawę praw do każdej partii
   i zaakceptowane wersje CLA. Zachować prawa już udzielone dla rewizji MIT.
2. CLA 2.0 przewiduje możliwość relicencjonowania wkładu w zakresie praw,
   które contributor rzeczywiście posiada i przyznał. Nie daje automatycznie
   praw do cudzych materiałów ani wkładów bez udokumentowanej akceptacji.
   Brak potwierdzenia wymaga wyjaśnienia przed zmianą warunków.
3. Porównać pozostawienie CLA z uproszczonym wkładem na warunkach projektu
   i [DCO](https://developercertificate.org/). DCO potwierdza podstawę
   zgłoszenia; nie jest zamiennikiem osobnego prawa do relicencjonowania.
   Wybór wpływa na prostotę zgłaszania PR i późniejszą możliwość zmiany licencji.
4. Przygotować wykaz zależności i próbny raport zawartości artefaktów, uzyskać
   ocenę prawną spornych pozycji, a następnie przedstawić właścicielowi
   konkretną decyzję dla wskazanych rewizji i plików.

Do czasu odrębnej decyzji nie zmieniamy LICENSE, CLA ani not praw autorskich.
Prace nad Flux 8 mogą postępować równolegle.
