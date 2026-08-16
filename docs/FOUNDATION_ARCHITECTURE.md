# Architektura KuroganeOS Foundation

## Warstwy docelowe

```text
Applications
  ├─ Kurogane Native
  └─ KuroPOSIX
          ↓
      KuroLibC/API
          ↓
       Syscalls
          ↓
  Process Core ── VFS
       ↓           ↓
   Scheduler   Filesystems
       ↓           ↓
   Memory Core  Block Core
          \       /
       Device Manager
       ├─ PCI
       ├─ USB
       └─ Platform/ACPI
```

Kernel pozostaje freestanding i nie zależy od userspace libc. `libk` dostarcza tylko prymitywy potrzebne kernelowi. Sterowniki publikują wspólne interfejsy urządzeń; filesystem nie może wywoływać AHCI bezpośrednio. KuroPOSIX ma być adapterem nad natywnym API, nie kopią kernela Linux.

## Stan bieżący

Zaimplementowana ścieżka to UEFI → kernel → PCI → Device/Driver Manager → AHCI → BlockDevice → GPT. VFS/FAT32 istnieje częściowo w kodzie i testach hostowych, lecz nie zamyka jeszcze runtime ścieżki do rootfs. Process Core, syscall transport oraz userspace są etapami późniejszymi.

## Granice odpowiedzialności

- `boot/`: loader, walidacja ELF i protokół startowy.
- `kernel/arch/x86_64/`: mechanizmy CPU, przerwania i paging zależny od architektury.
- `kernel/drivers/`: wykrywanie, lifecycle i obsługa sprzętu.
- `kernel/storage/`: abstrakcja blokowa, partycje i backendy storage.
- `kernel/fs/`: namespace, VFS i filesystemy.
- `kernel/libk/`: biblioteka freestanding kernela.
- `userspace/`: w przyszłości procesy, libc, narzędzia i usługi.

## Zasady

Brak urządzenia opcjonalnego degraduje funkcję, nie kończy bootu. Każde oczekiwanie sprzętowe ma timeout. Zasób ma jednego właściciela. Stan `Working` wymaga testu na odpowiedniej warstwie. Console Mode pozostaje domyślny do zakończenia Milestone 0–10.
