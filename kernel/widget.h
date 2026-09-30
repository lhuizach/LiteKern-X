/* LiteKern X — core GUI widgets (Phase 2 §4), in the Adwaita dark style.
 *
 * The minimum set Files needs: labels, buttons, a list, a text entry and a
 * dialog. Deliberately small; Phase 3 polishes them.
 *
 * Widgets are plain structs the app owns. The app sets each one's rectangle
 * (in its own surface's coordinates), passes it input, and redraws the ones
 * whose `dirty` flag input has set; drawing clears it. Colours come from the
 * theme, with Adwaita's translucent ones mixed over `under`, the colour the
 * widget sits on (0 = the theme's window background).
 *
 * Nothing here knows about the kernel: only gfx, the font and the theme,
 * so this can move to the user-side app library in §5 as it is. */
#ifndef LKX_WIDGET_H
#define LKX_WIDGET_H

#include <stdint.h>
#include "kernel/gfx.h"
#include "kernel/input.h"

#define WG_TEXT_MAX 64          /* longest name an entry takes, NUL included */
#define WG_DOUBLE_CLICK_MS 400

/* The left button as widgets see it. */
struct wg_pointer {
    int x, y;
    int held;           /* the left button is down */
    int down, up;       /* it went down / up with this update */
    uint32_t time_ms;
};
struct wg_pointer wg_pointer_make(int x, int y, uint8_t buttons, uint8_t changed, uint32_t time_ms);

/* --- text ----------------------------------------------------------------- */
enum wg_text { WG_TEXT_BODY, WG_TEXT_DIM, WG_TEXT_TITLE, WG_TEXT_ERROR };
void wg_label(struct gfx_surface *s, int x, int y, const char *text, enum wg_text style);
/* Centred on x. */
void wg_label_centred(struct gfx_surface *s, int cx, int y, const char *text, enum wg_text style);
/* Draw text in at most max_w pixels, ending in "..." if it doesn't fit.
 * Returns the width drawn. */
int wg_text_fit(struct gfx_surface *s, int x, int y, const char *text, int max_w, uint32_t colour);

/* --- button --------------------------------------------------------------- */
enum wg_button_style {
    WG_BUTTON_NORMAL,
    WG_BUTTON_SUGGESTED,        /* the main action: accent blue */
    WG_BUTTON_DESTRUCTIVE,      /* deleting: red */
    WG_BUTTON_FLAT,             /* no background until hovered */
};

struct wg_button {
    struct gfx_rect r;
    const char *label;
    enum wg_button_style style;
    uint32_t under;
    int disabled;
    int hover, pressed, dirty;
};

void wg_button_draw(struct gfx_surface *s, struct wg_button *b);
/* 1 when clicked: pressed and released over it, while enabled. */
int wg_button_pointer(struct wg_button *b, const struct wg_pointer *p);
void wg_button_set_disabled(struct wg_button *b, int disabled);
/* Width that fits its label with Adwaita's padding. */
int wg_button_width(const char *label);

/* --- list (an Adwaita "boxed list") ---------------------------------------- */
enum wg_icon { WG_ICON_NONE, WG_ICON_FOLDER, WG_ICON_FILE };

struct wg_row {
    const char *name;
    const char *detail;         /* right-aligned, dim; may be NULL */
    enum wg_icon icon;
};

enum wg_list_result { WG_LIST_NONE, WG_LIST_CHANGED, WG_LIST_ACTIVATE };

struct wg_list {
    struct gfx_rect r;          /* the card; rows are the theme's row_h tall */
    int count;
    int selected;               /* -1: none */
    int hover;                  /* -1: none */
    int top;                    /* first row shown (scrolling) */
    void (*row)(void *ctx, int i, struct wg_row *out);
    void *ctx;
    uint32_t under;
    int dirty;
    int last_click_row;
    uint32_t last_click_ms;
};

void wg_list_draw(struct gfx_surface *s, struct wg_list *l);
int wg_list_rows_shown(const struct wg_list *l);
/* A click selects (CHANGED); a second click on the same row within
 * WG_DOUBLE_CLICK_MS activates it. A click on the scrollbar pages. */
enum wg_list_result wg_list_pointer(struct wg_list *l, const struct wg_pointer *p);
/* Up/Down/Home/End/PgUp/PgDn move the selection; Enter activates. */
enum wg_list_result wg_list_key(struct wg_list *l, const struct key_event *k);
/* Change the row count (keeping the selection in range) / select a row and
 * scroll it into view. */
void wg_list_set_count(struct wg_list *l, int count);
void wg_list_select(struct wg_list *l, int i);

/* --- text entry ------------------------------------------------------------- */
enum wg_entry_result { WG_ENTRY_NONE, WG_ENTRY_CHANGED, WG_ENTRY_ACTIVATE, WG_ENTRY_CANCEL };

struct wg_entry {
    struct gfx_rect r;
    char text[WG_TEXT_MAX];
    int len, cursor;            /* cursor: 0..len, before that character */
    int scroll;                 /* first character shown */
    int focused, disabled;
    uint32_t under;
    int dirty;
};

void wg_entry_set(struct wg_entry *e, const char *text);   /* cursor at the end */
void wg_entry_draw(struct gfx_surface *s, struct wg_entry *e);
/* Printable characters insert; Backspace/Delete, Left/Right/Home/End edit;
 * Enter -> ACTIVATE, Esc -> CANCEL. Only while focused. */
enum wg_entry_result wg_entry_key(struct wg_entry *e, const struct key_event *k);
/* A click inside focuses it and moves the cursor there. */
enum wg_entry_result wg_entry_pointer(struct wg_entry *e, const struct wg_pointer *p);

/* --- dialog (an Adwaita alert dialog) --------------------------------------- */
#define WG_DIALOG_NONE (-1)

struct wg_dialog {
    const char *title;
    char body[160];
    int body_error;             /* body shown as an error (red) */
    int has_entry;
    struct wg_entry entry;
    struct wg_button buttons[2];    /* [0] cancels; the last is the default */
    int nbuttons;
    struct gfx_rect r;          /* the card, set by wg_dialog_layout */
    int dirty;
};

/* A dialog with a Cancel button and one action button; with an entry if
 * has_entry (focused, empty). */
void wg_dialog_init(struct wg_dialog *d, const char *title, const char *body,
                    const char *action, enum wg_button_style action_style, int has_entry);
void wg_dialog_set_body(struct wg_dialog *d, const char *body, int error);
/* Centre the card in a w x h area. */
void wg_dialog_layout(struct wg_dialog *d, int w, int h);
/* Dim the whole surface (what's behind), then draw the card. */
void wg_dialog_show(struct gfx_surface *s, struct wg_dialog *d);
/* Redraw just the card (after input set dirty flags). */
void wg_dialog_draw(struct gfx_surface *s, struct wg_dialog *d);
/* The index of the button chosen, or WG_DIALOG_NONE. Enter chooses the
 * last (default) button, Esc the first (Cancel). */
int wg_dialog_pointer(struct wg_dialog *d, const struct wg_pointer *p);
int wg_dialog_key(struct wg_dialog *d, const struct key_event *k);

#endif
