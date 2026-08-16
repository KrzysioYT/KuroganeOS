# GUI Applications on KuroganeOS

KuroganeOS GUI applications are normal Ring-3 processes. They do not draw by
writing directly to the physical framebuffer.

## Minimal model

```text
application
 -> libui / UI SDK
 -> UI syscalls
 -> WindowManager
 -> compositor/framebuffer
```

## Open a window

Typical helper pattern used by built-in GUI apps:

```c
#include <kurogane/libui.h>
#include <kurogane/kurogane.h>

ku_ui_window_options options = {
    sizeof(ku_ui_window_options),
    200, 160,
    520, 320
};

ku_result_t result = ku_ui_create("MY APP", 6U, &options);
if (result <= 0) return 1;
ku_window_t window = (ku_window_t)result;
```

Use the exact structure size expected by the ABI.

## Build a scene

```c
kui_scene scene;
kui_flow flow;

kui_scene_initialize(&scene);
kui_scene_set_palette(
    &scene,
    UINT32_C(0x111216),
    UINT32_C(0xEEF0F3),
    UINT32_C(0xE0162B));

kui_flow_begin(&flow, &scene, 0U);
(void)kui_flow_panel(&flow, 1U, "MY APP");
(void)kui_flow_label(&flow, 2U, "Hello KuroganeOS");
(void)kui_flow_button(&flow, 3U, "OK");
(void)kui_scene_select(&scene, 3U);

if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
    (void)ku_ui_close(window);
    return 2;
}
```

## Event loop

```c
for (;;) {
    ku_ui_event event;
    const int available = kui_next_event(window, &event);
    if (available < 0) break;
    if (available == 0) {
        (void)kuro_sleep(1U);
        continue;
    }

    if (event.type == KU_UI_EVENT_CLOSE) break;

    if (event.type == KU_UI_EVENT_KEY) {
        if (event.key == KU_UI_KEY_ENTER) {
            /* activate selected action */
        }
    }
}

(void)ku_ui_close(window);
```

## Keyboard policy

Use named public key codes. Primary navigation should use:

```text
Arrow Up/Down/Left/Right
Tab
Enter
Escape
Home/End where appropriate
```

Do not make `J/K` the only navigation model. Historical aliases may remain, but
the GUI should be understandable without Vim-style knowledge.

## Layout

The current `libui` scene transport is deliberately simple. Avoid hardcoding
text that only fits a single window size. Keep labels short and expect the
WindowManager to resize/move the surface.

## Color policy

The current Red Flux identity uses:

```text
dark/black background
graphite panels
light text
red focus/accent
```

Applications may use their own palette, but built-in system apps should remain
visually consistent.

## Avoid flicker

Do not call `present` continuously when nothing changed.

Preferred pattern:

```text
state changes -> rebuild/present scene
idle -> sleep/wait for event
```

System Monitor is an exception because live data changes over time, but it
should still update at a bounded cadence rather than every possible loop.

## Process ownership

GUI processes are part of the Red Flux session process tree. A normal app should
be launched by Home/another userspace process instead of anonymously from
Ring-0.

## Closing

On a close event:

1. release/close the window;
2. release application resources;
3. return from `main` or call the public exit wrapper.

Do not spin forever after the user closes the app.
