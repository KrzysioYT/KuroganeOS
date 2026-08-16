# Virtual File System

## Kontrakt docelowy

VFS ma udostępniać `open`, `close`, `read`, `write`, `seek`, `stat`, `create`, `unlink`, `rename`, `mkdir`, `rmdir`, `readdir`, `mount`, `unmount` i `sync`. Typy węzłów: regular, directory, device, pipe i mountpoint.

Proces będzie posiadał własny root, CWD i tablicę file descriptors. Normalizacja `.`, `..`, wielokrotnych separatorów oraz ścieżek względnych powinna pozostać wspólną usługą, nie kodem filesystemu.

## Stan bieżący

RAMFS jest zapisywalnym, hierarchicznym filesystemem używanym przez kernel shell. Read-only adapter FAT32 implementuje dla VFS `stat/open/close/read/stat_open/readdir/sync` z fixed-size handle table i generation checks. Kernel wybiera GPT `Kurogane Root`, tworzy `PartitionDevice`, montuje FAT32/VFS i odczytuje `/etc/system.conf` przez AHCI. Shell nie używa jeszcze tego namespace ani FD i wszystkie tworzone w nim dane znikają po restarcie.

## Integracja storage

Bieżąca ścieżka read-only to `AHCI BlockDevice → GPT PartitionDevice → FAT32 driver → VFS mount`. Następny etap wstawia Block Cache i bezpieczne mutacje FAT32. FAT32 nie może przeciekać do API aplikacji. Sukces wymaga operacji modyfikujących i testu persistence na dwóch bootach.
