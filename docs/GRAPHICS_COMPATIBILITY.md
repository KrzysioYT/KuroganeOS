# KuroganeOS Graphics Compatibility — 3.3.3-dev

## Stan faktyczny

KuroganeOS 3.3.3-dev rozpoznaje teraz kontroler klasy **PCI Display (0x03)** i
rejestruje go w centralnym Driver Managerze jako `redflux-display`.

Warstwa graficzna potrafi rozróżnić:

```text
PCI display adapter detected
UEFI GOP scanout available
Red Flux software compositor available
hardware accelerated 3D available
```

W 3.3.3 ostatnia flaga pozostaje **false**, dopóki nie istnieje prawdziwy
sterownik command-submission dla konkretnego GPU.

## Co działa

- UEFI GOP framebuffer;
- wykrywanie PCI display adaptera;
- driver-manager binding dla urządzenia display;
- software backbuffer;
- damage-style GOP scanout;
- clipping i 2D primitives;
- Red Flux WindowManager/compositor;
- Ring-3 GUI ABI;
- live `GPU/GFX` activity oparta o rzeczywiste submissiony compositora.

> `GPU/GFX %` w aplikacji Performance oznacza aktywność obecnego stosu
> GOP/software-compositor. Nie jest to licznik wykorzystania rdzeni fizycznego
> GPU i interfejs użytkownika mówi o tym wprost.

## DirectX 9 / 11 / 12

Direct3D 9, 11 i 12 **nie są jeszcze oznaczone jako zgodne**. DirectX jest
rodziną API Windows, a pełna kompatybilność wymaga znacznie więcej niż
framebuffer lub zestaw funkcji o podobnych nazwach.

Do prawdziwej implementacji potrzebne są między innymi:

- adapter/device abstraction;
- buffers, textures i resource lifetime;
- render targets i depth/stencil;
- swap/present;
- vertex/index input;
- shader runtime i odpowiednia translacja shaderów;
- command submission;
- synchronization/fences;
- blend/raster/depth states;
- D3D11 context semantics;
- D3D12 command queues/lists, barriers i descriptors;
- sprzętowy backend GPU albo kompletny software rasterizer.

KuroganeOS nie będzie zwracał fałszywego sukcesu z funkcji w rodzaju
`D3D12CreateDevice()` bez implementacji wymaganej semantyki.

## Docelowy model

```text
D3D9 compatibility frontend  ---\
D3D11 compatibility frontend ----> Kurogane Graphics Runtime
D3D12 compatibility frontend ---/            |
                                              +-- software 3D backend
                                              +-- accelerated GPU backend
```

### Etap 1 — Kurogane Graphics Runtime

- surface/image handles;
- buffers/textures;
- command buffers;
- viewport/scissor;
- raster/depth/blend states;
- swap/present;
- synchronization handles.

### Etap 2 — software 3D

- vertex/index buffers;
- triangles;
- depth buffer;
- texture sampling;
- prosty shader/intermediate representation.

### Etap 3 — hardware backend

Pierwszy backend powinien celować w konkretny, dobrze udokumentowany adapter
wirtualny/fizyczny zamiast próbować jednocześnie obsłużyć wszystkie GPU.
Dopiero po realnym command submission `accelerated_3d` może zostać ustawione na
`true`.

### Etap 4 — D3D compatibility

Warstwa D3D mapuje feature levels i zachowanie API na Kurogane Graphics.
Nieobsługiwane funkcje mają zwracać `not supported`, a nie renderować
niepoprawny wynik.

## Dla programistów

Obecnie aplikacje powinny używać natywnego GUI KuroganeOS. Binaria Windows i
biblioteki Windows SDK nie są natywnym ABI KuroganeOS.

Zobacz:

- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)
- [`DEVELOPERS/GUI_APPLICATIONS.md`](DEVELOPERS/GUI_APPLICATIONS.md)
- [`releases/3.3.3-dev.md`](releases/3.3.3-dev.md)
