/* LiteKern X — input events, as read() from input devices (kbd0; the
 * touchpad later). Reads return whole events: `len` must be a multiple of
 * the event size (-EINVAL otherwise), and 0 means nothing is waiting —
 * reads never block. */
#ifndef LKX_INPUT_H
#define LKX_INPUT_H

#include <stdint.h>

/* Key codes: the scancode-set-1 make code of the key, plus KEY_EXTENDED for
 * keys sent with an E0 prefix. US layout. */
#define KEY_EXTENDED    0x100
#define KEY_ESC         0x001
#define KEY_BACKSPACE   0x00e
#define KEY_TAB         0x00f
#define KEY_ENTER       0x01c
#define KEY_LCTRL       0x01d
#define KEY_LSHIFT      0x02a
#define KEY_RSHIFT      0x036
#define KEY_LALT        0x038
#define KEY_SPACE       0x039
#define KEY_CAPSLOCK    0x03a
#define KEY_F1          0x03b   /* F1-F10 are 0x03b-0x044 */
#define KEY_F11         0x057
#define KEY_F12         0x058
#define KEY_RCTRL       (KEY_EXTENDED | 0x01d)
#define KEY_RALT        (KEY_EXTENDED | 0x038)
#define KEY_HOME        (KEY_EXTENDED | 0x047)
#define KEY_UP          (KEY_EXTENDED | 0x048)
#define KEY_PAGEUP      (KEY_EXTENDED | 0x049)
#define KEY_LEFT        (KEY_EXTENDED | 0x04b)
#define KEY_RIGHT       (KEY_EXTENDED | 0x04d)
#define KEY_END         (KEY_EXTENDED | 0x04f)
#define KEY_DOWN        (KEY_EXTENDED | 0x050)
#define KEY_PAGEDOWN    (KEY_EXTENDED | 0x051)
#define KEY_INSERT      (KEY_EXTENDED | 0x052)
#define KEY_DELETE      (KEY_EXTENDED | 0x053)

/* kbd0 ioctl */
#define KBD_GET_DROPPED 1       /* arg: uint32_t *, events lost to a full queue */

#define MOD_SHIFT       0x01
#define MOD_CTRL        0x02
#define MOD_ALT         0x04
#define MOD_CAPSLOCK    0x08

struct key_event {
    uint16_t key;       /* KEY_* / scancode, see above */
    uint8_t pressed;    /* 1 = press (or auto-repeat), 0 = release */
    uint8_t mods;       /* MOD_* in effect for this event */
    uint8_t ascii;      /* the character typed, 0 if none (ctrl+letter = 1..26) */
    uint8_t reserved[3];
};

#endif
