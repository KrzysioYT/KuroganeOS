# Architektura KuroganeOS

## Status architektury

KuroganeOS jest samodzielnym kernelem x86-64 uruchamianym przez własną aplikację UEFI. Nie używa kernela Linux w systemie gościa. WSL2 i Windows są wyłącznie środowiskiem budowania oraz testowania.

Obecna wersja jest **kernel preview**, a nie kompletnym systemem użytkowym. Cały kod po wyjściu z UEFI wykonuje się w ring 0, na jednym procesorze i w jednej przestrzeni adresowej. Nie ma jeszcze procesów użytkownika, syscalli ani granicy ochrony między shellem, aplikacjami i kernelem.

## Przepływ wykonania

```text
firmware UEFI
  -> EFI/BOOT/BOOTX64.EFI
  -> walidacja i relokacja kernel.elf
  -> KuroganeBootInfo v2
  -> ExitBootServices
  -> kmain
  -> GDT/TSS/IST, heap, PMM, przejęcie tablic UEFI, RAMFS, scheduler, przerwania
  -> shell framebufferowy i pętla zdarzeń kernela
```

Szczegółowy przebieg opisuje [BOOT_PROCESS.md](BOOT_PROCESS.md).

## Warstwy repozytorium

| Warstwa | Kod | Rzeczywista odpowiedzialność |
| --- | --- | --- |
| Loader | `boot/efi/` | aplikacja PE32+ UEFI, odczyt ELF64 PIE, GOP, mapa pamięci, flaga safe mode i `ExitBootServices` |
| Protokół startowy | `common/boot_protocol.h` | wersjonowany kontrakt loader–kernel: framebuffer, mapa pamięci, RSDP, zakres kernela i flagi |
| Architektura | `kernel/arch/x86_64/` | wejście ASM, GDT, TSS, stosy IST, IDT, stuby przerwań i operacje I/O |
| Kernel | `kernel/main.cpp` | walidacja danych startowych, kolejność inicjalizacji, self-testy i główna pętla zdarzeń |
| Pamięć | `kernel/memory/` | stały heap, allocator ramek, walker tablic stron i adapter adoptujący aktywne czteropoziomowe tablice UEFI |
| Sterowniki | `kernel/drivers/` | framebuffer, port szeregowy, 8259 PIC, PIT, PS/2 keyboard, RTC i enumeracja PCI |
| Usługi ring 0 | `kernel/fs/`, `kernel/task/`, `kernel/net/` | ulotny RAMFS, scheduler wywołań zwrotnych i stos sieciowy używany z loopbackiem |
| Interfejs | `kernel/terminal.cpp`, `kernel/shell/`, `kernel/ui/`, `kernel/apps/` | terminal framebufferowy, shell oraz pełnoekranowe widoki wykonywane w kernelu |
| Publiczny ABI/SDK | `kernel/abi/`, `sdk/` | deskryptor ABI i nagłówki do kompilacji; bez transportu syscall, linkowania i uruchamiania programów |
| Narzędzia hosta | `scripts/`, `tests/` | build, obrazy FAT32/ISO, QEMU, VirtualBox i testy uruchamiane na hoście |

## Model wykonania

PIT generuje monotoniczne tyknięcia. Handler przerwania tylko aktualizuje czas i gotowość zadań. Pętla w `kernel/main.cpp` wykonuje z ograniczonym budżetem callbacki schedulera, odpytuje usługę sieciową i przekazuje znaki klawiatury do aktywnego widoku albo shella. `yield()` jest prośbą o ponowne wywołanie callbacku po jego powrocie; nie przełącza stosu ani kontekstu CPU.

Warstwa aplikacji utrzymuje rejestr callbacków i co najwyżej jeden aktywny widok. `desktop`, `monitor`, `files` i `about` nie są procesami. Używają tych samych globalnych usług i tego samego heapu co reszta kernela.

## Granice, których jeszcze nie ma

- `kernel/memory/kernel_virtual_memory.*` odczytuje aktywny `CR3`, adoptuje istniejące czteropoziomowe tablice UEFI i wykonuje rzeczywisty self-test map/write/translate/unmap. Nie buduje jednak własnej kompletnej mapy kernela, nie przełącza `CR3` i zakłada, że potrzebna pamięć fizyczna pozostaje dostępna przez identity mapping firmware.
- PMM wybiera jeden największy użyteczny region z mapy UEFI; nie zarządza wszystkimi regionami fizycznymi.
- RAMFS jest jedynym systemem plików kernela. Pliki z obrazu startowego nie są montowane po starcie, a zmiany RAMFS nie trafiają na dysk.
- Stos sieciowy ma rzeczywiste parsery Ethernet/ARP/IPv4/ICMP, lecz aktywnym transportem jest tylko interfejs programowy loopback. Nie istnieje sterownik NIC.
- Framebuffer jest powierzchnią pełnoekranową. Nie ma serwera wyświetlania, kompozytora ani menedżera okien.
- Deskryptor ABI reklamuje `available_features == 0`; nie istnieje ścieżka z programu ring 3 do usług kernela.

## Tryb normalny i safe mode

| Element | Tryb normalny | Safe mode |
| --- | --- | --- |
| Walidacja protokołu, GDT/TSS/IST, heap, PMM i self-test VMM | tak | tak |
| RAMFS, scheduler, PIC, PIT i klawiatura | tak | tak |
| Skan PCI | tak | pominięty |
| Usługa sieciowa i test loopback | tak | pominięte (`SKIP`) |
| Rejestracja widoków graficznych | tak | pominięta |
| Shell diagnostyczny | tak | tak |

Safe mode ogranicza liczbę inicjalizowanych modułów, ale nie zapewnia izolacji, trwałego trybu naprawczego ani sterownika dysku. Więcej: [KERNEL.md](KERNEL.md) i [CURRENT_LIMITATIONS.md](CURRENT_LIMITATIONS.md).

## Zasady awarii

Loader odrzuca niezgodny ELF, brak GOP lub nieudaną mapę pamięci. Kernel odrzuca nieznaną wersję albo flagi protokołu oraz niepoprawny framebuffer. Błąd wymaganego PMM, RAMFS, schedulera, timera, klawiatury lub normalnego testu loopback kończy rozruch kontrolowanym zatrzymaniem. Wyjątki CPU drukują wektor, kod błędu i rejestry, po czym zatrzymują system.

Pełny audyt stanu znajduje się w [PROJECT_AUDIT.md](PROJECT_AUDIT.md).
