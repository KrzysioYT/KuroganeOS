# Graphics, input and Kurogane Red Flux Desktop

KuroganeOS 3.1 używa UEFI GOP framebuffer i własnego software renderera. Nie ma
jeszcze akcelerowanego sterownika GPU. PS/2 keyboard/mouse oraz obsługiwane HID
źródła zasilają wspólny InputManager.

## Desktop boot model

Normalny boot:

```text
UEFI
 -> kernel
 -> persistent FAT32 root
 -> scheduler/input
 -> Red Flux WindowManager
 -> /system/init PID 1
 -> /gui/launcher
 -> aplikacje Ring 3 uruchamiane na żądanie
```

PID1 nadzoruje Launcher jako root sesji. Terminal, Files, System Monitor,
Settings i About są dziećmi Launchera i mogą być normalnie zamykane bez
natychmiastowego respawnu przez PID1. Jeżeli session root nie wystartuje,
system zachowuje console fallback.

## Red Flux Window Core

WindowManager udostępnia:

- generation-checked window IDs;
- focus i z-order;
- header drag;
- interactive bottom-right resize;
- minimize/maximize/restore/close;
- Alt+Tab i Alt+F4;
- Signal Spine;
- Pulse Ribbon;
- software pointer;
- jawne workspace/chrome geometry.

3.1 zmienia główną identyfikację z wcześniejszego cyan/violet preview na:

- niemal czarne tło;
- grafitowe surfaces;
- stalowe, przygaszone granice;
- czerwony focus/active signal;
- jaśniejszą czerwień dla danger/close.

Kształty i asymetryczne czerwone sygnały są inspirowane ostrą geometrią logo
KuroganeOS, bez kopiowania Windows/macOS/GNOME/KDE.

## Software full-frame backbuffer

Problem 3.0.1 polegał na tym, że poprawny pełny repaint nadal był wykonywany
bezpośrednio do widocznego GOP. Podczas drag/resize użytkownik mógł więc zobaczyć
kolejne fazy:

```text
clear -> desktop -> część okien -> kompletna klatka
```

3.1 dodaje statyczny software backbuffer dla GOP do 1600x1200. WindowManager:

1. ukrywa software cursor;
2. przełącza primitives na backbuffer;
3. buduje kompletny desktop poza widocznym framebufferem;
4. kopiuje ukończoną klatkę do GOP;
5. rysuje cursor na gotowej klatce.

Dla większego trybu renderer zachowuje bezpieczny direct-render fallback.
Pełne per-window surfaces i damage compositor pozostają etapem 3.2.

## Content clipping i body text scale

Przed wywołaniem callbacku aplikacji WindowManager ustawia clip dokładnie na
content area okna. `put_pixel`, `fill_rect`, `draw_rect`, `draw_char` i
`draw_text` nie mogą zapisać pikseli poza clip.

Dodatkowo legacy body text scale jest ograniczany zależnie od szerokości
content area. Dzięki temu compatibility `ku_ui_frame` nie powinien już
nadpisywać sąsiednich surfaces tylko dlatego, że linia tekstu jest za długa.

Chrome/title renderuje się przed ustawieniem body clip/scale limitu.

## Public UI key codes

`sdk/include/kurogane/ui.h` publikuje nazwane wartości m.in.:

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

Aplikacje nie powinny używać magicznych wartości PS/2 scan code jako
`ku_ui_event.key`.

Domyślny model 3.1:

- arrows: selection/navigation;
- Enter: activate;
- Escape: cancel/reset lokalnej interakcji;
- Tab: następny focus/selection;
- J/K mogą istnieć jako opcjonalne aliasy, ale nie są głównym UX.

## libui scene/view runtime

`kui_scene` posiada do 32 `kui_view` records. View ma:

- stable non-zero ID;
- optional parent ID;
- type;
- flags;
- text;
- optional value/maximum.

Dostępne compatibility view types:

- panel;
- label;
- button;
- input;
- list item;
- progress;
- separator.

`kui_flow` tworzy prosty pionowy flow. 3.1 pozostawia transport
`KU_SYS_UI_PRESENT` / `ku_ui_frame`, ale upraszcza jego serializację: główne
kontrolki nie są już przedstawiane jako `[> ... ]`, `>>` i `::`.

Domyślna paleta `libui` jest Red Flux, więc również aplikacje tworzone przez SDK
startują z tym samym profilem, jeśli nie ustawią własnej palety.

## FluxShellCore i Terminal

`/apps/shell` oraz `/gui/terminal` są różnymi frontendami tego samego
`userspace/common/flux_shell.h`.

GUI Terminal dodaje:

- Up/Down history;
- Left/Right cursor movement;
- Home/End;
- Delete/Backspace;
- Escape/Ctrl-U clear input;
- GUI scrollback.

Parser, commands, jobs, cwd, history semantics i capability errors pozostają
wspólne z recovery console.

## Runtime markers 3.1

Oczekiwane dodatkowe markery:

```text
[TEST] desktop_arrow_navigation: PASS
[TEST] desktop_files_3_1_navigation: PASS
[TEST] desktop_settings_arrow_navigation: PASS
[TEST] desktop_terminal_3_1_shared_shell: PASS
[TEST] red_flux_sysmon: PASS
[TEST] red_flux_about: PASS
```

Markery oznaczają osiągnięcie odpowiedniej ścieżki kodu, a nie automatyczny
dowód braku problemów wizualnych. Drag/resize musi zostać oceniony w QEMU.

## Known GUI limitations

- software rendering only;
- compatibility `ku_ui_frame` nadal jest transportem głównego scene backendu;
- jeden live UI window per process w obecnym kernel ABI;
- brak native widget pointer hit testing;
- brak wheel routing, clipboard i context actions;
- brak multi-monitor i GPU compositor;
- brak publicznego `readdir/stat` dla pełnej nawigacji Files;
- per-window surfaces/damage compositor są celem 3.2.

See `docs/roadmap/DESKTOP_ROADMAP.md` for the plan through 3.6.
