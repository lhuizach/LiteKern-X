# LiteKern X — Making an App (KERN86, Phase 2)

Apps are ring 3 programs. A bug in one ends only that app: a crash, a bad pointer passed to the kernel, or a loop that never returns (stopped after 10 s) all leave the rest of the system running. They come from the **ramdisk**, which is built into the kernel image.

## An app is a folder in `apps/`
```
apps/notes/
  kerns.json     {"name": "Notes", "icon": "notes", "entry": "notes.lkx"}
  notes.c        (any number of .c files)
```
- `name` is shown under the icon and in the window's title.
- `icon` is a name from `assets/icons.json` (48×48 PNG; see `docs/ASSET-PROMPTS.md` §4). Leave it empty for a letter tile.
- `entry` must be `<folder>.lkx`: the build makes that file from the folder's C files.

`make` builds every folder in `apps/` and packs it into the ramdisk. The app then appears in the dock and the app menu, before the built-in Log app.

## The code
```c
#include "kernel/theme.h"
#include "kernel/widget.h"      /* the same widgets the kernel uses */
#include "sdk/kern86.h"

int main(void)
{
    struct k86_window w;
    k86_window_open(&w);                            /* the canvas: w.w x w.h pixels */
    struct gfx_surface c = { w.canvas, w.w, w.h, w.w };
    gfx_fill_rect(&c, 0, 0, c.w, c.h, theme_get()->window_bg);
    wg_label(&c, 20, 20, "Hello", WG_TEXT_TITLE);
    k86_present(0, 0, c.w, c.h);                    /* show what changed */

    for (;;) {
        struct k86_event ev;
        k86_wait_event(&ev);                        /* sleeps; the desktop keeps running */
        if (ev.type == K86_EVENT_CLOSE)
            return 0;                               /* close button or Home */
        /* K86_EVENT_KEY (ev.key), K86_EVENT_POINTER (ev.x, ev.y, ev.buttons,
         * ev.changed: feed wg_pointer_make()), K86_EVENT_HEADER (ev.id) */
    }
}
```
- **Drawing:** into the canvas with `kernel/gfx.h` and `kernel/widget.h`, compiled into the app, then `k86_present(x, y, w, h)` for the part that changed. Colours come from `theme_get()`.
- **Header bar:** `k86_header()` sets the title and up to 6 buttons (text, or the back/add/up icons). A press arrives as `K86_EVENT_HEADER` with its id.
- **Files:** `k86_volumes()`, then `k86_list/create/rename/delete(volume, "/folder/path", ...)`. The kernel checks every path and name itself, and only FAT32 is ever written.
- **Log:** `k86_log("...")` / `k86_logf(...)` shows up in the Log app as `user: ...`.
- The full list of calls, and their numbers and structures, is in `kernel/kern86_abi.h`. Each is a thin wrapper over `int 0x80` (`sdk/kern86.h`). New calls are added only when an app needs one.

## Limits (Phase 2)
- One app at a time, full screen.
- Up to 1 MiB of code and data, a 16 KiB stack, and no `malloc`: use static arrays.
- There's no timer event yet. An app only wakes for input, and must return to `k86_wait_event()` within 10 s.
- Files is the example to copy: `apps/files/files.c`.
