# Storage Stack

## Model

```text
AHCI / future NVMe / VirtIO
          ↓
      BlockDevice
          ↓
   future Block Cache
          ↓
 Partition Manager (GPT)
          ↓
       VFS → FAT32
```

Filesystem nie może wywoływać funkcji AHCI bezpośrednio. `BlockDevice` waliduje zakres LBA, rozmiary bufora, read/write i flush. `PartitionDevice` ogranicza dostęp do zakresu partycji.

## Działające elementy

AHCI na QEMU q35 wykrywa kontroler i SATA, wykonuje IDENTIFY, read, write oraz flush z timeoutami. Parser GPT waliduje CRC i zakresy i przez AHCI wykrywa dwie partycje base IMG. Root partition jest ograniczana przez `PartitionDevice`, montowana jako read-only FAT32/VFS, a `/etc/system.conf` jest odczytywany w runtime. `diskinfo` pokazuje rzeczywistą geometrię i stan rootfs.

Test zapisu używa osobnego obrazu z tagiem, zakresu LBA 8–15 i sekwencji `read original → write pattern → flush → readback → restore → flush`. Base image jest podłączany w trybie snapshot.

## Kryterium Milestone 3

Milestone kończy dopiero runtime mount FAT32, create/read/write/rename/delete, block cache z dirty/flush, `sync` oraz odczyt tego samego pliku po drugim boocie. Obecny AHCI/GPT jest działającą dolną połową stosu, nie kompletnym filesystemem.
