# KuroganeOS Audio

## Status 3.3.1-dev

Referencyjnym kontrolerem audio dla VirtualBox jest **Intel ICH AC'97**.
KuroganeOS 3.3.1-dev wprowadza własny, minimalny sterownik PCM output dla
emulowanego kontrolera Intel `8086:2415`.

Docelowy tor:

```text
application
 -> public audio API
 -> kernel audio service
 -> AC'97 PCM output
 -> VirtualBox host audio backend
```

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

Dla innych formatów aplikacja lub przyszły userspace audio service będzie
wykonywać konwersję.

## Dlaczego AC'97

- VirtualBox potrafi emulować Intel AC'97;
- urządzenie jest proste do testowania w VM;
- używa klasycznego PCI + I/O + bus-master DMA;
- daje KuroganeOS realny pierwszy backend audio bez zależności od Guest
  Additions.

## Diagnostyka

Jeżeli nie ma dźwięku:

1. sprawdź `Enable Audio`;
2. sprawdź `Intel AC'97`;
3. sprawdź `Audio Output`;
4. sprawdź serial log pod kątem `AC97`;
5. upewnij się, że host VirtualBox sam ma działający backend audio;
6. nie wybieraj Intel HD Audio, jeśli testujesz wyłącznie backend AC'97.

## Dla programistów

Nie używaj bezpośrednio portów AC'97 z aplikacji Ring-3. Sterownik sprzętowy
należy do kernela. Aplikacje powinny korzystać z publicznego SDK.

Zobacz:

- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)
- [`DEVELOPERS/APP_DEVELOPMENT.md`](DEVELOPERS/APP_DEVELOPMENT.md)
