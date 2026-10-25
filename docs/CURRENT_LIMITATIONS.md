# Bieżące ograniczenia

Stan: 2026-10-25; runtime `7.0.0-dev`. [Dowody i SHA](../DEVELOPMENT_STATE.md).

## Flux 8 i VirtualBox

QEMU USB-only potwierdził Login, Home oraz rzeczywiste Files i Terminal na
`7f06237`. Nie zastępuje to pełnej akceptacji desktopu ani VirtualBox.
Zgłoszenie account lookup FAIL / restartów usług na zainstalowanym obrazie
VirtualBox pozostaje otwarte do testu świeżych mediów i plików instalacji.
Windows build od `9c8700c` uwzględnia wszystkie 33 programy userspace.

Pozostała praca: skalowalne fonty zamiast przejściowego 5x7, HiDPI i tryby GOP,
pełna dostępność, drag/drop, ogólna edycja clipboard, testy wszystkich aplikacji,
recovery i trwałości ustawień. [Pełny kontrakt](roadmap/FLUX8_MODERN_COMPLETION.md).

## Platforma i sprzęt

- Wyłącznie x86-64 UEFI. Apple Silicon wymaga emulacji gościa x86-64.
- SMP, NVMe, USB HID/mass storage i HDA są zaimplementowane i mają zakresowe
  dowody; nie są już „brakującymi sterownikami”, ale nie oznaczają obsługi
  każdego fizycznego kontrolera, huba czy laptopa.
- Brak deklarowanej pełnej obsługi Wi-Fi, szerokich USB hubs i suspend/resume.
- GOP/software compositor; brak kwalifikowanego sprzętowego 3D/Direct3D.
- FAT32 i KuroFS mają zapisywalne ścieżki. Pełny produktowy storage, uprawnienia,
  migracje i odporność na wszystkie awarie nie są ukończone.

## Usługi, sieć i bezpieczeństwo

Usługi event/settings/notification/account/session/clipboard, publiczne sockety,
DNS, TLS/HTTPS, audiod i registry istnieją. Historyczne błędy Event Broker
z 3.4 nie są bieżącym statusem całej platformy. Nadal potrzebna jest regresja
na dokładnym kandydacie oraz obsługa awarii i przeciążenia.

Nie ma pełnej przeglądarki Chromium, kompletnego POSIX, browser-grade sieci,
IPv6 ani finalnego bezpieczeństwa systemu. Rozwojowe przechowywanie haseł
nie stanowi finalnego KDF. Podpisane aktualizacje, rollback i recovery oraz
pełne users/groups/permissions pozostają zakresem roadmapy.

## Akceptacja

VirtualBox jest wymaganym zewnętrznym testem Flux 8. Brak dostępu oznacza
UNVERIFIED i pozostawia gate otwarty; nie zastępujemy go testem statycznym.
Fizyczne Intel/AMD i pełna macierz kompatybilności pozostają wymaganiem
14.0/15.0. [Macierz sprzętu](compatibility/HARDWARE_MATRIX.md).
