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
- xHCI ma ścieżkę resetu, ringów, enumeracji i HID keyboard; walidator capability/runtime/doorbell/port MMIO odrzuca overflow i obcięte okna po mapowaniu, przed dostępem do tych rejestrów. Nadal wymaga kwalifikacji na sprzęcie/QEMU; mouse, disconnect/reconnect i Mass Storage pozostają otwarte.

## Kolejność dalszych prac

Workflow `Qualify 5.0 xHCI USB Keyboard` buduje pełny obraz po dodaniu
wyłącznie diagnostyki do produkcyjnego `handle_keyboard_report()`.
QEMU udostępnia `qemu-xhci` i `usb-kbd`; dopiero po starcie Login QMP
wysyła F12, dwukrotnie. PASS wymaga rzeczywistej enumeracji, odebrania raportów
USB oraz dwóch poprawnie uporządkowanych par press/release opublikowanych
do kolejki input. Instrumentacja nie tworzy raportów ani backendu USB i nie
trafia do zwykłego builda. Pierwsza kwalifikacja tego zakresu przeszła na SHA
`73aba709c535261b99d23182ecb4b2ead091b6e1` w Actions
[35059962106](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35059962106),
job `104677993041`: pełny host-suite, clean release media, enumeracja
i dwie rzeczywiste pary F12 press/release. Nie jest to kwalifikacja hotplug,
hubów, wielu urządzeń ani USB Mass Storage.

1. Kwalifikacja xHCI resetu, command/event rings i root-hub enumeration.
2. USB HID keyboard na QEMU oraz obsługa disconnect/reconnect.
3. HID mouse i wspólny routing input.
4. USB Mass Storage jako `BlockDeviceOps`.
5. Failure injection i stress testy hotplug.
