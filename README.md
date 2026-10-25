# KuroganeOS

Autorski system operacyjny **x86-64 UEFI** z własnym kernelem, sterownikami,
usługami i natywnym pulpitem Flux. Nie jest dystrybucją Linuxa.

**Wersja kodu: 7.0.0-dev. Priorytet: ukończenie Flux Modern 8.0.
Cel końcowy: 15.0.0 stable. Projekt pozostaje w rozwoju.**

Bieżąca praca: `codex/road-to-15-stability`,
[PR #55](https://github.com/KrzysioYT/KuroganeOS/pull/55).
[Stan i dowody](DEVELOPMENT_STATE.md) rozróżniają implementację od kwalifikacji.

## Uruchomienie

Na Windows 11, z katalogu repozytorium po przygotowaniu toolchainu i WSL:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-flux8-virtualbox.ps1
```

Helper aktualizuje gałąź przez fast-forward, buduje media i tworzy lub uruchamia
VM. Istniejąca VM musi być wyłączona. Nie usuwa VDI; nowe ISO nie naprawia
automatycznie plików starej instalacji.

- [Pierwsze uruchomienie](docs/START_HERE.md)
- [Budowanie na Windows, Linux i macOS](docs/BUILDING.md)
- [VirtualBox i diagnostyka](docs/VIRTUALBOX.md)
- [Pełna dokumentacja](docs/README.md)

## Zakres

Kod zawiera Ring-3 ELF64, scheduler SMP, IPC i usługi kont/sesji, FAT32/KuroFS,
sterowniki AHCI/NVMe/USB, sieć IPv4/TCP/TLS oraz audio AC'97/HDA.
Ich zakres i dowody zależą od urządzenia oraz profilu testowego.
Flux używa software compositora i UEFI GOP; Modern i Classic korzystają
z natywnych aplikacji i wspólnego SDK. Nie deklarujemy akceleracji 3D,
Direct3D, pełnego Chromium ani uniwersalnej zgodności PC.

## Rozwój

- [Roadmapa do 15.0](docs/roadmap/MASTER_ROADMAP_15.md)
- [Warunki ukończenia Flux 8](docs/roadmap/FLUX8_MODERN_COMPLETION.md)
- [SDK i aplikacje](docs/DEVELOPERS/README.md)
- [Ograniczenia](docs/CURRENT_LIMITATIONS.md)
- [Testy i akceptacja](docs/TESTING.md)

## Licencja

Aktualne rewizje KuroganeOS są udostępniane na warunkach
**KuroganeOS Source-Available License 2.0 (KSAL-2.0)**.

Jest to licencja **source-available**, a nie licencja Open Source zatwierdzona
przez OSI. Domyślnie pozwala m.in. na prywatne użycie niekomercyjne, naukę,
prywatne modyfikacje, wkład przez fork/PR, niezależne kompatybilne implementacje
bez kopiowania chronionej formy kodu oraz recenzje, filmy i streamy w zakresie
opisanym w licencji. Komercyjne użycie samego KuroganeOS, redystrybucja
nieoficjalnych buildów/ISO i wykorzystanie chronionego kodu w innych produktach
wymagają osobnej pisemnej zgody.

- [LICENSE](LICENSE)
- [docs/LICENSING.md](docs/LICENSING.md)
- [CLA.md](CLA.md)
- [CONTRIBUTING.md](CONTRIBUTING.md)
- [TRADEMARKS.md](TRADEMARKS.md)
- [LICENSE-MIT-LEGACY](LICENSE-MIT-LEGACY) — wyłącznie historyczne rewizje MIT
