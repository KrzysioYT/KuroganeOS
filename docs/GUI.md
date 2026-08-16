# Graphics, input and Kurogane Flux Desktop

KuroganeOS 2.5 uses the UEFI GOP framebuffer with software rendering. There is
no accelerated GPU driver yet. PS/2 keyboard/mouse and supported xHCI HID input
feed the shared InputManager.

## Desktop boot model

Normal userspace boot starts the Flux desktop session introduced in 2.3. Safe
Mode remains the text-only emergency environment.

```text
UEFI
 -> kernel
 -> persistent FAT32 root
 -> scheduler/input
 -> Flux desktop session
 -> /system/init (PID 1)
 -> /gui/terminal
 -> /gui/files
 -> /gui/sysmon
 -> /gui/settings
 -> /gui/about
```

PID1 supervises the Ring-3 desktop applications and restarts individual GUI
children that exit. A broken initial GUI session falls back to `/apps/shell`.

## Flux Window Core

The 2.4 WindowManager provides generation-checked windows, focus/z-order,
header drag, minimize, expand/restore, dismiss, Alt+Tab and Alt+F4.

Its presentation is Kurogane Flux rather than classic desktop chrome:

- Signal Spine for session activity/focus;
- Pulse Ribbon for active/minimized surfaces;
- geometric Flux control rail instead of `- [] X`;
- explicit workspace geometry;
- shared chrome geometry and reserved resize grip;
- 2.4.1 repaint hotfix removes the periodic idle full-screen clear that caused
  severe flicker in QEMU.

## 2.5 Flux UI Runtime

2.5.0 introduces a userspace scene/view layer in `libui`. Applications no
longer need to treat the UI as a manually indexed set of text rows.

### Scene model

`kui_scene` owns up to 32 `kui_view` records. Each view has:

- stable non-zero ID;
- optional parent ID;
- type;
- flags;
- text;
- optional value/maximum pair.

The parent must already exist when a child is inserted. This guarantees an
acyclic construction order and gives later native render backends a stable tree
to consume.

### View types

2.5.0 exposes:

- panel;
- label;
- button;
- input;
- list item;
- progress;
- separator.

`kui_flow` is the first layout primitive. It inserts views in a vertical flow
under a chosen parent. More capable pixel/layout constraints can be added later
without changing application ownership of the scene.

### Interaction helpers

The userspace runtime provides:

- `kui_scene_select()`;
- `kui_scene_select_next()`;
- `kui_scene_selected()`;
- `kui_scene_scroll()`;
- text/flags/value mutation helpers.

The first backend still transports the rendered result through the existing
`KU_SYS_UI_PRESENT` frame ABI. That is deliberate: application code is migrated
first, then later 2.5.x patches can replace the transport with native widget
records, kernel hit testing, wheel routing, dialogs and custom surfaces without
rewriting every application again.

## Migrated Ring-3 applications

| Application | 2.5 state |
|---|---|
| Files | scene + panel/list hierarchy + selection/scroll model |
| Settings | scene + buttons + focus traversal + palette switching |
| System Monitor | scene + labels + progress view |
| About | scene + hierarchical information views |
| Flux Terminal | legacy frame backend temporarily retained for its text buffer |

Expected runtime markers include:

```text
[TEST] flux_scene_files: PASS
[TEST] flux_scene_settings: PASS
[TEST] flux_scene_sysmon: PASS
[TEST] flux_scene_about: PASS
```

## Public kernel transport

Syscalls 14-17 still provide create, present, poll and close. Each process owns
one live window. `KU_SYS_UI_PRESENT` remains the compatibility backend in 2.5.0.

Planned 2.5.x transport work:

- native widget records instead of line serialization;
- pointer hit testing returning widget IDs;
- mouse-wheel routing;
- modal/dialog primitives;
- custom surfaces;
- more precise dirty/damage regions.

## Known GUI limitations

- software framebuffer rendering only;
- no general interactive resize yet;
- one live UI window per process in the kernel ABI;
- 2.5.0 scenes are rendered through the compatibility frame backend;
- no clipboard, Unicode text input or accessibility layer yet;
- no multi-monitor or GPU compositor;
- native readdir/stat powered Files navigation remains a 2.6 task.

See `docs/roadmap/DESKTOP_ROADMAP.md` for the 2.3 → 3.6 plan.
