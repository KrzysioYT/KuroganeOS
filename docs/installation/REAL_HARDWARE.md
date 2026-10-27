# Fizyczny sprzęt

KuroganeOS celuje w x86-64 UEFI/GOP na udokumentowanych urządzeniach.
USB HID, AHCI/NVMe, ACPI/APIC i sterowniki sieci istnieją; ich obecność nie
jest dowodem pełnej zgodności z konkretnym komputerem.

Przed próbą sprawdź [macierz sprzętu](../compatibility/HARDWARE_MATRIX.md)
i [ograniczenia](../CURRENT_LIMITATIONS.md). Zachowaj kopie danych i używaj
osobnego dysku testowego; instalacja kasuje wybrany nośnik. Nie ma deklarowanej
uniwersalnej obsługi laptopów ani produkcyjnego Secure Boot.

Dla 14.0/15.0 wymagamy dowodów z fizycznych Intel i AMD: model, firmware,
identyfikatory urządzeń, SHA, hash obrazu, serial/log oraz wyniki każdego scenariusza.
QEMU i VirtualBox nie zastępują tych testów.
