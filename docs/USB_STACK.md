# USB Stack

## Planowana architektura

```text
xHCI → USB Core → Device → Configuration → Interface → Class Driver
                                                   ├→ HID
                                                   └→ Mass Storage
```

USB Core odpowiada za deskryptory, adresację, konfiguracje, endpointy, transfery, timeouty i disconnect. xHCI pozostaje warstwą kontrolera. HID publikuje zdarzenia przez wspólny `InputDeviceOps`, a Mass Storage przez `BlockDeviceOps`.

## Stan

- Hotplug po początkowej enumeracji używa stanów DrainInput → ReleaseKeys →
  WaitingForDevice → Active. Zmiana połączenia PORTSC wykrywa także krótki cykl
  odłączenia/podłączenia między pollami. Stare bufory endpointów są ponownie
  używane dopiero po potwierdzonym Disable Slot; uchwyt starej klawiatury wygasa.
  Timeout kończy polling i zachowuje zasoby do bezpiecznego cleanupu.
  `runtime_status()` rozróżnia oczekiwanie NoDevice od błędu kontrolera.
- Nowy gate hotplug wykonuje trzy rzeczywiste cykle QMP remove/add z trzymanym
  Shiftem i testem F12 po ponownej enumeracji. Przeszedł na SHA
  `4b33bfbd05bc8b3f01c1cf89bc44ceabb33da79c`, run
  [35231888211](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35231888211),
  job `105237832241`, razem z host-suite, clean media, 130 parami F12,
  zawinięciem ringów i cleanupem pustego kontrolera. Wszystkie 20 workflowów
  uruchomionych dla tego źródła zakończyło się sukcesem, w tym Pre-Steel
  [35231888631](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35231888631).
  Nie obejmuje hubów ani wielu równoczesnych urządzeń.
- Nowy zakres inżynierski: pusty kontroler pozostaje uruchomiony po bootowaniu.
  `initialize()` zwraca Ok, `initialized()` jest true, `keyboard_ready()` false,
  a `runtime_status()` NoDevice. Poll opróżnia event ring również bez klawiatury.
  Pierwsze podłączenie używa tej samej enumeracji co ponowne podłączenie.
  Rozszerzony gate dodaje klawiaturę dopiero po Login, po czym wykonuje trzy
  pełne cykle disconnect/reconnect. Zakres przeszedł na SHA
  `ad6a19b11d83d0eecaba3599c084321768371e67`, run
  [35905197560](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35905197560),
  job runtime `107331023235`, job cleanup `107331023071`.
  Osobny wariant `--cleanup-empty` wywołuje rzeczywisty cleanup kontrolera
  zamiast oczekiwania; ta operacja jest wstrzykiwana wyłącznie w build testowy.
- Pełna kolejka input wstrzymuje następny transfer HID. Stały bufor 20 zdarzeń
  zachowuje nieopublikowaną część raportu; następny poll ponawia ją bez duplikatów.
  Stan obejmuje również zwolnienia klawiszy i modyfikatorów.
