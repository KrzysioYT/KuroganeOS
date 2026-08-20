# KuroganeOS documentation

Ta strona jest mapą **kanonicznej dokumentacji** KuroganeOS 3.3.3-dev.

Jeżeli dwa dokumenty opisują ten sam temat inaczej, traktuj poniższą hierarchię jako źródło prawdy. Pliki z `docs/releases/`, datowane audyty i stare ścieżki zgodności są materiałem historycznym i nie powinny nadpisywać bieżącego stanu `main`.

## Zacznij tutaj

- [`START_HERE.md`](START_HERE.md) — pierwszy start, wybór artefaktu i najczęstsze problemy.
- [`VIRTUALBOX.md`](VIRTUALBOX.md) — kanoniczna konfiguracja Oracle VirtualBox.
- [`INSTALLATION.md`](INSTALLATION.md) — Try/Install, SATA/AHCI i instalacja na VDI.
- [`RUNNING.md`](RUNNING.md) — uruchamianie gotowych mediów.
- [`BUILDING.md`](BUILDING.md) — budowanie projektu.

## Aktualny stan projektu

- [`ROADMAP.md`](ROADMAP.md) — **główne źródło prawdy dla aktywnych prac i stanu funkcji**.
- [`BUILD_STATUS.md`](BUILD_STATUS.md) — krótki snapshot bieżącej kwalifikacji build/runtime.
- [`CURRENT_LIMITATIONS.md`](CURRENT_LIMITATIONS.md) — czego nadal nie należy traktować jako ukończonego.

Aktualna linia to **3.3.3-dev / DEV BETA**.

## Sieć i HTTPS

- [`NETWORKING.md`](NETWORKING.md) — NIC, DHCP, DNS, TCP, TLS/HTTPS i diagnostyka.
- `kernel/net/tcp_client.cpp` — bieżący klient TCP.
- `kernel/net/tls/client.cpp` — freestanding Mbed TLS 3.6.7 client/BIO.

Bieżący stan HTTPS: DNS i TCP są dostępne, Mbed TLS 3.6.7 oraz X.509/trust store są zintegrowane, ale **TLS nie jest jeszcze release-qualified end-to-end**. Aktualny blocker na `main` jest w ścieżce TCP/BIO podczas handshake; nie należy opisywać problemu jako braku całego TLS ani jako samego błędu trust store.

## Pozostałe subsystemy

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — architektura systemu.
- [`ARCHITECTURE_BOUNDARIES.md`](ARCHITECTURE_BOUNDARIES.md) — granice warstw i zależności.
- [`DRIVERS.md`](DRIVERS.md) — sterowniki i hardware.
- [`FILESYSTEM.md`](FILESYSTEM.md) — VFS/FAT32.
- [`GUI.md`](GUI.md) — Red Flux / WindowManager.
- [`AUDIO.md`](AUDIO.md) — AC'97 i audio.
- [`GRAPHICS_COMPATIBILITY.md`](GRAPHICS_COMPATIBILITY.md) — graphics runtime i ograniczenia Direct3D.
- [`CHROMIUM_PORT.md`](CHROMIUM_PORT.md) — wymagania przyszłego portu Chromium.

## Dla programistów

- [`DEVELOPERS/README.md`](DEVELOPERS/README.md) — wejście do SDK/developmentu.
- [`DEVELOPERS/APP_DEVELOPMENT.md`](DEVELOPERS/APP_DEVELOPMENT.md) — aplikacje Ring-3.
- [`DEVELOPERS/GUI_APPLICATIONS.md`](DEVELOPERS/GUI_APPLICATIONS.md) — aplikacje GUI.
- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md) — publiczne API.
- [`DEVELOPERS/KERNEL_CONTRIBUTION.md`](DEVELOPERS/KERNEL_CONTRIBUTION.md) — kernel i sterowniki.

## Dokumenty historyczne

Następujące pliki są przydatne jako zapis konkretnego momentu, ale **nie są bieżącym źródłem prawdy**:

- `docs/releases/*` — release notes dla konkretnych rewizji;
- `AUDIT_2026-08-18.md` — snapshot audytu z 18 sierpnia 2026;
- `ARCHITECTURE_DEBT_3.3.3.md` — snapshot długu/analizy dla określonej rewizji;
- stare ścieżki typu `docs/installation/VIRTUALBOX.md` lub lowercase compatibility files — zachowane tylko jako przekierowania dla dawnych linków.

## Zasada aktualizacji dokumentacji

Zmiana subsystemu jest kompletna dopiero wtedy, gdy w tym samym cyklu zmian zgadzają się:

1. kod,
2. testy/kwalifikacja runtime,
3. publiczne API, jeżeli dotyczy,
4. `ROADMAP.md`,
5. odpowiedni dokument subsystemu.

Nie oznaczamy funkcji jako gotowej tylko dlatego, że istnieje stub, plik się kompiluje albo host-test przechodzi bez runtime smoke na właściwej ścieżce.
