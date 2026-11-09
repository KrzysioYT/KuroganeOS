# Flux Modern 8 — ręczna macierz VirtualBox na Windows 11

Pobierz [kandydata dla aktualnego HEAD](FLUX8_TEST_CANDIDATE.md). Rozpakuj
ZIP z zachowaniem katalogów. Zapisz SHA źródeł, SHA-256
ISO, identyfikator przebiegu Actions, wersję VirtualBox i rozdzielczość.
Konfiguracja tworzy świeżą VM EFI64, VDI na SATA AHCI i PCnet NAT. COM1
trafia do pliku `kurogane-serial.log` w katalogu VM wskazanym przez skrypt.

```powershell
$artifact = 'D:\sciezka\do\rozpakowanego-artefaktu'
$iso = Join-Path $artifact 'dist\KuroganeOS-7.0.0-dev-x86_64.iso'
$manifest = Join-Path $artifact 'build\tests\flux-8-foundation'
$source = (Get-Content -LiteralPath (Join-Path $manifest 'source-sha.txt') -Raw).Trim()
$expected = ((Get-Content -LiteralPath (Join-Path $manifest 'iso-sha256.txt') -Raw).Trim() -split '\s+')[0]
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
