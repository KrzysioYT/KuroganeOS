# Proces startu KuroganeOS

## Artefakty startowe

Firmware x86-64 UEFI uruchamia fallback `EFI/BOOT/BOOTX64.EFI`. Loader otwiera `\kernel.elf` z katalogu głównego tego samego systemu plików. Skrypt budowania umieszcza wymagane pliki zarówno w stagingu, obrazie FAT32, jak i ISO; ręczna edycja obrazu nie jest potrzebna.

Legacy pliki tekstowe w `boot/efi/src/` nie uczestniczą w buildzie. Aktywnym loaderem jest `boot/efi/standalone.c` z linkowaniem opisanym przez `boot/efi/standalone-linker.ld`.

## Kolejność po stronie loadera

1. Firmware ładuje własną aplikację PE32+ `BOOTX64.EFI`.
2. Loader wypisuje wersję i przez około 750 ms przyjmuje `S` albo `F8` jako żądanie safe mode.
3. Loader pozyskuje Graphics Output Protocol. Brak GOP kończy rozruch błędem.
4. Otwiera `\kernel.elf`, odczytuje go do pamięci i waliduje jako 64-bitowy little-endian ELF dla AMD64 typu `ET_DYN`.
5. Waliduje nagłówki programowe, zakresy, wyrównania, brak nakładających się segmentów i brak segmentu jednocześnie zapisywalnego oraz wykonywalnego.
6. Rezerwuje pamięć dla całego obrazu PIE, kopiuje segmenty `PT_LOAD`, zeruje obszary BSS i stosuje wyłącznie relokacje `R_X86_64_RELATIVE` z zaakceptowanej `.rela.dyn`.
7. Buduje `KuroganeBootInfo` w wersji 2: bieżący framebuffer GOP, znormalizowaną mapę pamięci, adres RSDP, fizyczny zakres kernela i flagi startowe.
8. Pobiera świeżą mapę pamięci i wywołuje `ExitBootServices`. Przy `EFI_INVALID_PARAMETER` ponawia sekwencję z nową mapą; liczba prób jest ograniczona.
9. Wywołuje punkt wejścia kernela zgodnie z ABI SysV x86-64, przekazując wskaźnik do `KuroganeBootInfo`.

Loader nie pozostawia usług UEFI dostępnych dla kernela. Po udanym `ExitBootServices` kernel nie może używać Boot Services.

## Kontrakt startowy v2

Kontrakt znajduje się w `common/boot_protocol.h`. Kernel wymaga:

- poprawnej wartości magicznej, numeru wersji i rozmiaru struktury;
- wyłącznie znanych flag — obecnie tylko `KUROGANE_BOOT_FLAG_SAFE_MODE`;
- 32-bitowego framebufferu RGBX8 lub BGRX8 z poprawnym pitch i zakresem adresów;
- co najmniej jednego poprawnego regionu w mapie pamięci;
- wyrównanego i niepustego fizycznego zakresu obrazu kernela;
- braku oznaczenia pamięci zajętej przez kernel jako użytecznej.

`rsdp_address` jest przekazywany, lecz obecny kernel nie ma kompletnej warstwy ACPI i nie używa RSDP do wyłączania czy enumeracji platformy.

## Wejście do kernela

`kernel/arch/x86_64/entry.asm` ustawia własny stos, zeruje `RBP` i wywołuje `kmain`. Pierwsze kroki `kmain` to port szeregowy, walidacja protokołu oraz konfiguracja terminala framebufferowego. Jeżeli danych nie da się zaufać, kernel wypisuje błąd na serialu i zatrzymuje CPU.

Po zaakceptowaniu danych kolejność jest następująca:

1. banner oraz informacja o safe mode;
2. GDT, TSS i osobne stosy IST dla double fault, NMI i machine check;
3. stały heap kernela i PMM z wybranego regionu UEFI;
4. adopcja aktywnego czteropoziomowego `CR3` i runtime self-test map/write/translate/unmap;
5. self-test heapu i ramek fizycznych;
6. inicjalizacja i zasianie RAMFS;
7. w trybie normalnym skan PCI oraz start programowego stosu sieciowego;
8. scheduler callbacków i rejestr aplikacji;
9. IDT, 8259 PIC, PIT 100 Hz i klawiatura PS/2;
10. wymagane testy startowe, inicjalizacja shella i pętla zdarzeń.

Niepowodzenie wymaganego etapu wywołuje kontrolowany `boot_failure`, zapisuje `FAIL` i zatrzymuje system. Wyjątek CPU przechodzi do handlera paniki opisanego w [KERNEL.md](KERNEL.md).

## Safe mode

Loader zapisuje wybór użytkownika w polu `flags`; nie zmienia samego obrazu kernela. Kernel w safe mode:

- nie skanuje PCI;
- nie inicjalizuje usługi sieciowej i oznacza test loopback jako `SKIP`;
- nie rejestruje `desktop`, `monitor`, `files` ani `about`;
- nadal wymaga działających GDT/TSS/IST, heapu, PMM, adaptera VMM i jego self-testu, RAMFS, schedulera, IDT/PIC, PIT, klawiatury i shella.

Safe mode jest minimalnym rozruchem diagnostycznym. Nie montuje nośnika, nie naprawia trwałych danych i nie uruchamia alternatywnego kernela.

## Zatrzymanie i restart

`reboot` próbuje kolejno resetu przez kontroler klawiatury, port PCI `0xCF9` i System Control Port A. `poweroff`/`shutdown` zapisuje do portów wyłączania używanych przez emulatory. Nie ma jeszcze sterownika ACPI, dlatego zachowanie poza wspieranym emulatorem nie jest gwarantowane.
