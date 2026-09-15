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
- xHCI ma ścieżkę resetu, ringów, enumeracji i HID keyboard; walidator capability/runtime/doorbell MMIO odrzuca overflow i obcięte okna przed mapowaniem. Nadal wymaga kwalifikacji na sprzęcie/QEMU; mouse, disconnect/reconnect i Mass Storage pozostają otwarte.

## Kolejność dalszych prac

1. Kwalifikacja xHCI resetu, command/event rings i root-hub enumeration.
2. USB HID keyboard na QEMU oraz obsługa disconnect/reconnect.
3. HID mouse i wspólny routing input.
4. USB Mass Storage jako `BlockDeviceOps`.
5. Failure injection i stress testy hotplug.
