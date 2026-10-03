/* LiteKern X — the KERN86 app ABI (Phase 2 §5): what ring 3 apps and the
 * kernel agree on. Included by both the kernel and the app SDK (sdk/), so
 * it holds only numbers and plain structs.
 *
 * Calls: `int 0x80`, EAX = number, arguments in EBX, ECX, EDX, ESI, EDI,
 * result in EAX (a negative errno on failure). The kernel checks every
 * pointer an app passes. v1's apps called kernel functions directly through
 * a jump table, in ring 0; these run in ring 3, and a crash only ends the app.
 *
 * The table grows only as apps need it. */
#ifndef LKX_KERN86_ABI_H
#define LKX_KERN86_ABI_H

#include <stdint.h>
#include "kernel/input.h"

/* --- calls ------------------------------------------------------------------- */
#define SYS_EXIT            0   /* ebx: code. Doesn't return */
#define SYS_DEBUG_WRITE     1   /* ebx: text, ecx: length. Logs "user: <text>" */
#define SYS_UPTIME_MS       2
#define SYS_WINDOW_OPEN     3   /* ebx: struct k86_window * out. Maps the canvas (the window's
                                 * own pixels: draw, then SYS_WINDOW_PRESENT) */
#define SYS_WINDOW_PRESENT  4   /* ebx, ecx, edx, esi: x, y, w, h of the canvas to show */
#define SYS_WINDOW_HEADER   5   /* ebx: const struct k86_header *: title and buttons */
#define SYS_WAIT_EVENT      6   /* ebx: struct k86_event * out. Sleeps until one comes */
#define SYS_FONT            7   /* ebx: uint8_t[K86_FONT_BYTES] out: the 8x16 font */
#define SYS_FS_VOLUMES      8   /* ebx: struct k86_volume * out, ecx: max. Returns the count */
#define SYS_FS_LIST         9   /* ebx: volume, ecx: path, edx: struct k86_dirent * out,
                                 * esi: max. Returns the count (all of them: may be > max) */
#define SYS_FS_CREATE       10  /* ebx: volume, ecx: folder path, edx: name, esi: 1 = folder */
#define SYS_FS_RENAME       11  /* ebx: volume, ecx: folder path, edx: old name, esi: new name */
#define SYS_FS_DELETE       12  /* ebx: volume, ecx: folder path, edx: name (folders: all of it) */
#define SYS_THEME_GET       13  /* ebx: struct theme * out (kernel/theme.h): the current colours */
#define SYS_APPEARANCE      14  /* ebx: struct k86_appearance * out: settings and choices */
#define SYS_APPEARANCE_SET  15  /* ebx: style (0 dark, 1 light, -1 keep), ecx: accent (-1 keep),
                                 * edx: wallpaper name (NULL keep, "" none). Every app window
                                 * gets K86_EVENT_THEME */
#define SYS_WALLPAPER_THUMB 16  /* ebx: name, ecx: uint32_t * out, edx: w, esi: h (<= 256 x 160) */
#define SYS_FS_READ         17  /* ebx: volume, ecx: folder path, edx: name, esi: buffer,
                                 * edi: length. Reads from the start; returns the bytes read */
#define SYS_SCREENSAVER_SET 18  /* ebx: seconds without input before the screen saver
                                 * starts (0 = never, at most 3600); or ecx = 1: show it
                                 * now (a preview; any input ends it) */

/* --- memory -------------------------------------------------------------------- */
#define K86_APP_BASE        0x80000000u     /* where an app's image is loaded */
#define K86_CANVAS          0xa0000000u     /* where SYS_WINDOW_OPEN maps the canvas */
#define K86_FONT_BYTES      (256 * 16)
#define K86_PATH_MAX        256             /* "/Documents/Work", NUL included */
#define K86_NAME_MAX        64

/* --- the .lkx file: this header, then the image loaded at K86_APP_BASE ----------- */
#define LKX_MAGIC           0x41584b4cu     /* "LKXA" */
#define LKX_VERSION         1

struct lkx_header {
    uint32_t magic, version;
    uint32_t image_size;    /* bytes after this header */
    uint32_t bss_size;      /* zeroed bytes mapped after the image */
    uint32_t entry;         /* virtual address */
    uint32_t reserved[3];
};

/* --- structures ----------------------------------------------------------------- */
struct k86_window {
    int32_t w, h;           /* the content area, in pixels (K86_EVENT_RESIZE changes them) */
    uint32_t *canvas;       /* 0x00RRGGBB, `stride` pixels a row (room for the largest size) */
    int32_t stride;
};

#define K86_MAX_BUTTONS 6
struct k86_header {
    char title[48];
    int32_t nbuttons;
    struct {
        int32_t side;       /* 0 left, 1 right */
        int32_t icon;       /* 0 text, 1 back, 2 add, 3 up (kernel/wm.h's order) */
        int32_t id;
        char label[16];
    } buttons[K86_MAX_BUTTONS];
};

/* Event types: the same values as kernel/wm.h's. */
#define K86_EVENT_KEY       0
#define K86_EVENT_CLICK     1
#define K86_EVENT_HEADER    2   /* id: a header button */
#define K86_EVENT_CLOSE     3   /* close now: the next SYS_WAIT_EVENT ends the app */
#define K86_EVENT_POINTER   4   /* x, y in the canvas, buttons held, changed */
#define K86_EVENT_THEME     5   /* the style or accent changed: redraw (the SDK has already
                                 * fetched the new theme) */
#define K86_EVENT_RESIZE    6   /* x, y: the canvas's new width and height: lay out, redraw */

struct k86_event {
    int32_t type;
    int32_t x, y;
    uint8_t buttons, changed, pad[2];
    int32_t id;
    uint32_t time_ms;
    struct key_event key;
};

struct k86_volume {
    char name[40];
    int32_t supported, read_only;
    char why_not[24];
    uint32_t free_kib;
};

#define K86_MAX_ACCENTS 8
#define K86_MAX_WALLPAPERS 8
struct k86_appearance {
    int32_t style;          /* 0 dark, 1 light */
    int32_t accent;
    char wallpaper[32];     /* "" for the plain colour */
    int32_t naccents;
    struct {
        char name[12];
        uint32_t colour;
    } accents[K86_MAX_ACCENTS];
    int32_t nwallpapers;
    char wallpapers[K86_MAX_WALLPAPERS][32];
    int32_t screensaver_s;  /* seconds without input before it starts; 0 = never */
};

struct k86_dirent {
    char name[K86_NAME_MAX];
    uint32_t size;
    int32_t is_dir;
    uint16_t date, time;
};

#endif
