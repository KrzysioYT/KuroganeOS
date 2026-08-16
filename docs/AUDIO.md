# KuroganeOS Audio

## Status 3.3.1-dev

Referencyjnym kontrolerem audio dla VirtualBox jest **Intel ICH AC'97**.
KuroganeOS 3.3.1-dev wprowadza własny, minimalny kernelowy sterownik PCM output
dla emulowanego kontrolera Intel `8086:2415`.

Aktualny działający fundament to:

```text
kernel
 -> PCI 8086:2415
 -> AC'97 mixer + bus master
 -> DMA32 BDL / PCM buffer
 -> PCM S16LE stereo 48 kHz
 -> VirtualBox host audio backend
```

Docelowy tor aplikacji będzie wyglądał tak:

```text
application
 -> public audio SDK/API
 -> validated syscall/audio service
 -> AC'97 PCM output
 -> VirtualBox host audio backend
```

> **Ważne:** 3.3.1-dev dostarcza **kernelowy backend AC'97**, ale nie deklaruje
> jeszcze stabilnego publicznego Ring-3 playback API. Zwykła aplikacja nie może
> w tej wersji bezpośrednio wysłać PCM przez oficjalny SDK. To jest następna
> warstwa nad gotowym sterownikiem sprzętowym.

3.3.1 skupia się na bazowym output PCM. Capture/microphone, mikser per-process,
format conversion i zaawansowany scheduler audio pozostają dalszą pracą.

## VirtualBox

Ustaw:

```text
Enable Audio: ON
Audio Controller: Intel AC'97
Audio Output: ON
```

Host Audio Driver zostaw jako `Default`, chyba że debugujesz problem hosta.

## Format referencyjny

Sterownik bazowy przyjmuje:

```text
PCM signed 16-bit little-endian
stereo
48 kHz
interleaved L,R,L,R...
```

Jeden blok jest kopiowany do pamięci należącej do kernela/DMA32 przed
uruchomieniem bus-master engine. Aplikacja Ring-3 nie będzie dostawać surowego
adresu DMA ani prawa do bezpośredniego programowania BAR-ów kontrolera.

## Dlaczego AC'97

- VirtualBox potrafi emulować Intel AC'97;
- urządzenie jest proste do testowania w VM;
- używa klasycznego PCI + I/O + bus-master DMA;
- daje KuroganeOS realny pierwszy backend audio bez zależności od Guest
  Additions.

## Diagnostyka

Jeżeli kontroler nie inicjalizuje się:

1. sprawdź `Enable Audio`;
2. sprawdź `Intel AC'97`;
3. sprawdź `Audio Output`;
4. sprawdź serial log pod kątem `AC97`;
5. upewnij się, że host VirtualBox sam ma działający backend audio;
6. nie wybieraj Intel HD Audio, jeśli testujesz backend AC'97;
7. sprawdź, czy PCI wykrywa `8086:2415`.

Log gotowego backendu powinien zawierać informację z modułu `AC97`, że PCM
output jest gotowy.

## Dla programistów

Nie używaj bezpośrednio portów AC'97 z aplikacji Ring-3. Sterownik sprzętowy
należy do kernela.

Dopóki stabilne publiczne audio API nie zostanie dodane, aplikacje powinny
traktować playback audio jako capability jeszcze niedostępne przez publiczny
SDK, zamiast importować prywatne nagłówki `kernel/drivers/audio/*`.

Zobacz:

- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)
- [`DEVELOPERS/APP_DEVELOPMENT.md`](DEVELOPERS/APP_DEVELOPMENT.md)
