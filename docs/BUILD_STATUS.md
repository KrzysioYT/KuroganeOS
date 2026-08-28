# Build status

Data: 28 sierpnia 2026 r.
Audited HEAD: `17bd55091c63544b9585840192f0eb288e9cffff`

## Current stage

KuroganeOS **3.3.3-dev — DEV BETA** pozostaje bieżącym numerem wersji w kodzie.
Następny release, **3.3.4-dev**, jest w fazie `QUALIFICATION` i skupia się na
pełnej kwalifikacji Oracle VirtualBox, bez sztucznego podbijania wersji przed
realnym PASS.

## Working foundation

- własny x86-64 UEFI `BOOTX64.EFI` i boot protocol v3;
- VMM, GDT/TSS/IST, IDT;
- Ring 3, ELF64, PID/TID, spawn/wait/exit i `/system/init` jako PID 1;
- AHCI, GPT, writable FAT32/VFS i persistent root;
- publiczny Ring-3 filesystem ABI obejmujący podstawowy odczyt/zapis oraz
  operacje katalogowe obecnej generacji;
- Try/Install media i read-only live package root;
- instalator GPT/FAT32 z językiem, profilem i opcjonalnym hasłem DEV;
- PS/2, PCI i ACPI/MADT/APIC discovery;
- Red Flux Login/Desktop i aplikacje Ring 3;
- software framebuffer/backbuffer i damage-style GOP scanout;
- natywny stos sieciowy z obsługiwanymi w obecnej generacji wirtualnymi NIC;
- aktywne poprawki TCP/TLS transportu w bieżącym HEAD;
- Intel ICH AC'97 kernel PCM backend i bounded Ring-3 playback;
- build tooling dla Windows/WSL, macOS i Linux x86-64;
- ISO z El Torito EFI + GPT ESP i strukturalnym verifierem;
- GitHub Actions z host tests, full regression suite, media build, FAT32/VFS
  image validation, OVMF/QEMU boot i QEMU NAT network qualification.

## 3.3.4 VirtualBox qualification

Istniejący `scripts/smoke-virtualbox-iso.ps1` tworzy prawdziwą VM Oracle
VirtualBox i obejmuje znaczną część ścieżki instalacyjnej:

```text
EFI64
-> ISO boot
-> installer
-> SATA / IntelAHCI VDI
-> install
-> detach ISO
-> disk-first reboot
-> persistent FAT32 root
-> PID 1
-> DHCP / gateway / DNS checks
```

Brakującym gate przed 3.3.4 jest osobna kwalifikacja:

```text
ISO -> Try -> Login -> Red Flux Desktop
```

Ten gate jest implementowany na gałęzi rozwojowej `dev/road-to-15` i musi
zostać wykonany na realnym hoście x86-64 z Oracle VirtualBox przed zamknięciem
3.3.4-dev.

## Automated qualification state

Workflow `.github/workflows/virtualbox-iso.yml` zawiera realne automatyczne gate:

```text
kernel test configuration
host ABI / SDK regression tests
full host regression suite
Linux IMG + ISO build
production FAT32/VFS image validation
20-pass ISO structure verifier
OVMF/QEMU optical UEFI boot
QEMU E1000 NAT qualification
QEMU PCnet NAT qualification
QEMU VirtIO-net NAT qualification
artifact publication
```

Dla zmian tworzonych po tym audycie wynik jest `PENDING`, dopóki odpowiedni
workflow na kandydacie nie zakończy się sukcesem. Historyczny PASS nie jest
przenoszony automatycznie na nowy commit.

## Oracle VirtualBox status

```text
VirtualBox qualification tooling: IMPLEMENTED
Install -> VDI -> reboot tooling: IMPLEMENTED
Try -> Login -> Desktop gate: IN PROGRESS
Final 3.3.4 real-host run: PENDING
```

Środowisko wykonawcze bieżącej sesji nie posiada dostępu do Oracle VirtualBox,
więc nie wolno oznaczyć runtime jako PASS wyłącznie na podstawie inspekcji kodu.

## Known gaps / DEV warnings

- `FNV1A64-DEV` nie jest bezpiecznym password KDF;
- brak finalnego users/groups/ACL/capability security model;
- brak SMP i SMP-aware schedulera;
- NVMe nie jest jeszcze równorzędnym, produkcyjnie kwalifikowanym backendem;
- USB/xHCI wymaga dalszej implementacji i kwalifikacji;
- Intel HDA nie jest jeszcze docelowym audio backendem;
- userspace networking pozostaje przed docelowym async service/socket ABI;
- TLS nie jest oznaczone jako complete; bieżący HEAD zawiera aktywne prace nad
  retry/backpressure/CLOSE-WAIT/TLS transportem;
- brak Direct3D 9/10/11/12 i produkcyjnego GPU acceleration;
- real-hardware qualification jest węższa niż VM qualification;
- brak finalnego recovery environment i transakcyjnego updatera.

## Source of truth

Postęp release: `docs/roadmap/CURRENT_RELEASE.md`

Plan do 15.0.0: `docs/roadmap/MASTER_ROADMAP_15.md`

Ograniczenia: `docs/CURRENT_LIMITATIONS.md`

Audyt baseline: `docs/audits/HEAD_AUDIT_2026-08-28.md`
