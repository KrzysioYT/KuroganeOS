# Flux Modern 8 — ręczna macierz VirtualBox na Windows 11

## Wyniki dla kandydata `d0bb7511`

- SHA źródeł `d0bb7511ebd4ab1a67928d02e7d9b364defffab9`. [Przebieg GitHub Actions](https://github.com/KrzysioYT/KuroganeOS/actions/runs/42546543990)
- SHA-256 obrazu `5f24b9c49811f4137e7a92cd26e97e5bf0bc8cbe4ddbde529fe6de22e2ad6d5d`. Sumę pobranego pliku ISO przeliczono i porównano z `iso-sha256.txt`. `source-sha.txt` i metadane przebiegu wskazują ten sam commit. [Artefakt ISO](https://github.com/KrzysioYT/KuroganeOS/actions/runs/42546543990/artifacts/13643782633)
- Widoczne sesje VirtualBox 7.2.20 na Windows 11. EFI64, klawiatura i mysz PS/2, SATA AHCI, PCnet, COM1. VM `KuroganeOS-Flux8-d0bb7511-QA` uruchamiała instalację z dysku przy 1280×800, a VM `KuroganeOS-Flux8-d0bb7511-GOP` uruchamiała Try przy 1024×768 i 1920×1080. Wejście w sprawdzanych sesjach podawano także przez wirtualne urządzenia VirtualBox.

| Scenariusz | Widoczny VirtualBox, PS/2 | Sprawdzony zakres i dowód |
| --- | --- | --- |
| Try → Login → Home | PASS | COM1 z obu sesji Live rejestruje profil Live, pierwszą klatkę Home i wejście na pulpit. Zrzuty Home potwierdzają obie rozdzielczości GOP. |
| Next Color i Previous Color | PASS | Zainstalowany profil Mono → Ember → Mono. COM1 rejestruje zmianę ustawienia oraz publikację zdarzenia. Zmianę palety Home, Docka i otwartego Settings obserwowano podczas próby w widocznym oknie. |
| Next Wallpaper | PASS | Grid, Minimal i Forged przy otwartym Home i 200%. COM1 rejestruje wybór wszystkich trzech presetów w zainstalowanej sesji, a zrzuty pokazują ich odrębne tła. |
| Reset Interface Profile | PASS | COM1 rejestruje powodzenie resetu. Zrzut `virtualbox-reset.png` pokazuje komunikat `PROFILE / DEFAULTS RESTORED`, czerwony akcent, Forged Steel, Modern i 100%. |
| Skala 100/125/150/200% | PASS w sprawdzonym zakresie | Cztery skale przy 1024×768, 1280×800 i 1920×1080. W obu sesjach Live zrzut i COM1 potwierdzają zmianę tapety po zmniejszeniu Settings przy 200%. W zmniejszonym Files przy 200% kliknięcie przy prawej krawędzi kafelka otworzyło katalog. Przy 1024×768 i 200% nagłówek skraca nazwę systemu wielokropkiem, zachowując czytelną wersję i status sesji. |
| Home, Dock, Files i Terminal | PASS | COM1 rejestruje uruchomienie Files z Home i Terminala z Docka, otwarcie katalogu, przewijanie, wyszukiwanie, utworzenie i zmianę nazwy katalogu. `ls`, `echo j`, `echo k`, historię i przewijanie sprawdzono w widocznej sesji przy 200%. Zrzut resetu zachowuje część wyjścia Terminala. |
| Zarządzanie oknami | PASS | COM1 rejestruje fokus, przesunięcie, zmianę rozmiaru, minimalizację, przywrócenie i zamknięcie. Przekazywanie klawiszy sprawdzono, wpisując filtr w Files i polecenie w Terminalu po zmianie fokusu. |
| Instalacja na pustym VDI | PASS | Instalację wykonał użytkownik w widocznej VM. COM1 rejestruje `installer_complete`, `installed_first_boot`, poprawne logowanie hasłem i pierwszą klatkę Home. |
| Restart bez ISO i odtworzenie ustawień | PASS | Start z VDI przy pustym napędzie DVD. COM1 po restarcie rejestruje logowanie, Home, Mono, Graphite Grid i 100%. Zrzut `qa-files-after-restart.png` pokazuje katalog `/flxok`. Odczyt kopii FAT32 potwierdził nową nazwę i zapis pięciu wartości wyglądu. |

Zachowane logi COM1 to `gop-1024.log`, `gop-1920.log`,
`virtualbox-before-cold-boot.log` oraz `virtualbox-after-cold-boot.log`.
Lokalny pakiet `build/flux8-candidate-d0bb7511/evidence/` zawiera te logi,
zrzuty gościa i raport przebiegu. Zrzuty i logi potwierdzają wskazane
zdarzenia oraz stany. Szczegóły interakcji, których zrzuty nie zachowały,
opierają się na obserwacji podczas pierwotnej próby w widocznym oknie.

### Osobny przebieg USB HID

USB-only sprawdzono w automatycznym QEMU/OVMF na tym samym SHA źródeł.
Zadanie CI wyłączyło PS/2 i uruchomiło klawiaturę USB oraz tablet USB.
Przeszły logowanie do Home, cykl życia okien, zmiana koloru, skali i
tapety, reset profilu oraz instalacja, dwa uruchomienia z dysku i test
odrzucenia uszkodzonego profilu. Widoczna próba VirtualBox z USB pozostaje
`NOT TESTED`. Nazwa znacznika `flux_files_usb_wheel_input` w COM1 z próby
PS/2 opisuje obsługę kółka w aplikacji, a protokół urządzenia potwierdza
osobny znacznik `ps2_mouse`. [Zadanie QEMU USB-only](https://github.com/KrzysioYT/KuroganeOS/actions/runs/42546543990/job/128932655450)

## Procedura dla kolejnego obrazu

Wybierz [obraz dla testowanej rewizji](FLUX8_TEST_CANDIDATE.md). Rozpakuj
ZIP z zachowaniem katalogów. Zapisz SHA źródeł, SHA-256
ISO, identyfikator przebiegu Actions, wersję VirtualBox i rozdzielczość.
Konfiguracja tworzy świeżą VM EFI64, VDI na SATA AHCI i PCnet NAT. COM1
trafia do pliku `kurogane-serial.log` w katalogu VM wskazanym przez skrypt.

```powershell
$artifact = 'D:\sciezka\do\rozpakowanego-artefaktu'
$manifest = Join-Path $artifact 'build\tests\flux-8-foundation'
$source = (Get-Content -LiteralPath (Join-Path $manifest 'source-sha.txt') -Raw).Trim()
$expected, $media = (Get-Content -LiteralPath (Join-Path $manifest 'iso-sha256.txt') -Raw).Trim() -split '\s+', 2
if ($media -notmatch '^dist/KuroganeOS-[a-zA-Z0-9.-]+-x86_64\.iso$') {
    throw 'Manifest wskazuje nieprawidłową ścieżkę obrazu.'
}
$iso = Join-Path $artifact $media
$actual = (Get-FileHash -LiteralPath $iso -Algorithm SHA256 -ErrorAction Stop).Hash
if ($source -notmatch '^[0-9a-f]{40}$' -or $actual -ne $expected) {
    throw 'Manifest źródeł albo suma ISO jest nieprawidłowa.'
}
$vm = 'KuroganeOS-Flux8-' + $source.Substring(0, 8) + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$disk = Join-Path $artifact ($vm + '.vdi')
.\scripts\create-virtualbox-vm.ps1 -Iso $iso -Name $vm -Disk $disk -Nic pcnet -Start
```

| Scenariusz | Operacja i oczekiwany wynik | Wynik |
| --- | --- | --- |
| Try → Login → Home | Wybierz Try i uruchom sesję Live. Pulpit i Home odpowiadają na wejście. | NOT TESTED |
| Next Color i Previous Color | Zostaw otwarte Home i Files. Kliknij oba przyciski i sprawdź natychmiastową zmianę kolorów Home, Docka i okien. W Try usługa oznacza zmianę jako `SESSION ONLY`. | NOT TESTED |
| Next Wallpaper | Z otwartym Home klikaj **Next Wallpaper** bez przewijania. Sprawdź Grid, Minimal i Forged po nazwie i po tle widocznym poza oknami. Powtórz przy 200%. | NOT TESTED |
| Reset Interface Profile | Ustaw inny kolor, tapetę, styl i skalę. Użyj resetu. Sprawdź czerwony akcent, Forged Steel, Modern, 100% oraz komunikat na początku formularza. | NOT TESTED |
| Skala 100/125/150/200% | Sprawdź nagłówek i trafianie w kontrolki w zwykłym i zmniejszonym oknie. Powtórz przy 1024×768, 1280×800 i 1920×1080, jeśli profil GOP je udostępnia. Zapisz niedostępne tryby. | NOT TESTED |
| Home, Dock, Files i Terminal | Uruchom Files z wyniku wyszukiwania oraz Terminal z Docka. W Files otwórz katalog, wróć do rodzica i przewiń. W Terminalu sprawdź wynik `ls`, `echo j` i `echo k`, historię i przewijanie przy 200%. | NOT TESTED |
| Zarządzanie oknami | Zmień fokus i sprawdź, które okno otrzymuje klawisze. Przesuń, zmień rozmiar, zminimalizuj, przywróć i zamknij okno. | NOT TESTED |
| Instalacja na pustym VDI | Uruchom Install na osobnym testowym VDI. Wyłącz VM, odłącz ISO, uruchom z dysku i zaloguj się testowym kontem. | NOT TESTED |
| Trwałe ustawienia po starcie bez ISO | Na zainstalowanym systemie zmień motyw, tapetę i skalę. W Files utwórz i zmień nazwę katalogu. Uruchom z VDI bez ISO i sprawdź dokładnie te same wartości i nazwę. | NOT TESTED |

Do każdej porażki dołącz krok, rozdzielczość, skalę, zrzut i odpowiedni fragment
COM1. Każdy wynik przypisz do konkretnej sumy ISO. Przebieg QEMU ani headless
VirtualBox nie uzupełnia kolumny ręcznej akceptacji. Jeżeli nie można wykonać
scenariusza w widocznym oknie, wpisz `NOT TESTED` lub `BLOCKED` z przyczyną.
