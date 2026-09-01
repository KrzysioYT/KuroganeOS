# Testowanie KuroganeOS w QEMU

Bieżący runtime ma dwa różne rodzaje obrazu i nie wolno ich mieszać:

```text
kurogane.img
  legacy 64 MiB FAT/EFI artifact

build/images/KuroganeOS-base.img
  Foundation GPT + ESP + Kurogane Root
  pełny userspace, PID1, login, desktop i aplikacje Ring-3
```

Do normalnych testów systemu używamy Foundation GPT.

## Windows — canonical runner

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-qemu.ps1 `
  -UseDiskImage `
  -DiskImagePath .\build\images\KuroganeOS-base.img `
  -ShellTest `
  -TimeoutSeconds 90 `
  -MemoryMiB 1024 `
  -LogName qemu-foundation
```

`ShellTest` zachowuje starą nazwę parametru dla kompatybilności. Na bieżącym
Foundation obrazie nie oznacza już „czekaj zawsze na `kurogane:user$`”. Runner
rozpoznaje graficzny login, aktywuje live session i sprawdza markery PID1,
WindowManagera, loginu i Blade Launcher. Safe Mode nadal używa konsoli.

## Interaktywny rozwój GUI na Windows

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-qemu-fast.ps1 `
  -Accelerator auto `
  -MemoryMiB 1024 `
  -LogName gui-dev
```

Runner próbuje WHPX, a gdy WHPX jest niedostępny wraca do TCG. `qemu-fast` jest
narzędziem interaktywnym, nie zastępuje deterministycznej kwalifikacji.

## WSL wrapper

```bash
./scripts/run-qemu.sh interactive
./scripts/run-qemu.sh system
./scripts/run-qemu.sh safe
./scripts/run-qemu.sh fast
./scripts/run-qemu.sh iso
```

| Tryb | Obraz | Cel |
|---|---|---|
| `interactive` | working image, potem Foundation base | ręczna praca z desktopem |
| `system` | Foundation base | PID1/login/desktop integration |
| `safe` | Foundation base | Safe Mode + emergency console |
| `fast` | deleguje do WHPX/TCG runnera | responsywny GUI development na Windows |
| `iso` | `kurogane.iso` | ręczny boot ISO |
| `smoke` | staged `iso/` FAT | EFI/kernel smoke |
| `img` | jawny/working/base IMG | dedykowany wrapper IMG |
| `headless` | jawny/working/base IMG | test bez okna |
| `debug` | jawny IMG | GDB / zatrzymany CPU |

## Bezpieczeństwo obrazów

System image jest domyślnie podpinany jako QEMU snapshot. Aby pozwolić gościowi
modyfikować dokładnie wybrany plik, trzeba jawnie podać ścieżkę i przełącznik:

```powershell
.\scripts\run-qemu-img.ps1 `
  -ImagePath .\build\test-disks\working-copy.img `
  -Writable
```

Nie używaj `-Writable` na `state/KuroganeOS.img` ani Foundation base bez
świadomej kopii testowej.

Scratch AHCI:

```powershell
.\scripts\run-qemu-headless.ps1 `
  -ImagePath .\build\images\KuroganeOS-base.img `
  -ScratchDiskPath .\build\test-disks\ahci-scratch.img `
  -ShellTest
```

Repozytoryjny scratch jest akceptowany tylko pod `build/test-disks/`.

## Safe Mode

```powershell
.\scripts\run-qemu.ps1 `
  -UseDiskImage `
  -DiskImagePath .\build\images\KuroganeOS-base.img `
  -SafeMode `
  -ShellTest `
  -TimeoutSeconds 60 `
  -LogName safe
```

Safe Mode pomija normalną sesję graficzną i używa diagnostycznego promptu Ring0.

## Debug + GDB

```powershell
.\scripts\run-qemu-debug.ps1 `
  -ImagePath .\build\images\KuroganeOS-base.img `
  -Headless
```

Następnie:

```powershell
.\tools\compiler\x86_64-elf\bin\x86_64-elf-gdb.exe `
  .\build\kernel.elf `
  -ex "target remote 127.0.0.1:1234"
```

## Co runner sprawdza w Foundation

Wymagane są m.in. markery:

```text
kernel_context_switch
kernel_preemption
fat32_vfs_read
process_spawn_wait
ring3_preemption
userspace_init_spawn
userspace_init_pid1
desktop_session
kurogane5_obsidian_login
kurogane5_login_to_desktop
desktop_launcher_ring3
kurogane5_blade_launcher
e1000_link
dhcp_lease
udp_transport
network_gateway_icmp
ALL_REQUIRED_TESTS_PASSED
```

Nie uznajemy samego pojawienia się okna QEMU za PASS.

## Logi

Każdy run powinien mieć unikalny `-LogName`:

```text
build/logs/<LogName>-serial.log
build/logs/<LogName>-stdout.log
build/logs/<LogName>-stderr.log
```

Najważniejszy jest serial. `KERNEL PANIC`, `KERNEL EXCEPTION`, `fatal:` albo
`[TEST] ...: FAIL` kończy test jako failure.

## QEMU a wydajność

Automatyczna kwalifikacja preferuje deterministyczne TCG. Nie używaj jej do
oceny FPS GUI.

Do pomiaru responsywności na Windows:

```text
run-qemu-fast.ps1 -> WHPX
```

Na Apple Silicon x86-64 guest działa przez TCG. Wynik FPS z takiego środowiska
nie reprezentuje wydajności desktopu na sprzęcie x86-64 z akceleracją
wirtualizacji.
