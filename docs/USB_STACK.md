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
trafia do zwykłego builda. Samo dodanie workflow nie oznacza kwalifikacji.

1. Kwalifikacja xHCI resetu, command/event rings i root-hub enumeration.
2. USB HID keyboard na QEMU oraz obsługa disconnect/reconnect.
3. HID mouse i wspólny routing input.
4. USB Mass Storage jako `BlockDeviceOps`.
5. Failure injection i stress testy hotplug.
