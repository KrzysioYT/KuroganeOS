# Graphics, input and Kurogane Red Flux Desktop

KuroganeOS 3.2 używa UEFI GOP framebuffer i własnego software renderera. Nie ma
jeszcze akcelerowanego sterownika GPU. PS/2 keyboard/mouse oraz obsługiwane HID
źródła zasilają wspólny InputManager.

## Boot i model sesji

Normalny flow 3.2:

```text
UEFI
 -> Red Flux desktop boot (default)
 -> graphical boot splash
 -> kernel + persistent FAT32 root
 -> WindowManager
 -> /system/init PID 1
 -> /gui/login
 -> /gui/launcher (Red Flux Home)
 -> aplikacje Ring 3 uruchamiane na żądanie
```

UEFI zachowuje awaryjne wejścia `S`/`F8` do Safe Mode i `X` do Diagnostics.
Normalny start nie wymaga już klawisza `D`.

Podczas startu kernel utrzymuje pełne logi na serialu, a GOP pokazuje boot
splash. Progres odpowiada faktycznym checkpointom: paging, Ring 3, filesystem,
persistent storage, preemption/input i start PID1. Safe Mode, Diagnostics,
Installer i fatalny boot failure przywracają framebufferową service console.

## Login / session gate

`/gui/login` jest specjalną userspace surface bez zwykłego window chrome.
Enter lub kliknięcie uruchamia Red Flux Home. Login jest obecnie bramą lokalnej
sesji deweloperskiej — 3.2 celowo nie udaje hasła bez account/credential
service.

PID1 nadzoruje:

```text
Login -> Home -> Login
```

Nowy Login czyści surfaces poprzedniej sesji i prosi process manager o
zakończenie ich właścicieli. Zakończone orphan zombie slots są odzyskiwane przy
kolejnych spawnach.

## Red Flux Window Core

WindowManager udostępnia:

- generation-checked window IDs;
- focus i z-order;
- header drag;
- interactive bottom-right resize;
- minimize/maximize/restore/close;
- Alt+Tab i Alt+F4;
- software pointer;
- jawne workspace/chrome geometry;
- userspace session ownership.

Aplikacje GUI po wejściu do desktopu muszą należeć do drzewa procesu Red Flux
Home. Historyczne anonimowe `/gui/*` requesty Ring-0 są kompatybilnościowym
no-op i nie tworzą procesów ani widocznych okien.

## Red Flux Dock

3.2 zastępuje dawny prosty Pulse Ribbon systemowym Dockiem. Przypięte pozycje:

- Home;
- Terminal;
- Files;
- Monitor;
- Settings;
- About.

Każda ma własną geometryczną ikonę KuroganeOS, running indicator i active/focus
state. Kliknięcie przypiętej aplikacji:

1. focusuje ją, jeśli już działa;
2. przywraca ją, jeśli jest zminimalizowana;
3. wysyła quick-launch do Home, jeśli nie działa.

Po prawej stronie przypiętej części Docka znajduje się dynamiczna sekcja żywych
okien do focus/restore. Dock nie jest klasycznym taskbarem i nie używa ikon ani
assetów Windows/macOS/GNOME/KDE.

## Pulpit i język wizualny

3.2 rozwija profil Red Flux:

- prawie czarne gradientowe tło;
- grafitowe surfaces;
- czerwony focus/active/danger;
- subtelny stalowy tekst pomocniczy;
- duży niski-kontrast geometryczny znak Kurogane w tle;
- top identity rail zamiast klasycznego desktop panelu;
- odświeżony window chrome i czerwony resize signal;
- osobne wizualne warstwy boot, login i desktop.

Brand geometry jest inspirowana ostrą formą logo KuroganeOS, ale renderer
pozostaje własnym zestawem prymitywów systemu.

## Software full-frame backbuffer

Pełny desktop renderuje się poza widocznym GOP dla wspieranych trybów do
1600x1200:

1. software cursor jest ukrywany;
2. primitives przechodzą na backbuffer;
3. WindowManager buduje całą klatkę;
4. gotowa klatka jest kopiowana do GOP;
5. cursor trafia na ukończony obraz.

Dla większego trybu istnieje direct-render fallback. Natywne per-window surfaces
i damage tracking pozostają etapem 3.3.

## Content clipping i body text scale

Przed callbackiem aplikacji WindowManager ustawia clip na content area okna.
`put_pixel`, `fill_rect`, `draw_rect`, `draw_char` i `draw_text` nie powinny
wychodzić poza ten prostokąt.

Compatibility body text ma limit skali zależny od szerokości okna. Window chrome
i tytuły są renderowane poza tym limitem.

## Public UI key codes

SDK publikuje nazwane wartości m.in.:

```text
KU_UI_KEY_ESCAPE
KU_UI_KEY_BACKSPACE
KU_UI_KEY_TAB
KU_UI_KEY_ENTER
KU_UI_KEY_HOME
KU_UI_KEY_ARROW_UP
KU_UI_KEY_ARROW_LEFT
KU_UI_KEY_ARROW_RIGHT
KU_UI_KEY_END
KU_UI_KEY_ARROW_DOWN
KU_UI_KEY_DELETE
```

Model sterowania:

- arrows — selection/navigation;
- Enter — activate;
- Escape — cancel/reset lokalnej interakcji;
- Tab — następny focus/selection;
- mysz — focus, drag, resize, controls, Dock i Login;
- Alt+Tab — następne okno;
- Alt+F4 — zamknięcie aktywnego okna.

## libui scene/view runtime

`kui_scene` posiada do 32 `kui_view` records. Dostępne compatibility view types:

- panel;
- label;
- button;
- input;
- list item;
- progress;
- separator.

`kui_flow` tworzy pionowy flow. Główny transport nadal wykorzystuje
`KU_SYS_UI_PRESENT` / `ku_ui_frame`; Red Flux 3.2 nie deklaruje jeszcze
natywnego widget compositora.

## FluxShellCore i Terminal

`/apps/shell` oraz `/gui/terminal` są frontendami tego samego
`userspace/common/flux_shell.h`. GUI Terminal dodaje scrollback i edycję inputu,
ale parser, commands, jobs, cwd, history semantics i capability errors pozostają
wspólne.

## Runtime markers 3.2

Przy poprawnym przejściu session flow serial powinien zawierać m.in.:

```text
[TEST] userspace_init_spawn: PASS
[TEST] red_flux_login_surface: PASS
[TEST] red_flux_session_gate: PASS
[TEST] red_flux_login_supervision: PASS
[TEST] red_flux_login_to_desktop: PASS
[TEST] red_flux_dock_controller: PASS
```

Markery potwierdzają ścieżkę kodu; brak flickera, poprawny Dock i wizualny flow
muszą być ocenione runtime w QEMU.

## Known GUI limitations

- software rendering only;
- compatibility `ku_ui_frame` nadal jest głównym transportem aplikacji;
- brak natywnych per-window surfaces i damage compositora;
- brak native widget pointer `widget_id`;
- brak wheel routing, clipboard, Unicode i context actions;
- login nie posiada jeszcze realnego account/credential service;
- brak multi-monitor i GPU compositora;
- brak publicznego `readdir/stat` dla pełnej nawigacji Files.

Zobacz `docs/roadmap/DESKTOP_ROADMAP.md` dla planu do 3.6.
