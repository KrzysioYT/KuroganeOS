# KuroganeOS Graphics Compatibility — 3.3.3-dev

## Stan faktyczny

KuroganeOS 3.3.3-dev rozpoznaje kontroler klasy **PCI Display (0x03)** i
rejestruje go w centralnym Driver Managerze jako `redflux-display`.

Warstwa graficzna rozróżnia:

```text
PCI display adapter detected
UEFI GOP scanout available
Red Flux software compositor available
hardware accelerated 3D available
```

Ostatnia flaga pozostaje **false**, dopóki nie istnieje prawdziwy sterownik
command-submission dla konkretnego GPU. Obecność software Direct3D compatibility
foundation nie jest raportowana jako sprzętowa akceleracja.

## Co działa

- UEFI GOP framebuffer;
- wykrywanie PCI display adaptera;
- driver-manager binding dla urządzenia display;
- software backbuffer;
- damage-style GOP scanout;
- clipping i 2D primitives;
- Red Flux WindowManager/compositor;
- Ring-3 GUI ABI;
- publiczny bounded XRGB8888 software surface runtime;
- integer `clear`, clipped `rect`, `line` i filled-triangle rasterizer bez FPU/SSE;
- wspólny software device foundation dla frontendów D3D9, D3D11 i D3D12;
- bounded D3D12-style command list (`CLEAR`, `TRIANGLE`);
- per-frame draw budget i jawne `WOULD_BLOCK` po przekroczeniu limitu pracy;
- host regression test z canary przed/za framebufferem, clippingiem i błędnymi
  rozmiarami surface;
- live `GPU/GFX` activity oparta o rzeczywiste submissiony compositora.

> `GPU/GFX %` w aplikacji Performance oznacza aktywność obecnego stosu
> GOP/software-compositor. Nie jest to licznik wykorzystania rdzeni fizycznego
> GPU.

## Direct3D 9 / 11 / 12 — obecny poziom

W SDK istnieje teraz **source-level Kurogane Direct3D compatibility foundation**.
Nie jest to jeszcze Microsoft Windows COM ABI i nie oznacza, że dowolna gra
Windows korzystająca z `d3d9.dll`, `d3d11.dll` albo `d3d12.dll` uruchomi się bez
warstwy Win32/PE/COM.

Działający wspólny backend zapewnia obecnie:

```text
D3D9  -> software device -> BeginScene/EndScene -> clear/triangle -> frame finalize
D3D11 -> software device -> clear/triangle -> frame finalize
D3D12 -> bounded command list -> clear/triangle -> execute -> frame finalize
```

Każda ścieżka korzysta z tego samego sprawdzanego `ku_gfx_surface`. Operacje są
clippowane, rozmiary i stride są overflow-checkowane, a ilość pracy w ramce jest
ograniczona. Nieobsługiwana semantyka ma zwracać błąd zamiast fałszywego
sukcesu.

### Czego jeszcze nie nazywamy pełnym DirectX

- Windows COM ABI / Win32 object model;
- natywny window/swapchain present dla software surface;
- GPU resource handles;
- vertex/index buffers;
- textures i samplery;
- render targets i depth/stencil;
- blend/raster/depth state;
- shaders / shader IR / translacja HLSL bytecode;
- fences i synchronizacja GPU;
- D3D11 context semantics;
- pełne D3D12 queues/lists/barriers/descriptors;
- sprzętowy command-submission backend.

KuroganeOS nie zwraca fałszywego sukcesu z funkcji w rodzaju
`D3D12CreateDevice()` bez wymaganej semantyki.

## Docelowy model

```text
D3D9 compatibility frontend  ---\
D3D11 compatibility frontend ----> Kurogane Graphics Runtime
D3D12 compatibility frontend ---/            |
                                              +-- bounded software 3D backend
                                              +-- accelerated GPU backend
```

### Etap 1 — native Kurogane Graphics Runtime

- kernel/runtime surface/image handles;
- buffers/textures;
- bounded per-window command buffers;
- viewport/scissor;
- raster/depth/blend states;
- swap/present;
- synchronization handles.

Publiczny software surface i pierwszy integer rasterizer są już wykonane; kolejnym
krokiem jest natywny command-buffer/present do WindowManagera bez kopiowania
pełnego framebufferu przy każdej klatce.

### Etap 2 — software 3D

- vertex/index buffers;
- depth buffer;
- texture sampling;
- prosty shader/intermediate representation;
- resource lifetime i limity pamięci per process/device.

### Etap 3 — hardware backend

Pierwszy backend powinien celować w konkretny, dobrze udokumentowany adapter
wirtualny/fizyczny zamiast próbować jednocześnie obsłużyć wszystkie GPU.
Dopiero po realnym command submission `accelerated_3d` może zostać ustawione na
`true`.

### Etap 4 — pełniejsze D3D compatibility

Warstwa D3D mapuje feature levels i zachowanie API na Kurogane Graphics.
Nieobsługiwane funkcje mają zwracać `not supported`, a nie renderować
niepoprawny wynik.

## Zasady stabilności i obciążenia

Graphics Runtime jest projektowany tak, aby pojedyncza aplikacja nie mogła
przez błędne wymiary lub gigantyczny command buffer zablokować całej sesji:

- maksymalne wymiary surface są jawnie ograniczone;
- pixel capacity i `stride * height` są sprawdzane przed zapisem;
- rasterizer clipuje do surface;
- line rasterizer ma twardy work budget;
- D3D device ma limit draw calls na ramkę;
- D3D12 listy mają stałą maksymalną liczbę komend;
- przyszły WindowManager present zachowa bounded-copy/bounded-command model;
- sprzętowy backend nie będzie włączany bez reset/timeout/error recovery.

## Dla programistów

Natywne aplikacje mogą używać:

```c
#include <kurogane/graphics.h>
#include <kurogane/direct3d.h>
```

Binaria Windows i biblioteki Windows SDK nie są jeszcze natywnym ABI KuroganeOS.

Zobacz:

- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)
- [`DEVELOPERS/GUI_APPLICATIONS.md`](DEVELOPERS/GUI_APPLICATIONS.md)
- [`releases/3.3.3-dev.md`](releases/3.3.3-dev.md)
