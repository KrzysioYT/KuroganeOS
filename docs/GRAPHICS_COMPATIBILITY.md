# KuroganeOS Graphics Compatibility

## Ważne: DirectX a KuroganeOS

Direct3D 11 i Direct3D 12 są API środowiska Windows. KuroganeOS nie jest
Windowsem i nie może uczciwie deklarować pełnego DirectX 11/12 tylko dlatego,
że ma framebuffer albo podobne nazwy funkcji.

Dlatego 3.3.1-dev rozdziela trzy rzeczy:

1. **Kurogane native graphics API** — własny publiczny kontrakt dla aplikacji;
2. **software renderer / framebuffer backend** — obecny fundament;
3. **future Direct3D compatibility layer** — warstwa zgodności/translacji,
   która ma mapować wybrany podzbiór zachowania D3D na Kurogane Graphics.

## Stan 3.3.1-dev

Dostępne:

- UEFI GOP framebuffer;
- software backbuffer;
- 2D primitives/text/UI;
- damage-style scanout;
- WindowManager/compositor foundation;
- publiczny Ring-3 UI ABI.

Nie jest jeszcze dostępne:

- pełne DXGI;
- `d3d11.dll` / `d3d12.dll` zgodne binarnie z Windows;
- HLSL compiler/runtime;
- shader model 5/6;
- GPU command queues;
- descriptor heaps;
- resource barriers;
- pełne D3D feature levels;
- sterownik GPU klasy WDDM.

## Dlaczego nie oznaczamy tego jako "DirectX 12 supported"

Direct3D 12 wymaga znacznie więcej niż rysowania pikseli: urządzeń, adapterów,
zasobów GPU, kolejek poleceń, synchronizacji CPU/GPU, shaderów i zarządzania
stanem zasobów. Direct3D 11 ma dodatkowo własny runtime i model context/device.

Fałszywe oznaczenie `DX12 READY` utrudniłoby rozwój, bo programista dostałby
nagłówki, które kompilują program, ale nie zapewniają semantyki API.

## Plan kompatybilności

Docelowy model:

```text
Direct3D-like compatibility API
        |
        +-- D3D9/10/11 compatibility frontend
        |
        +-- D3D12 compatibility frontend
        |
        v
Kurogane Graphics Runtime
        |
        +-- software backend
        +-- future accelerated GPU backend
```

### Etap A — native Kurogane Graphics

- surface/image objects;
- swap/present;
- buffers/textures;
- command buffer;
- basic rasterization;
- clipping/scissor;
- blend states;
- resource lifetime handles.

### Etap B — software 3D

- vertex buffers;
- indexed triangles;
- viewport/scissor;
- depth buffer;
- texture sampling;
- fixed/simple programmable shading subset.

### Etap C — Direct3D compatibility

- feature mapping;
- error/result mapping;
- adapter/device abstraction;
- D3D11-style immediate/deferred command translation;
- D3D12-style explicit resource/command model where backend allows it.

## Feature-level policy

KuroganeOS nie będzie zgłaszać feature level, którego realnie nie implementuje.
Compatibility layer ma zwracać `not supported` zamiast udawać poprawne
renderowanie.

## Dla programistów

Dzisiaj pisz aplikacje pod natywne API KuroganeOS. Nie linkuj aplikacji
KuroganeOS bezpośrednio z bibliotekami Windows SDK.

Zobacz:

- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)
- [`DEVELOPERS/GUI_APPLICATIONS.md`](DEVELOPERS/GUI_APPLICATIONS.md)
