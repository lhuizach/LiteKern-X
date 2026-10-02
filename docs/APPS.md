# LiteKern X — Making an App (KERN86)

Apps are ring 3 programs. A bug in one ends only that app: a crash, a bad pointer passed to the kernel, or a loop that never returns (stopped after 10 s) all leave the rest of the system running. They come from the **ramdisk**, which sits on the boot disk after the kernel. Each app is read from it the first time it's opened.

## An app is a folder in `apps/`
```
apps/notes/
  kerns.json     {"name": "Notes", "icon": "notes", "entry": "notes.lkx", "order": 4,
                  "width": 640, "height": 480}
  notes.c        (any number of .c files)
```
- `name` is shown under the icon and in the top bar.
- `icon` is a name from `assets/icons.json` (48×48 PNG; see `docs/ASSET-PROMPTS.md` §4). Leave it empty for a letter tile.
- `entry` must be `<folder>.lkx`: the build makes that file from the folder's C files.
- `order` (optional) sets the position in the app menu (and the dock), lowest first; the default is 100. The current apps use Files 1, Calculator 2, Settings 3.
- `pinned` (optional, 1): a favourite, always in the dock. Other apps are started from Show Apps and sit in the dock, after a line, only while they're open. Files is pinned; so is Log.
- `width`, `height` (optional): the window's size when it opens, header bar included (default 760 x 480). `min_width`, `min_height`: the smallest it can be resized to (default 300 x 200). Keep within 1024 x 570, the EeePC's space below the top bar.

`make` builds every folder in `apps/` and packs it into the ramdisk. The app then appears in the app menu, before the built-in Log app.

## The code
```c
#include "kernel/text.h"        /* the GUI font */
#include "kernel/theme.h"
#include "kernel/widget.h"      /* the same widgets the kernel uses */
#include "sdk/kern86.h"

static struct gfx_surface c;

static void draw(void)
{
    const struct theme *t = theme_get();
    gfx_fill_rect(&c, 0, 0, c.w, c.h, t->window_bg);
    text_draw(&c, 20, 20, "Hello", TEXT_HEADING, t->fg);
    k86_present(0, 0, c.w, c.h);                    /* show what changed */
}

int main(void)
{
    struct k86_window w;
    k86_window_open(&w);                            /* the canvas: w.w x w.h pixels */
    c = (struct gfx_surface){ w.canvas, w.w, w.h, w.stride };
    draw();
    for (;;) {
        struct k86_event ev;
        k86_wait_event(&ev);                        /* sleeps; the desktop keeps running */
        if (ev.type == K86_EVENT_CLOSE)
            return 0;                               /* the close button */
        if (ev.type == K86_EVENT_THEME)
            draw();                                 /* the style or accent changed */
        if (ev.type == K86_EVENT_RESIZE) {          /* the window was resized */
            c.w = ev.x;
            c.h = ev.y;
            draw();
        }
        /* K86_EVENT_KEY (ev.key), K86_EVENT_POINTER (ev.x, ev.y, ev.buttons,
         * ev.changed: feed wg_pointer_make()), K86_EVENT_HEADER (ev.id) */
    }
}
```
- **Drawing:** draw into the canvas with `kernel/gfx.h`, `kernel/text.h` and `kernel/widget.h`, which are compiled into the app. Then call `k86_present(x, y, w, h)` for the part that changed. The canvas is the window's own pixels: its rows are `w.stride` pixels apart (room for the largest size), and the window keeps them while it's moved, covered or minimised, so an app never redraws for those.
- **Resizing:** the user can resize the window (and maximise it). The app gets `K86_EVENT_RESIZE` with the new width and height in `ev.x`, `ev.y`: lay out again for that size and redraw. Lay out from the canvas size, never from fixed screen numbers.
- **Text:** the GUI font, in five styles: `TEXT_BODY`, `TEXT_BOLD`, `TEXT_SMALL`, `TEXT_HEADING`, `TEXT_LARGE`. Use `text_width()` to measure and `text_draw_fit()` to cut text short with an ellipsis. `TEXT_MINUS`, `TEXT_TIMES`, `TEXT_DIVIDE` and `TEXT_ELLIPSIS` are the symbols beyond ASCII.
- **Colours:** always come from `theme_get()`, never hard-coded. When the user changes the style or accent in Settings, the app gets `K86_EVENT_THEME`, and by then the SDK has already updated `theme_get()`: just redraw.
- **Header bar:** `k86_header()` sets the title and up to 6 buttons (text, or the back/add/up icons). A press arrives as `K86_EVENT_HEADER` with its id.
- **Files:** call `k86_volumes()`, then `k86_list/create/rename/delete/read(volume, "/folder/path", ...)`. The kernel checks every path and name itself, and only FAT32 is ever written.
- **Log:** `k86_log("...")` / `k86_logf(...)` shows up in the Log app as `user: ...`. Keep it ASCII, since the tests grep it.
- The full list of calls, and their numbers and structures, is in `kernel/kern86_abi.h`. Each is a thin wrapper over `int 0x80` (`sdk/kern86.h`). New calls are added only when an app needs one.

## Examples
- `apps/files/files.c`: lists, dialogs, text entry, paths, a scrolling text viewer.
- `apps/calculator/calculator.c`: a grid of buttons, the keyboard, and whole-number maths, because there's no floating point in apps.
- `apps/settings/settings.c`: hand-drawn previews, appearance calls, theme events.

## Limits
- Up to 8 windows at once, one per app. Apps take turns: one runs while the others wait for events, so an app must go back to `k86_wait_event()` quickly; nothing else runs while it works.
- Up to 1 MiB of code and data, a 16 KiB stack, and no `malloc`: use static arrays.
- No floating point (apps are built with `-mgeneral-regs-only`). Use fixed-point whole numbers, as Calculator does.
- There's no timer event yet. An app only wakes for input, and must return to `k86_wait_event()` within 10 s.
