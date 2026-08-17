# KuroganeOS Audio

## Status 3.3.3-dev

Referencyjnym kontrolerem audio dla VirtualBox jest **Intel ICH AC'97**.
KuroganeOS ma własny minimalny kernelowy sterownik PCM output dla emulowanego
kontrolera Intel `8086:2415` oraz bounded publiczny Ring-3 playback contract.

Aktualny tor wygląda tak:

```text
application
 -> sdk/include/kurogane/audio.h
 -> validated Ring-3 syscall
 -> per-PID exclusive playback ownership
 -> kernel copy into DMA32 PCM page
 -> AC'97 BDL / bus-master PCM output
 -> VirtualBox host audio backend
```

## Publiczny Ring-3 playback

SDK udostępnia:

```c
ku_audio_get_state(...)
ku_audio_set(...)
ku_audio_play_pcm16_stereo(...)
ku_audio_poll()
ku_audio_stop()
```

`ku_audio_play_pcm16_stereo()` przyjmuje maksymalnie 1024 klatki jednego
bufora. Akceptowany bufor jest kopiowany do pamięci należącej do kernela zanim
syscall wróci, więc sterownik nie przechowuje wskaźnika do pamięci aplikacji.

Format publicznego transportu 3.3.3 jest celowo stały:

```text
PCM signed 16-bit little-endian
stereo
48 kHz
interleaved L,R,L,R...
maximum 1024 frames per submitted buffer
```

Po przyjęciu bufora wywołanie zwraca `KU_STATUS_OK`. `ku_audio_poll()` zwraca
`KU_STATUS_WOULD_BLOCK`, dopóki bufor jest aktywny, i `KU_STATUS_OK` po jego
zakończeniu. `ku_audio_stop()` zatrzymuje playback należący do bieżącego
procesu.

Aktualny model jest **single-owner, per PID**. Jeden proces może posiadać
aktywny playback na referencyjnym urządzeniu. Inny proces nie może zatrzymać ani
pollować cudzego odtwarzania. Cleanup procesu automatycznie zatrzymuje DMA,
jeżeli proces nadal jest właścicielem playbacku.

To jest bezpieczny fundament aplikacyjnego audio, ale jeszcze nie pełny mixer
ani wielostrumieniowy serwis. Per-process stream handles, miksowanie wielu
aplikacji, resampling, capture/microphone i Intel HDA pozostają dalszą pracą.

## VirtualBox

Ustaw:

```text
Enable Audio: ON
Audio Controller: Intel AC'97
Audio Output: ON
```

Host Audio Driver zostaw jako `Default`, chyba że debugujesz problem hosta.

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

Nie używaj bezpośrednio portów AC'97 ani prywatnych nagłówków sterownika z
aplikacji Ring-3. Publicznym kontraktem jest wyłącznie `kurogane/audio.h`.
Aplikacja nigdy nie otrzymuje surowego adresu DMA ani prawa do programowania
BAR-ów kontrolera.

Zobacz:

- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)
- [`DEVELOPERS/APP_DEVELOPMENT.md`](DEVELOPERS/APP_DEVELOPMENT.md)
