# Flux Modern 8 — ręczna macierz VirtualBox na Windows 11

Zapisz pełny SHA źródeł, SHA-256 ISO, wersję VirtualBox, rozdzielczość i skalę.
Użyj świeżej VM EFI64 i pustego testowego VDI na SATA AHCI. Porównaj sumę ISO
z manifestem artefaktu. Obraz uruchom z widocznym oknem VM.

```powershell
$iso = 'D:\sciezka\KuroganeOS-7.0.0-dev-x86_64.iso'
Get-FileHash -LiteralPath $iso -Algorithm SHA256
$vm = 'KuroganeOS-Flux8-QA-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
.\scripts\create-virtualbox-vm.ps1 -Iso $iso -Name $vm -Nic pcnet -Start
```

| Scenariusz | Operacja i oczekiwany wynik | Wynik |
| --- | --- | --- |
| Try i Settings | Wejdź do Home. Otwórz Files i Terminal, potem Settings. Kliknij **Next Color** oraz **Previous Color**. Zmieniają się kolory Docka, Home i już otwartych okien. Komunikat brzmi `SESSION ONLY`. | Do uzupełnienia |
| Tapeta i reset | Wybierz kolejno Grid, Minimal i Forged. Potwierdź zmianę tła poza oknami. Zmień styl i skalę. Przejdź klawiszem Tab do resetu, użyj Enter i sprawdź rzeczywisty powrót wyglądu domyślnego. | Do uzupełnienia |
| Skala i geometria | Dla 100/125/150/200% sprawdź nagłówek, pozycję wskaźnika i działanie przycisków w zwykłym i zmniejszonym oknie. Powtórz przy 1024×768 i 1280×800 oraz przy 1920×1080, jeśli profil GOP je udostępnia. Zapisz niedostępne tryby. | Do uzupełnienia |
| Dock i okna | Uruchom aplikacje z Docka myszą, przełącz aktywne okno, przesuń je, zmień rozmiar, zminimalizuj, przywróć i zamknij. Sprawdź, które okno faktycznie otrzymuje klawisze. | Do uzupełnienia |
| Home i Terminal | Wyszukaj Files z Home i uruchom wynik. W Terminalu wykonaj `ls`, `echo j` i `echo k`. Sprawdź wyniki, przewijanie, historię strzałkami oraz widoczność pola polecenia przy 200%. | Do uzupełnienia |
| Files w Try | Otwórz katalog i wróć do rodzica. Kółko nad kafelkami zmienia stronę, a poza nimi przewija panel. Operacje zmiany nazwy i utworzenia katalogu na pakiecie Live są oznaczone jako tylko do odczytu. | Do uzupełnienia |
| Instalacja i odtwarzanie | Zainstaluj na pustym testowym VDI. Odłącz ISO. Zmień motyw, tapetę i skalę. W Files utwórz katalog, zmień jego nazwę i uruchom ponownie z VDI. Sprawdź dokładnie te same ustawienia i nową nazwę katalogu. | Do uzupełnienia |

Do każdej porażki dołącz krok, rozdzielczość, skalę, zrzut i odpowiedni fragment
COM1. Każdy wynik przypisz do konkretnej sumy ISO. Przebieg QEMU ani headless
VirtualBox nie uzupełnia kolumny ręcznej akceptacji. Jeżeli nie można wykonać
scenariusza w widocznym oknie, wpisz `NOT TESTED` lub `BLOCKED` z przyczyną.
