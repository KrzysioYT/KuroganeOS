# USB Stack

## Planowana architektura

```text
xHCI → USB Core → Device → Configuration → Interface → Class Driver
                                                   ├→ HID
                                                   └→ Mass Storage
```

USB Core będzie odpowiadać za deskryptory, adresację, konfiguracje, endpointy, transfery, timeouty i disconnect. xHCI pozostanie warstwą kontrolera. HID będzie publikować zdarzenia przez wspólny `InputDeviceOps`, a Mass Storage przez `BlockDeviceOps`.

## Kolejność

1. xHCI reset i command/event rings.
2. Root hub i enumeration jednego urządzenia.
3. USB HID keyboard, potem mouse.
4. Disconnect/reconnect i failure injection.
5. USB Mass Storage jako zwykły block device.

## Stan

Niezaimplementowane. `CONFIG_USB=n` i `CONFIG_XHCI=n` są bezpiecznymi wartościami domyślnymi. Istnieje tylko wspólny kontrakt zdarzeń input przygotowany w Driver Framework.
