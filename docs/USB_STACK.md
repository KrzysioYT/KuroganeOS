# USB Stack

## Planowana architektura

```text
xHCI → USB Core → Device → Configuration → Interface → Class Driver
                                                   ├→ HID
                                                   └→ Mass Storage
```

USB Core odpowiada za deskryptory, adresację, konfiguracje, endpointy, transfery, timeouty i disconnect. xHCI pozostaje warstwą kontrolera. HID publikuje zdarzenia przez wspólny `InputDeviceOps`, a Mass Storage przez `BlockDeviceOps`.

## Stan

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
- xHCI ma ścieżkę resetu, ringów, enumeracji i HID keyboard; walidator capability/runtime/doorbell/port MMIO odrzuca overflow i obcięte okna po mapowaniu, przed dostępem do tych rejestrów. Enumeracja i klawiatura przeszły kwalifikację QEMU opisaną poniżej; realny sprzęt, mouse, disconnect/reconnect i Mass Storage pozostają otwarte.
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

1. Obsługa Port Status Change w `xhci::poll()`: disconnect, zwolnienie
   przytrzymanych klawiszy, bezpieczny cleanup i ponowna enumeracja.
2. HID mouse i wspólny routing input.
3. USB Mass Storage jako `BlockDeviceOps`.
4. Failure injection i stress testy hotplug.