- Parser konfiguracji USB i wybór HID boot keyboard są zaimplementowane bez alokacji dynamicznej.
- Walidacja odrzuca niepełne deskryptory, alternatywne interfejsy, EP0, endpointy bez interwału oraz nieprawidłowe rozmiary pakietów.
- Dekoder raportów HID boot keyboard jest ograniczony do sześciu klawiszy i stałego bufora zdarzeń.
- Dekoder zatwierdza pełną zmianę atomowo: za mały bufor nie zmienia Caps Lock,
  poprzedniego raportu ani tablicy wyjściowej. Ponowienie nie gubi zdarzeń.
  Powtórzone usage nie tworzą podwójnych naciśnięć/zwolnień. Rollover zachowuje
  ostatni zestaw zwykłych klawiszy, nadal aktualizując modyfikatory zgodnie z
  [HID 1.11, Appendix C](https://www.usb.org/sites/default/files/hid1_11.pdf).
  Kody błędów 2/3 są odrzucane bez częściowej zmiany stanu.
- Host-suite uruchamia `tests/test_usb_protocol.cpp` jako regresję protokołu.
- Host-suite sprawdza także granice MMIO oraz rzeczywisty skaner portów xHCI
  (0, 254 i 255 portów, brak urządzenia i ostatni port). Porty są adresowane
  względem rejestrów operational: limit uwzględnia CAPLENGTH. Licznik skanera
  nie zawija się przy MaxPorts=255.
- xHCI ma ścieżkę resetu, ringów, enumeracji i pojedynczego HID Boot keyboard/mouse; walidator capability/runtime/doorbell/port MMIO odrzuca overflow i obcięte okna po mapowaniu, przed dostępem do tych rejestrów. Klawiatura, disconnect/reconnect i pierwszy bounded mouse runtime przeszły kwalifikację QEMU opisaną poniżej; równoczesne urządzenia HID, huby, realny sprzęt i Mass Storage pozostają otwarte.
- Cleanup stron opublikowanych kontrolerowi wymaga potwierdzenia USBSTS.HCHalted
  i wyłączenia PCI bus mastering z odczytem kontrolnym. Samo skasowanie RUN
  nie pozwala oddać stron DMA. Odczyt rejestru równy wszystkim jedynkom nie
  stanowi potwierdzenia zatrzymania. `ControllerHaltTimeout` zachowuje strony
  i mapowania; `ResourceReleaseFailed` zachowuje niezwrócone zasoby. Ponowna
  inicjalizacja najpierw ponawia zaległy cleanup, zamiast nadpisywać jego stan.
- Częściowe mapowanie MMIO i nieudana rejestracja klawiatury zachowują właściciela
  do rollbacku. Unmap od końca umożliwia ponowienie bez podwójnego zwalniania.
  Host regression sprawdza timeout, odmowę wyłączenia bus mastering, błąd DMA
  release, częściowy unmap, rollback urządzenia i odrzucenie starego uchwytu.
- Każdy raport HID posiada zapisany adres oczekującego Normal TRB. Zakończenie
  transferu musi wskazywać dokładnie ten deskryptor; nie wystarcza zgodność
  slotu i endpointu. Obcy, niewyrównany, zduplikowany oraz Event Data completion
  nie zmienia dekodera, nie czyści bufora DMA i nie publikuje klawiszy.
  Host regression wykonuje produkcyjny `poll()` i reprodukuje błędne przedwczesne
  przetworzenie raportu sprzed poprawki. Pełny QEMU gate przeszedł na SHA
  `d96c9c8b631c1c60799aed1a9b80f85e0fb81865`, run
  [35086010272](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35086010272),
  job `104760879337`: full host-suite, clean release IMG/ISO i install.pkg,
  130 par F12, zawinięcie ringów i cleanup pustego kontrolera.

## Kolejność dalszych prac

Workflow `Qualify 5.0 xHCI USB Keyboard` buduje pełny obraz po dodaniu
wyłącznie diagnostyki do produkcyjnego `handle_keyboard_report()`.
QEMU udostępnia `qemu-xhci` i `usb-kbd`; dopiero po starcie Login QMP
wysyła F12. PASS wymaga rzeczywistej enumeracji, odebrania raportów
USB oraz poprawnie uporządkowanych par press/release opublikowanych
do kolejki input. Instrumentacja nie tworzy raportów ani backendu USB i nie
trafia do zwykłego builda. Pierwsza kwalifikacja tego zakresu przeszła na SHA
`73aba709c535261b99d23182ecb4b2ead091b6e1` w Actions
[35059962106](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35059962106),
job `104677993041`: pełny host-suite, clean release media, enumeracja
i dwie rzeczywiste pary F12 press/release. Nie jest to kwalifikacja hotplug,
hubów, wielu urządzeń ani USB Mass Storage.

Dekoder z atomowym zatwierdzaniem raportów przeszedł ten sam gate na SHA
`17ca70f4afc9ef3deca770da91a5189f86565c52`, run
[35060322848](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35060322848).

Gate zawiera również boot z `--usb-controller`, bez klawiatury.
Marker `xhci_empty_cleanup` wymaga rzeczywistego startu kontrolera, braku
urządzenia i pomyślnego zatrzymania/zwolnienia zasobów. Przeszedł na SHA
`648b9d38cabf8fcabb66ab3e5071f676f18ad98a`, run
[35085130775](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35085130775),
job `104758052878`, razem z pełnym host-suite, clean media i klawiaturą.
Hostowe wstrzykiwanie błędów nie jest dowodem zachowania wadliwego sprzętu.

Nowe rozszerzenie gate wymaga 130 par F12, czyli ponad 260 rzeczywistych
raportów, oraz obserwacji zmiany cycle bit obu produkcyjnych ringów:
transferowego (255 wpisów + Link TRB) i eventowego (256 wpisów).
Każde kolejne naciśnięcie czeka na poprzednie zwolnienie w logu gościa.
Budżet 240 s obejmuje tę nową liczbę interakcji; nie zastępuje żadnego
sprawdzenia. Host regression przechodzi przez osiem zawinięć i odrzuca
nieaktualne eventy. Rozszerzenie runtime przeszło na SHA
`cdb2af84bfeab5bf139f6b06fa810ca18d7e4c97`, run
[35085875406](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35085875406),
job `104760439289`: 130 press, 130 release, `usb_hid_ring_wrap`, a następnie
osobny boot i `xhci_empty_cleanup`. Poprzednia próba `35085576426` zatrzymała
się przed startem QEMU z powodu niedozwolonego parametru timeout 300 s;
poprawka używa istniejącego limitu 240 s, bez zmniejszenia liczby raportów.

Wspólna kolejka input przyjmuje już raport myszy w całości albo odmawia bez
zmiany pozycji i przycisków. Pump PS/2 zachowuje odrzucone zdarzenie do ponowienia.
Zmiana `4926de5` przeszła host-suite, ASan/UBSan, lokalne dwa cykle sesji z myszą
i klawiaturą oraz USB matrix
[35906417389](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35906417389).
To przygotowanie wspólnego input, nie sterownik myszy USB.

Warstwa protokołu ma bounded parser HID Boot Mouse oraz transakcyjny
dekoder 3-bajtowego raportu do wspólnego `drivers::mouse::Sample`. Fundament
protokołu na `0b369e09b424bc707122babfdf8874e904c30bf3` przeszedł pełną
macierz klawiatury w run `36063735518`. Boot protocol publikuje X/Y/buttons;
wheel pozostaje zerem do czasu jawnej obsługi report protocol.

Pierwszy bounded runtime myszy xHCI jest teraz **QUALIFIED** w zakresie jednego
aktywnego urządzenia HID. PR #20 został scalony jako
`c8db9a080cdc1e6094ec47ed76d68c0e18f5575d`. Candidate run `36121992774`,
job `108029393842`, przeszedł host-suite, clean release media, regresję
klawiatury i rzeczywisty boot QEMU z `usb-mouse`. Harness wybiera dokładnie
QEMU HID/USB Mouse przed ruchem/kliknięciem; guest musi wyemitować
`xhci_mouse_enumeration` oraz `usb_hid_mouse_input`, więc PS/2 nie może
fałszywie zaliczyć testu. Post-merge run `36122278967`, job
`108030300639`, przeszedł ponownie.

Generalizacja HID ujawniła nieaktualne literalne anchor-y wyłącznie w starym
qualifierze klawiatury. PR #21 naprawił harness i dodał pełną macierz USB
keyboard także dla pull requestów. Run `36122522577` przeszedł job
`108031097621` (empty cleanup) i `108031097910` (runtime): 130 par F12,
wrap ringów transfer/event, cleanup pustego kontrolera, trzy cykle
disconnect/re-enumeration oraz late attach. Hotfix został scalony jako
`2516dcb010f61d3ceed25647431d5530db112208`.

Aktualny runtime nadal świadomie obsługuje jeden slot / jedno aktywne urządzenie
boot HID naraz (keyboard **albo** mouse). Nie jest to kwalifikacja hubów,
równoczesnej klawiatury i myszy, report-protocol wheel ani fizycznego sprzętu.

1. USB Mass Storage: bounded parser interfejsu Bulk-Only Transport, CBW/CSW i
   podstawowe SCSI CDB, bez aktywowania runtime.
2. xHCI bulk transport + adapter do istniejącego `BlockDeviceOps`/block layer,
   dopiero potem realny QEMU `usb-storage`.
3. Failure injection i stress testy hotplug.


## USB Mass Storage runtime progression — 2026-09-26

The protocol-only BOT/SCSI foundation from PR #23 was followed by bounded xHCI
bulk planning (#24) and exact Endpoint Context encoding (#25).

PR #26 activates only the **enumeration/configuration** layer. Candidate
`dc8fbaaed40534cb7346d4e3959f7472057881a1` passed:

- real QEMU `qemu-xhci + usb-storage` enumeration in run `36213602627`;
- USB Mouse regression in run `36213602647`;
- the complete USB Keyboard ring-wrap/hotplug/late-attach matrix in
  run `36213602671`;
- CLA in run `36213602651`.

The guest now performs Address Device, reads the real Mass Storage descriptors,
issues SET_CONFIGURATION, builds independent Bulk IN/OUT Endpoint Contexts and
executes Configure Endpoint. The Device Model child is deliberately left in
`Initializing`; no CBW/data/CSW transaction or block read/write is claimed.

The next transport gate is exact transfer-completion ownership. A bulk
completion must match the exact submitted TRB pointer, slot and endpoint DCI;
Event Data, foreign, misaligned, malformed-residual and failed completion events
must not be accepted. Only after that host contract is qualified should BOT
CBW/data/CSW execution be enabled.
