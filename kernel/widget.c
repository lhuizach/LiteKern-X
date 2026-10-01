#include "kernel/widget.h"
#include "kernel/text.h"
#include "kernel/theme.h"

/* Adwaita's translucent colours, as alpha out of 255 over what's beneath. */
#define A_BUTTON        26      /* 10%: resting button */
#define A_BUTTON_HOVER  38      /* 15% */
#define A_BUTTON_ACTIVE 77      /* 30% */
#define A_CARD          20      /* 8%: boxed-list card */
#define A_ROW_HOVER     10      /* 4% more over the card */
#define A_SELECTED      64      /* accent at 25% */
#define A_SEPARATOR     90      /* black at 35% over the card */
#define A_DIM_BEHIND    120     /* black over the window behind a dialog */
#define WHITE           0xffffff

static const struct theme *T(void)
{
    return theme_get();
}

static uint32_t under_or_window(uint32_t under)
{
    return under ? under : T()->window_bg;
}

static int inside(struct gfx_rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int length(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

struct wg_pointer wg_pointer_make(int x, int y, uint8_t buttons, uint8_t changed, uint32_t time_ms)
{
    return (struct wg_pointer){
        .x = x, .y = y,
        .held = !!(buttons & MOUSE_LEFT),
        .down = (buttons & changed & MOUSE_LEFT) != 0,
        .up = (~buttons & changed & MOUSE_LEFT) != 0,
        .time_ms = time_ms,
    };
}

/* --- text ------------------------------------------------------------------- */

static uint32_t text_colour(enum wg_text style)
{
    switch (style) {
    case WG_TEXT_DIM:   return T()->fg_dim;
    case WG_TEXT_ERROR: return T()->destructive;
    default:            return T()->fg;
    }
}

static enum text_style text_style_of(enum wg_text style)
{
    return style == WG_TEXT_TITLE ? TEXT_BOLD : style == WG_TEXT_HEADING ? TEXT_HEADING
         : style == WG_TEXT_SMALL ? TEXT_SMALL : TEXT_BODY;
}

int wg_label(struct gfx_surface *s, int x, int y, const char *text, enum wg_text style)
{
    return text_draw(s, x, y, text, text_style_of(style), text_colour(style));
}

void wg_label_centred(struct gfx_surface *s, int cx, int y, const char *text, enum wg_text style)
{
    wg_label(s, cx - text_width(text, text_style_of(style)) / 2, y, text, style);
}

int wg_label_width(const char *text, enum wg_text style)
{
    return text_width(text, text_style_of(style));
}

int wg_label_height(enum wg_text style)
{
    return text_height(text_style_of(style));
}

int wg_text_fit(struct gfx_surface *s, int x, int y, const char *text, int max_w, uint32_t colour)
{
    return text_draw_fit(s, x, y, text, max_w, TEXT_BODY, colour);
}

/* --- button ------------------------------------------------------------------ */

int wg_button_width(const char *label)
{
    return text_width(label, TEXT_BOLD) + 2 * 17;
}

void wg_button_draw(struct gfx_surface *s, struct wg_button *b)
{
    const struct theme *t = T();
    uint32_t under = under_or_window(b->under), bg, fg = t->fg;
    int hover = b->hover && !b->disabled, pressed = b->pressed && !b->disabled;

    switch (b->style) {
    case WG_BUTTON_SUGGESTED:
    case WG_BUTTON_DESTRUCTIVE: {
        uint32_t base = b->style == WG_BUTTON_SUGGESTED ? t->accent_bg : t->destructive_bg;
        bg = pressed ? gfx_mix(0, base, 51) : hover ? gfx_mix(WHITE, base, 26) : base;
        fg = WHITE;
        break;
    }
    case WG_BUTTON_FLAT:
        bg = pressed ? gfx_mix(t->ink, under, A_BUTTON_ACTIVE)
           : hover ? gfx_mix(t->ink, under, A_BUTTON) : under;
        break;
    default:
        bg = gfx_mix(t->ink, under, pressed ? A_BUTTON_ACTIVE : hover ? A_BUTTON_HOVER : A_BUTTON);
        break;
    }
    if (b->disabled) {          /* Adwaita: the whole button at 50% opacity */
        bg = gfx_mix(bg, under, 128);
        fg = gfx_mix(fg, under, 128);
    }
    gfx_fill_rect(s, b->r.x, b->r.y, b->r.w, b->r.h, under);
    gfx_fill_round_rect(s, b->r.x, b->r.y, b->r.w, b->r.h, t->radius, bg);
    int x = b->r.x + (b->r.w - text_width(b->label, TEXT_BOLD)) / 2;
    int y = b->r.y + (b->r.h - text_height(TEXT_BOLD)) / 2;
    text_draw(s, x, y, b->label, TEXT_BOLD, fg);              /* buttons are bold */
    b->dirty = 0;
}

int wg_button_pointer(struct wg_button *b, const struct wg_pointer *p)
{
    int over = inside(b->r, p->x, p->y), clicked = 0;
    if (b->disabled) {
        if (b->hover || b->pressed)
            b->dirty = 1;
        b->hover = b->pressed = 0;
        return 0;
    }
    if (over != b->hover) {
        b->hover = over;
        b->dirty = 1;
    }
    if (p->down && over) {
        b->pressed = 1;
        b->dirty = 1;
    } else if (p->up && b->pressed) {
        b->pressed = 0;
        b->dirty = 1;
        clicked = over;
    }
    return clicked;
}

void wg_button_set_disabled(struct wg_button *b, int disabled)
{
    if (b->disabled != !!disabled) {
        b->disabled = !!disabled;
        b->dirty = 1;
    }
}

/* --- list ------------------------------------------------------------------- */

int wg_list_rows_shown(const struct wg_list *l)
{
    int n = l->r.h / T()->row_h;
    return n < 1 ? 1 : n;
}

static int max_top(const struct wg_list *l)
{
    int m = l->count - wg_list_rows_shown(l);
    return m < 0 ? 0 : m;
}

static void scroll_to(struct wg_list *l, int i)
{
    int shown = wg_list_rows_shown(l);
    if (i < l->top)
        l->top = i;
    else if (i >= l->top + shown)
        l->top = i - shown + 1;
    if (l->top > max_top(l))
        l->top = max_top(l);
    if (l->top < 0)
        l->top = 0;
}

void wg_list_select(struct wg_list *l, int i)
{
    if (i >= l->count)
        i = l->count - 1;
    if (i < -1)
        i = -1;
    if (i >= 0)
        scroll_to(l, i);
    l->selected = i;
    l->dirty = 1;
}

void wg_list_set_count(struct wg_list *l, int count)
{
    l->count = count;
    if (l->selected >= count)
        l->selected = count - 1;
    if (l->hover >= count)
        l->hover = -1;
    if (l->top > max_top(l))
        l->top = max_top(l);
    l->dirty = 1;
}

static void draw_row_icon(struct gfx_surface *s, enum wg_icon icon, int x, int cy)
{
    switch (icon) {
    case WG_ICON_FOLDER:
        gfx_fill_round_rect(s, x, cy - 8, 9, 4, 1, 0x3584e4);        /* tab */
        gfx_fill_round_rect(s, x, cy - 6, 20, 14, 2, 0x3584e4);      /* back */
        gfx_fill_round_rect(s, x, cy - 3, 20, 11, 2, 0x62a0ea);      /* front */
        break;
    case WG_ICON_FILE:
        gfx_fill_round_rect(s, x + 3, cy - 9, 14, 18, 2, 0xdeddda);
        gfx_fill_rect(s, x + 6, cy - 3, 8, 1, 0x9a9996);             /* lines of text */
        gfx_fill_rect(s, x + 6, cy, 8, 1, 0x9a9996);
        gfx_fill_rect(s, x + 6, cy + 3, 5, 1, 0x9a9996);
        break;
    case WG_ICON_NONE:
        break;
    }
}

void wg_list_draw(struct gfx_surface *s, struct wg_list *l)
{
    const struct theme *t = T();
    uint32_t under = under_or_window(l->under);
    /* Adwaita's card: white 8% over the window in the dark style; the view
     * colour (white) in the light one. */
    uint32_t card = t->light ? t->view_bg : gfx_mix(WHITE, under, A_CARD);
    int shown = wg_list_rows_shown(l), card_h = shown * t->row_h;
    struct gfx_surface c = gfx_sub(s, (struct gfx_rect){ l->r.x, l->r.y, l->r.w, card_h });
    int scrollbar = l->count > shown;
    int text_right = c.w - 14 - (scrollbar ? 10 : 0);

    gfx_fill_rect(s, l->r.x, l->r.y, l->r.w, l->r.h, under);
    gfx_fill_rect(&c, 0, 0, c.w, c.h, card);
    for (int k = 0; k < shown && l->top + k < l->count; k++) {
        int i = l->top + k, y = k * t->row_h, cy = y + t->row_h / 2;
        struct wg_row row = { 0, 0, WG_ICON_NONE };
        l->row(l->ctx, i, &row);
        if (i == l->selected)
            gfx_fill_rect(&c, 0, y, c.w, t->row_h, gfx_mix(t->accent_bg, card, A_SELECTED));
        else if (i == l->hover)
            gfx_fill_rect(&c, 0, y, c.w, t->row_h, gfx_mix(t->ink, card, A_ROW_HOVER));
        if (k)
            gfx_fill_rect(&c, 0, y, c.w, 1, gfx_mix(0, card, t->light ? 24 : A_SEPARATOR));
        int x = 14;
        if (row.icon != WG_ICON_NONE) {
            draw_row_icon(&c, row.icon, x, cy);
            x += 32;
        }
        int dw = row.detail ? text_width(row.detail, TEXT_BODY) : 0, ty = cy - text_height(TEXT_BODY) / 2;
        if (row.detail)
            text_draw(&c, text_right - dw, ty, row.detail, TEXT_BODY, t->fg_dim);
        text_draw_fit(&c, x, ty, row.name ? row.name : "", text_right - x - (dw ? dw + 16 : 0),
                      TEXT_BODY, t->fg);
    }
    if (scrollbar) {            /* a thin overlay scrollbar, like GTK's */
        int track = card_h - 8, thumb = track * shown / l->count;
        if (thumb < 16)
            thumb = 16;
        int y = 4 + (track - thumb) * l->top / max_top(l);
        gfx_fill_round_rect(&c, c.w - 8, y, 4, thumb, 2, gfx_mix(t->ink, card, 110));
    }
    gfx_round_corners(s, l->r.x, l->r.y, l->r.w, card_h, 12, under);
    l->dirty = 0;
}

enum wg_list_result wg_list_pointer(struct wg_list *l, const struct wg_pointer *p)
{
    const struct theme *t = T();
    int shown = wg_list_rows_shown(l);
    struct gfx_rect card = { l->r.x, l->r.y, l->r.w, shown * t->row_h };
    int row = -1;
    if (inside(card, p->x, p->y)) {
        row = l->top + (p->y - l->r.y) / t->row_h;
        if (row >= l->count)
            row = -1;
    }
    if (row != l->hover && !p->held) {
        l->hover = row;
        l->dirty = 1;
    }
    if (!p->down || !inside(card, p->x, p->y))
        return WG_LIST_NONE;

    if (l->count > shown && p->x >= card.x + card.w - 14) {    /* the scrollbar: page */
        int before = l->top;
        int thumb_mid = card.h * l->top / l->count + card.h * shown / l->count / 2;
        l->top += p->y - card.y < thumb_mid ? -shown : shown;
        if (l->top > max_top(l))
            l->top = max_top(l);
        if (l->top < 0)
            l->top = 0;
        l->dirty = 1;
        return l->top != before ? WG_LIST_CHANGED : WG_LIST_NONE;
    }
    if (row < 0)
        return WG_LIST_NONE;
    int twice = row == l->last_click_row && p->time_ms - l->last_click_ms <= WG_DOUBLE_CLICK_MS;
    l->last_click_row = twice ? -1 : row;       /* a third click starts over */
    l->last_click_ms = p->time_ms;
    if (row != l->selected)
        wg_list_select(l, row);
    return twice ? WG_LIST_ACTIVATE : WG_LIST_CHANGED;
}

enum wg_list_result wg_list_key(struct wg_list *l, const struct key_event *k)
{
    if (!k->pressed || !l->count)
        return WG_LIST_NONE;
    int page = wg_list_rows_shown(l) - 1, i = l->selected;
    switch (k->key) {
    case KEY_UP:       i = i < 0 ? l->count - 1 : i - 1; break;
    case KEY_DOWN:     i = i + 1; break;
    case KEY_HOME:     i = 0; break;
    case KEY_END:      i = l->count - 1; break;
    case KEY_PAGEUP:   i = i - (page > 0 ? page : 1); break;
    case KEY_PAGEDOWN: i = i + (page > 0 ? page : 1); break;
    case KEY_ENTER:    return l->selected >= 0 ? WG_LIST_ACTIVATE : WG_LIST_NONE;
    default:           return WG_LIST_NONE;
    }
    if (i < 0)
        i = 0;
    if (i >= l->count)
        i = l->count - 1;
    if (i == l->selected)
        return WG_LIST_NONE;
    wg_list_select(l, i);
    return WG_LIST_CHANGED;
}

/* --- entry ------------------------------------------------------------------ */

/* Keep the cursor in view: scroll is the first character shown. */
static void entry_follow_cursor(struct wg_entry *e)
{
    int room = e->r.w - 22;
    if (e->cursor < e->scroll)
        e->scroll = e->cursor;
    while (e->scroll < e->cursor &&
           text_width_n(e->text + e->scroll, e->cursor - e->scroll, TEXT_BODY) > room)
        e->scroll++;
    if (e->scroll < 0)
        e->scroll = 0;
}

void wg_entry_set(struct wg_entry *e, const char *text)
{
    int n = 0;
    for (; text && text[n] && n < WG_TEXT_MAX - 1; n++)
        e->text[n] = text[n];
    e->text[n] = '\0';
    e->len = e->cursor = n;
    e->scroll = 0;
    entry_follow_cursor(e);
    e->dirty = 1;
}

void wg_entry_draw(struct gfx_surface *s, struct wg_entry *e)
{
    const struct theme *t = T();
    uint32_t under = under_or_window(e->under);
    uint32_t bg = gfx_mix(t->ink, under, A_BUTTON);
    gfx_fill_rect(s, e->r.x, e->r.y, e->r.w, e->r.h, under);
    if (e->focused) {           /* Adwaita's focus ring: accent at 50%, 2 px */
        gfx_fill_round_rect(s, e->r.x, e->r.y, e->r.w, e->r.h, t->radius + 2,
                            gfx_mix(t->accent_bg, under, 128));
        gfx_fill_round_rect(s, e->r.x + 2, e->r.y + 2, e->r.w - 4, e->r.h - 4, t->radius, bg);
    } else {
        gfx_fill_round_rect(s, e->r.x, e->r.y, e->r.w, e->r.h, t->radius, bg);
    }
    struct gfx_surface in = gfx_sub(s, (struct gfx_rect){ e->r.x + 10, e->r.y, e->r.w - 20, e->r.h });
    int h = text_height(TEXT_BODY), y = (e->r.h - h) / 2;
    uint32_t fg = e->disabled ? t->fg_dim : t->fg;
    text_draw(&in, 0, y, e->text + e->scroll, TEXT_BODY, fg);
    if (e->focused) {           /* the text cursor (steady: there's no timer to blink it) */
        int cx = text_width_n(e->text + e->scroll, e->cursor - e->scroll, TEXT_BODY);
        gfx_fill_rect(&in, cx, y, 1, h, t->fg);
    }
    e->dirty = 0;
}

enum wg_entry_result wg_entry_key(struct wg_entry *e, const struct key_event *k)
{
    if (!e->focused || e->disabled || !k->pressed)
        return WG_ENTRY_NONE;
    switch (k->key) {
    case KEY_ENTER:     return WG_ENTRY_ACTIVATE;
    case KEY_ESC:       return WG_ENTRY_CANCEL;
    case KEY_LEFT:      if (e->cursor > 0) e->cursor--; break;
    case KEY_RIGHT:     if (e->cursor < e->len) e->cursor++; break;
    case KEY_HOME:      e->cursor = 0; break;
    case KEY_END:       e->cursor = e->len; break;
    case KEY_BACKSPACE:
    case KEY_DELETE:
        if (k->key == KEY_BACKSPACE) {  /* Backspace = step left, then Delete */
            if (e->cursor == 0)
                return WG_ENTRY_NONE;
            e->cursor--;
        }
        if (e->cursor == e->len)
            return WG_ENTRY_NONE;
        for (int i = e->cursor; i < e->len; i++)
            e->text[i] = e->text[i + 1];
        e->len--;
        break;
    default:
        if (k->ascii < 0x20 || k->ascii > 0x7e || (k->mods & (MOD_CTRL | MOD_ALT)) ||
            e->len == WG_TEXT_MAX - 1)
            return WG_ENTRY_NONE;
        for (int i = e->len; i >= e->cursor; i--)
            e->text[i + 1] = e->text[i];
        e->text[e->cursor++] = (char)k->ascii;
        e->len++;
        break;
    }
    entry_follow_cursor(e);
    e->dirty = 1;
    return WG_ENTRY_CHANGED;
}

enum wg_entry_result wg_entry_pointer(struct wg_entry *e, const struct wg_pointer *p)
{
    if (!p->down || e->disabled || !inside(e->r, p->x, p->y))
        return WG_ENTRY_NONE;
    int c = e->scroll + text_index_at(e->text + e->scroll, TEXT_BODY, p->x - e->r.x - 10);
    e->cursor = c < 0 ? 0 : c > e->len ? e->len : c;
    e->focused = 1;
    e->dirty = 1;
    return WG_ENTRY_CHANGED;
}

/* --- dialog ------------------------------------------------------------------ */

#define DLG_PAD 24
#define DLG_W   420
#define DLG_LINE (text_height(TEXT_BODY) + 2)

/* Word-wrap the body to max_w pixels: calls out(line, len) per line,
 * returns the line count. */
static int wrap(const char *text, int max_w,
                void (*out)(void *, const char *, int, int), void *ctx)
{
    int lines = 0;
    while (text && *text) {
        int n = length(text), cut = n;
        if (text_width(text, TEXT_BODY) > max_w) {
            cut = 0;
            while (cut < n && text_width_n(text, cut + 1, TEXT_BODY) <= max_w)
                cut++;
            int word = cut;
            while (word > 0 && text[word] != ' ')
                word--;
            if (word > 0)
                cut = word;             /* break at the last space that fits */
            if (cut == 0)
                cut = 1;
        }
        if (out)
            out(ctx, text, cut, lines);
        lines++;
        text += cut;
        while (*text == ' ')
            text++;
    }
    return lines;
}

static int body_width(const struct wg_dialog *d)
{
    return d->r.w - 2 * DLG_PAD;
}

void wg_dialog_init(struct wg_dialog *d, const char *title, const char *body,
                    const char *action, enum wg_button_style action_style, int has_entry)
{
    *d = (struct wg_dialog){ .title = title, .has_entry = has_entry, .nbuttons = 2 };
    wg_dialog_set_body(d, body, 0);
    d->buttons[0] = (struct wg_button){ .label = "Cancel", .style = WG_BUTTON_NORMAL };
    d->buttons[1] = (struct wg_button){ .label = action, .style = action_style };
    if (has_entry) {
        wg_entry_set(&d->entry, "");
        d->entry.focused = 1;
    }
}

void wg_dialog_set_body(struct wg_dialog *d, const char *body, int error)
{
    int n = 0;
    for (; body && body[n] && n < (int)sizeof(d->body) - 1; n++)
        d->body[n] = body[n];
    d->body[n] = '\0';
    d->body_error = error;
    d->dirty = 1;
}

void wg_dialog_layout(struct wg_dialog *d, int w, int h)
{
    const struct theme *t = T();
    int dw = w - 32 < DLG_W ? w - 32 : DLG_W;
    d->r.w = dw;
    int lines = wrap(d->body, body_width(d), 0, 0);
    if (lines < 1)
        lines = 1;
    int dh = DLG_PAD + text_height(TEXT_HEADING) + 10 + lines * DLG_LINE + (d->has_entry ? 12 + 34 : 0) +
             24 + t->button_size + DLG_PAD;
    d->r = (struct gfx_rect){ (w - dw) / 2, (h - dh) / 2, dw, dh };
    int y = d->r.y + dh - DLG_PAD - t->button_size;
    int bw = (dw - 2 * DLG_PAD - 12 * (d->nbuttons - 1)) / d->nbuttons;
    for (int i = 0; i < d->nbuttons; i++) {
        d->buttons[i].r = (struct gfx_rect){ d->r.x + DLG_PAD + i * (bw + 12), y, bw, t->button_size };
        d->buttons[i].under = t->dialog_bg;
    }
    if (d->has_entry) {
        d->entry.r = (struct gfx_rect){ d->r.x + DLG_PAD, y - 24 - 34, dw - 2 * DLG_PAD, 34 };
        d->entry.under = t->dialog_bg;
        entry_follow_cursor(&d->entry);
    }
}

struct body_ctx {
    struct gfx_surface *s;
    int cx, y;
    uint32_t colour;
};

static void draw_body_line(void *ctx, const char *text, int len, int line)
{
    struct body_ctx *b = ctx;
    int x = b->cx - text_width_n(text, len, TEXT_BODY) / 2;
    text_draw_n(b->s, x, b->y + line * DLG_LINE, text, len, TEXT_BODY, b->colour);
}

void wg_dialog_draw(struct gfx_surface *s, struct wg_dialog *d)
{
    const struct theme *t = T();
    int cx = d->r.x + d->r.w / 2, y = d->r.y + DLG_PAD;
    gfx_fill_round_rect(s, d->r.x, d->r.y, d->r.w, d->r.h, 12, t->dialog_bg);
    wg_label_centred(s, cx, y, d->title, WG_TEXT_HEADING);
    y += text_height(TEXT_HEADING) + 10;
    struct body_ctx b = { s, cx, y, d->body_error ? t->destructive : t->fg_dim };
    wrap(d->body, body_width(d), draw_body_line, &b);
    if (d->has_entry)
        wg_entry_draw(s, &d->entry);
    for (int i = 0; i < d->nbuttons; i++)
        wg_button_draw(s, &d->buttons[i]);
    d->dirty = 0;
}

void wg_dialog_show(struct gfx_surface *s, struct wg_dialog *d)
{
    gfx_darken(s, 0, 0, s->w, s->h, A_DIM_BEHIND);
    wg_dialog_draw(s, d);
}

int wg_dialog_pointer(struct wg_dialog *d, const struct wg_pointer *p)
{
    int chosen = WG_DIALOG_NONE;
    for (int i = 0; i < d->nbuttons; i++)
        if (wg_button_pointer(&d->buttons[i], p))
            chosen = i;
    if (d->has_entry && wg_entry_pointer(&d->entry, p) != WG_ENTRY_NONE)
        d->dirty = 1;
    for (int i = 0; i < d->nbuttons; i++)
        if (d->buttons[i].dirty)
            d->dirty = 1;
    return chosen;
}

int wg_dialog_key(struct wg_dialog *d, const struct key_event *k)
{
    if (!k->pressed)
        return WG_DIALOG_NONE;
    if (k->key == KEY_ESC)
        return 0;
    if (k->key == KEY_ENTER)
        return d->nbuttons - 1;
    if (d->has_entry && wg_entry_key(&d->entry, k) == WG_ENTRY_CHANGED)
        d->dirty = 1;
    return WG_DIALOG_NONE;
}
