# Driver Model

## Lifecycle

```text
DISCOVERED → PROBING → INITIALIZING → READY
                         ├→ DEGRADED
                         └→ FAILED
```

Driver rejestruje nazwę, priorytet, timeout oraz callbacki `match`, `probe`, `attach` i opcjonalny `detach`. Driver Manager wybiera pasujące sterowniki od najwyższego priorytetu. Nieudany probe/attach zwalnia ownership i pozwala spróbować kolejnego kandydata.

## Wspólne interfejsy

`kernel/drivers/core/interfaces.hpp` definiuje:

- `BlockDeviceOps`: `read_blocks`, `write_blocks`, `flush`, geometria;
- `NetworkDeviceOps`: `send`, `receive`;
- `InputDeviceOps`: jednolite zdarzenia klawiatury i myszy.

Operacje zwracają `KStatus`. Wyższa warstwa nie może zależeć od konkretnego AHCI, NVMe, VirtIO ani PS/2.

## Ownership i timeout

Device Manager pozwala przypisać urządzenie tylko jednemu driverowi. Próba drugiego claim zwraca `Busy`. Lifecycle otrzymuje jawny budżet czasu; pętle sprzętowe muszą być ograniczone. Awaria urządzenia opcjonalnego ustawia `FAILED`/`DEGRADED` i boot jest kontynuowany.

## Stan

Framework i AHCI są podłączone. Brakuje Resource Managera dla IRQ/MMIO/DMA, hotplug, runtime detach oraz modelu współbieżnego dla SMP.
