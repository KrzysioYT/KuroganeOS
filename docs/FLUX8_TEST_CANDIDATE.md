# Flux 8 — kandydat do ręcznych testów

To obraz testowy etapu 8.0.0. W systemie nadal widnieje **7.0.0-dev**.
Źródło: **29250ec8bdfa85db8f1908066627f0cf7c29db24**,
[PR #58](https://github.com/KrzysioYT/KuroganeOS/pull/58) (Draft).

- [Pobierz ZIP z dokładnym ISO](https://github.com/KrzysioYT/KuroganeOS/actions/runs/42389245464/artifacts/13581037201) — zaloguj się na GitHubie i rozpakuj.
- [Kwalifikacja Flux USB-only w QEMU](https://github.com/KrzysioYT/KuroganeOS/actions/runs/42389245464) oraz [kwalifikacja UEFI ISO](https://github.com/KrzysioYT/KuroganeOS/actions/runs/42389250304): **PASS**.
- [Serial, manifesty i zrzuty QEMU](https://github.com/KrzysioYT/KuroganeOS/actions/runs/42389245464/artifacts/13580553997). Ręczny VirtualBox: **NOT TESTED**.

## Start na Windows

Użyj VirtualBox na hoście x86-64. Nie trzeba budować systemu ani instalować
toolchainu. Uruchom w PowerShell z katalogu repozytorium z gałęzi
`integration/flux8-reconciliation` i wpisz ścieżkę do rozpakowanego pliku ISO:

```powershell
$iso = 'C:\sciezka\do\KuroganeOS-7.0.0-dev-x86_64.iso'
$expected = '7ef5738be0cc67ceb04085482b666b010cefd3c41b6b42fa8bc2f3f870c4fcd9'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $iso -ErrorAction Stop).Hash -ne $expected) {
    throw 'Suma ISO nie zgadza się z kandydatem 29250ec.'
}
$vm = 'KuroganeOS-Flux8-29250ec-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\create-virtualbox-vm.ps1 -Iso $iso -Name $vm -Nic pcnet -Start
```

## Kolejność prób

1. **Try:** przejdź z Login do Home. Otwórz Files i Terminal myszą,
   wpisz polecenie, sprawdź przewijanie, zmianę fokusu, przesunięcie,
   resize, minimalizację i przywracanie okna.
2. **Install:** zrestartuj z ISO i instaluj wyłącznie na nowym VDI utworzonym
   przez powyższą komendę. Po zakończeniu wyłącz VM i odłącz ISO w
   **Ustawienia → Pamięć masowa**. Uruchom z dysku, sprawdź odrzucenie
   złego hasła i wejście do Home z poprawnym hasłem testowym.
3. **Skala:** sprawdź 100/125/150/200% w Settings. Przy 200% zmniejsz okno,
   przejdź po kontrolkach klawiszem Tab i sprawdź ich widoczność. Zapisz
   motyw i skalę, uruchom ponownie, sprawdź odtworzenie obu ustawień.
4. Zapisz PASS/FAIL dla każdej próby, wersję VirtualBox i pierwszy krok,
   który nie zadziałał. Przy problemie zachowaj zrzut i plik
   `$HOME\VirtualBox VMs\<nazwa VM>\kurogane-serial.log`.
   Nie dołączaj hasła. Z tego samego PowerShell diagnostykę uruchomisz
   przez `.\scripts\check-flux8-ui.ps1 -Name $vm`.

Wynik QEMU nie zastępuje tej próby. Wersja nie jest jeszcze wydaniem 8.0.0.
