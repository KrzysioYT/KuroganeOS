# Architektura bieżącej linii

KuroganeOS: własny x86-64 UEFI kernel, runtime `7.0.0-dev`, Flux 8 w rozwoju.
[Status i dowody](../../DEVELOPMENT_STATE.md) • [Plan](../roadmap/MASTER_ROADMAP_15.md).

## Warstwy

UEFI loader → kernel/core → sterowniki i prymitywy ochrony → usługi Ring-3
→ publiczne biblioteki/SDK → sesja, desktop i aplikacje.

Kernel odpowiada za pamięć, procesy/wątki, SMP, syscall validation, urządzenia,
IPC i ochronę zasobów. Procesy ELF64 mają własną przestrzeń adresową i cleanup.
Generacyjne uchwyty zabezpieczają przed ponownym użyciem nieaktualnych zasobów
w implementujących je API. Nie oznacza to ukończenia całej polityki security.

## Usługi

PID 1 nadzoruje eventd, settings, notification, account, session, clipboard,
network events, DNS, application registry i audio. Named IPC obsługuje bind,
connect, accept, bounded send/receive i close; uprawnienia dotyczą realnych PID.
Event Broker używa prawdziwych obiektów zdarzeń z grantami, a nie pozornych
powiadomień. Historyczne 3.4/3.5 mają kwalifikację swojego zakresu; aktualne
zmiany wymagają regresji. Windows/Linux muszą budować te same programy usług.

## Urządzenia i dane

AHCI, NVMe, USB mass storage i VirtIO block integrują się z blokową warstwą
systemu w obsługiwanym zakresie. VFS/FAT32 i KuroFS zapewniają ścieżki trwałych
danych. USB HID oraz PS/2 dostarczają wspólne zdarzenia input. Sieć obejmuje
IPv4/DHCP/DNS/TCP, sockety i TLS/HTTPS. Audio obejmuje AC'97/HDA i audiod.
Dostępność sterownika nie stanowi gwarancji działania na dowolnym urządzeniu.

## Desktop

Flux korzysta z UEFI GOP, software backbuffer i compositora w kernelu.
Aplikacje GUI i usługi sesji działają w Ring-3. Modern/Classic dzielą katalog
aplikacji i palety; metadane tytułów nie nadają uprawnień do Login/Home.
Weryfikowane są pochodzenie procesu i właściciel sesji. Rozdzielenie polityki
UI od kernela pozostaje kierunkiem architektury, nie ukończoną migracją.

[Granice warstw](../ARCHITECTURE_BOUNDARIES.md) i
[pełny kontrakt 8.0](../roadmap/FLUX8_MODERN_COMPLETION.md).
GPU 3D, Wayland, Direct3D ani Chromium nie są deklarowanym gotowym backendem.
