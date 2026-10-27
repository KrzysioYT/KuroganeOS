# Zacznij tutaj

KuroganeOS jest własnym systemem x86-64 UEFI. Wersja kodu to `7.0.0-dev`,
a bieżącym zadaniem jest ukończenie Flux Modern 8.0. To build rozwojowy.

1. Przygotuj repozytorium z submodułami i narzędzia opisane w
   [BUILDING.md](BUILDING.md).
2. Na Windows 11 użyj [instrukcji VirtualBox](VIRTUALBOX.md). Na Linux lub
   macOS użyj odpowiedniego buildera i [QEMU](QEMU_TESTING.md).
3. Zacznij od Try/live na osobnej VM. Instaluj wyłącznie na przeznaczonym
   do tego dysku testowym; instalator wymaga `INSTALL` i kasuje wybrany dysk.
4. Sprawdź logowanie, Home, Files i Terminal, a następnie pełny
   [kontrakt 8.0](roadmap/FLUX8_MODERN_COMPLETION.md).
5. Zapisz SHA kodu, hash obrazu, konfigurację VM i serial log.

MacBook z Apple Silicon potrzebuje QEMU/TCG dla gościa x86-64; nie jest to
natywny system ARM. [Wskazówki macOS](MACOS_DEVELOPMENT.md).

[Indeks](README.md) • [Stan](../DEVELOPMENT_STATE.md) •
[Ograniczenia](CURRENT_LIMITATIONS.md) • [SDK](DEVELOPERS/README.md)
