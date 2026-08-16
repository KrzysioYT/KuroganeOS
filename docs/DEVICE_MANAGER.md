# Device Manager

Device Manager jest centralnym rejestrem urządzeń kernela. Każdy wpis zawiera ID, type, bus, vendor/device, class/subclass/prog-if, adres bus, status, resources, owner driver oraz relacje parent/children.

## Typy i magistrale

Typy obejmują m.in. `Display`, `StorageController`, `Block`, `Input`, `Network`, `UsbController` i `Bridge`. Magistrale to `Platform`, `Pci`, `Usb` i `Virtual`. Pojemności tablic są obecnie statyczne: 192 urządzenia, 8 resources i 16 dzieci na urządzenie.

## Rejestracja runtime

PCI scan tworzy wpis dla każdego znalezionego urządzenia. Driver Manager przypisuje sterownik AHCI do kontrolera. Każdy wykryty dysk jest dzieckiem kontrolera typu `Block` i dziedziczy ownership `ahci`.

## Diagnostyka

```text
device list
device info <id>
driver list
driver info <name>
diskinfo
```

W zweryfikowanym profilu QEMU rejestr zawiera 5 urządzeń PCI i 2 dyski blokowe. Safe mode inicjalizuje pusty manager i pomija scan PCI.

## Ograniczenia

Brak hotplug, usuwania wpisów, persistent IDs, sysfs/devfs oraz centralnej rezerwacji zakresów MMIO/IRQ/DMA.
