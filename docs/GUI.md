# Graphics, input and Forged Steel Desktop

KuroganeOS `3.3.3-dev` używa UEFI GOP oraz własnego software compositora.
Bieżąca gałąź rozwija warstwę wizualną Forged Steel/KuroganeOS 5, ale publiczny
numer wersji nie jest jeszcze podniesiony do 5.0.0.

## Boot i model sesji

Normalny Foundation flow:

```text
UEFI
 -> BOOTX64.EFI
 -> Forged Steel boot splash
 -> kernel + GPT Kurogane Root
 -> WindowManager
 -> /system/init jako PID 1
 -> /gui/login
 -> KUROGANE // SECURE ACCESS
 -> /gui/launcher (Blade Launcher)
 -> aplikacje Ring-3 uruchamiane na żądanie
```

Safe Mode pozostaje ścieżką diagnostyczną bez normalnej sesji desktopowej.
Serial pozostaje źródłem pełnych logów, nawet kiedy GOP jest już własnością GUI.

## Secure Access / session gate

`/gui/login` jest specjalnym oknem bez zwykłego chrome. Jego techniczny tytuł
pozostaje `KUROGANE LOGIN`, ponieważ WindowManager używa go obecnie do
identyfikacji roli session gate. Widoczny branding jest niezależny od tego
tytułu.

PID1 nadzoruje:

```text
Secure Access -> Blade Launcher -> Secure Access
```

Live profile może wejść przez Enter/CTA. Profil instalowany może używać danych z
`/etc/user.cfg` i lokalnego hasha credentiali.

## WindowManager

Aktualny WindowManager obsługuje:

- generation-checked window IDs;
- focus i z-order;
- move/drag;
- resize;
- minimize/maximize/restore/close;
- Alt+Tab i Alt+F4;
- software cursor z własnymi kształtami;
- workspace/chrome geometry;
- Dock/Pulse Ribbon i desktop shortcuts;
- Ring-3 ownership względem session root;
- adapter global -> window-local dla pointer input;
- native UI ABI v2 surfaces.

Blade Launcher jest rootem graficznego drzewa sesji. Aplikacje uruchomione przez
Blade są jego potomkami i mogą tworzyć zwykłe surfaces. Stare anonimowe Ring-0
`/gui/*` requesty pozostają compatibility no-op i nie są prawdziwą ścieżką
launchera.

## Forged Steel visual language

Tokeny:

```text
Obsidian      #090E0E
Forged Steel  #171C22
Ash           #A8AFB8
Crimson       #E62932
Hot Edge      #FF4A45
```

Kierunek UI:

- czarne/obsydianowe tło;
- warstwowe stalowe surfaces;
- chamfered/angular geometry;
- Crimson do struktury i stanu;
- Hot Edge tylko dla aktywnego/focus CTA;
- kondensowana techniczna typografia UI;
- Mono dla Kurosh/code;
- własny Kurogane branding i icon pack;
- bez kopiowania layoutu Windows/macOS/GNOME/KDE.

**BUILT IN STEEL. REFINED IN FIRE.**

## Native UI ABI v2

Bieżący `ku_ui_surface` transportuje do kernela prawdziwe widget records, a nie
tylko serializowane linie tekstu. Typy:

```text
panel
label
button
input
list item
progress
separator
```

Każdy widget zachowuje m.in.:

```text
id
parent_id
type
flags
value / maximum
icon_id
text
```

`kui_scene` / `kui_flow` w userspace budują sceny, a kernelowy Forged renderer
rysuje native surface. Legacy `ku_ui_frame` nadal istnieje dla kompatybilności,
ale nie jest już jedynym transportem GUI.

## Input i responsywność

PS/2 keyboard/mouse oraz wspierane HID źródła trafiają do wspólnej kolejki
InputManagera. Kolejne ruchy myszy są koaleskowane, aby nie odtwarzać starych
pozycji kursora po przeciążeniu CPU.

Desktop nie renderuje pełnej klatki po każdym pojedynczym evencie. Input może
oznaczyć WindowManager jako dirty, a compositor składa końcowy stan raz na tick.
To jest część trwającej optymalizacji latency.

## Software compositor

Wspierane tryby do 1600x1200 korzystają z software backbufferu. Obecny model:

1. cursor jest zdejmowany z widocznego obrazu;
2. WindowManager składa desktop/windows do RAM backbufferu;
3. zmienione fragmenty są prezentowane do GOP;
4. software cursor jest nakładany po prezentacji.

Nie ma jeszcze produkcyjnego GPU accelerated compositora. GOP/GFX activity w
Performance oznacza pracę software renderera/scanoutu, nie fizyczne użycie GPU.

Najbliższy performance work obejmuje dalsze ograniczenie pełnego frame scan i
bardziej precyzyjne dirty rectangles/per-window damage.

## Fonty

Kernel ma osobne logiczne faces:

```text
Ui
Mono
Display
```

Nie należy tego mylić z pełnym TTF/OpenType stackiem. Obecny system fontów jest
własnym bitmapowym/rasterowym subsystemem; skalowalne font assets/rasterizer są
nadal pracą przyszłą.

## Aplikacje bieżącej sesji

```text
Blade Launcher
Kurosh
Vault
Anvil
Forge Control
Pulse
Kurogane Web
Performance
System Monitor
About
```

Vault korzysta z prawdziwego VFS/readdir i potrafi otwierać katalogi, podglądać
pliki tekstowe i uruchamiać ELF. Forge Control ma działające kontrolki
Network/Appearance/Audio/System. Anvil ma realny backend pakietów i klikalny
refresh/install. Web ma HTTP/HTTPS, redirecty, historię i aktywację linków.

## Pointer hit testing

WindowManager dostaje globalne współrzędne. `ui_window_adapter` zapamiętuje
aktualny content rect i konwertuje pointer event do window-local przed callbackiem
Ring-3. Dzięki temu kliknięcia zachowują geometrię po move/resize/maximize bez
app-specific offsetów.

## Legacy markery testowe

Część serial markerów nadal zawiera historyczne `red_flux_*`, ponieważ starsze
testy/CI mogą ich używać. Są to identyfikatory kompatybilności, nie bieżąca
nazwa produktu. Nowe ścieżki powinny równolegle emitować markery `kurogane5_*`
lub `forged_steel_*`.

## Znane ograniczenia

- software compositor/GOP bez pełnej akceleracji GPU;
- brak multi-monitor;
- bitmapowy font subsystem zamiast pełnego TTF/OpenType;
- widget layout jest nadal głównie flow-based, nie pełnym constraint/flex/grid;
- brak kompletnego clipboard/IME/Unicode text stack;
- brak pełnego accessibility model;
- brak produkcyjnego compositor damage graph;
- część internal roles nadal zależy od stabilnych tytułów okien;
- publiczna kompatybilność D3D nie oznacza jeszcze pełnego sprzętowego DirectX.

Aktualny plan GUI: [roadmap/KUROGANEOS_5_GUI.md](roadmap/KUROGANEOS_5_GUI.md).
