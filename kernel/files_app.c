/* LiteKern X — Files, for now a demo on a folder kept in memory (Phase 2 §4).
 *
 * It exercises every widget (list, buttons, entry, dialogs) the way the real
 * Files will: create, rename and delete files and folders. Storage (§5a)
 * swaps the in-memory folder for the USB stick's FAT32 partition, and §5
 * moves the app to ring 3; the UI stays. Actions are logged ("files: ...")
 * for the tests. */
#include "kernel/apps.h"
#include "kernel/font.h"
#include "kernel/printk.h"
#include "kernel/theme.h"
#include "kernel/widget.h"

#define MAX_ITEMS 64
#define LIST_MAX_W 720
#define BAR_GAP 16

struct item {
    char name[WG_TEXT_MAX];
    char detail[16];
    int folder;
};

static struct item items[MAX_ITEMS];
static int nitems, seeded;

static struct wg_list list;
static struct wg_button b_new_folder, b_new_file, b_rename, b_delete;
static struct wg_button *const bar[] = { &b_new_folder, &b_new_file, &b_rename, &b_delete };

enum action { NONE, NEW_FILE, NEW_FOLDER, RENAME, DELETE };
static struct wg_dialog dialog;
static enum action dialog_for;      /* NONE: no dialog open */
static char dialog_text[96];

/* --- the in-memory folder --------------------------------------------------- */

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

/* FAT names are case-insensitive: compare that way. */
static int name_cmp(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b))
        a++, b++;
    return lower(*a) - lower(*b);
}

static int identical(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static void copy(char *dst, const char *src, int max)
{
    int i = 0;
    for (; src[i] && i < max - 1; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static char *append(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = '\0';
    return p;
}

static void size_text(char *out, uint32_t bytes)
{
    char digits[12];
    int n = 0;
    const char *unit = bytes == 0 ? "Empty" : bytes < 1024 ? " bytes" : " KB";
    uint32_t v = bytes < 1024 ? bytes : (bytes + 512) / 1024;     /* nearest KB */
    if (!bytes) {
        append(out, unit);
        return;
    }
    do
        digits[n++] = (char)('0' + v % 10);
    while (v /= 10);
    while (n)
        *out++ = digits[--n];
    append(out, unit);
}

/* Folders first, then by name: like GNOME Files. */
static int before(const struct item *a, const struct item *b)
{
    if (a->folder != b->folder)
        return a->folder;
    return name_cmp(a->name, b->name) < 0;
}

static int find(const char *name)
{
    for (int i = 0; i < nitems; i++)
        if (!name_cmp(items[i].name, name))
            return i;
    return -1;
}

/* Insert keeping the order; returns its index. */
static int add(const char *name, int folder, uint32_t size)
{
    struct item it = { .folder = folder };
    copy(it.name, name, sizeof(it.name));
    if (folder)
        append(it.detail, "Folder");
    else
        size_text(it.detail, size);
    int i = nitems;
    while (i > 0 && before(&it, &items[i - 1])) {
        items[i] = items[i - 1];
        i--;
    }
    items[i] = it;
    nitems++;
    return i;
}

static void remove_at(int i)
{
    for (; i < nitems - 1; i++)
        items[i] = items[i + 1];
    nitems--;
}

static void seed(void)
{
    static const struct { const char *name; int folder; uint32_t size; } start[] = {
        { "Documents", 1, 0 }, { "Music", 1, 0 }, { "Pictures", 1, 0 },
        { "notes.txt", 0, 2048 }, { "todo.txt", 0, 512 }, { "kernel.bin", 0, 28672 },
        { "readme.md", 0, 3100 },
    };
    for (unsigned i = 0; i < sizeof(start) / sizeof(start[0]); i++)
        add(start[i].name, start[i].folder, start[i].size);
    seeded = 1;
}

/* A name FAT32 can store: not empty, no \ / : * ? " < > |, not "." or "..",
 * no leading or trailing space. NULL if fine, else why not. */
static const char *bad_name(const char *name)
{
    int n = 0;
    for (; name[n]; n++)
        for (const char *c = "\\/:*?\"<>|"; *c; c++)
            if (name[n] == *c)
                return "Names can't contain \\ / : * ? \" < > |";
    if (n == 0)
        return "Type a name.";
    if (name[0] == ' ' || name[n - 1] == ' ')
        return "Names can't start or end with a space.";
    if (!name_cmp(name, ".") || !name_cmp(name, ".."))
        return "That name is reserved.";
    return 0;
}

/* --- drawing ------------------------------------------------------------------ */

static void row(void *ctx, int i, struct wg_row *out)
{
    (void)ctx;
    out->name = items[i].name;
    out->detail = items[i].detail;
    out->icon = items[i].folder ? WG_ICON_FOLDER : WG_ICON_FILE;
}

static void layout(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *c = wm_content();
    int w = c->w - 2 * t->margin * 2 < LIST_MAX_W ? c->w - 2 * t->margin * 2 : LIST_MAX_W;
    int x = (c->w - w) / 2, bar_y = c->h - BAR_GAP - t->button_size;
    int top = 24 + FONT_H + 16;
    list.r = (struct gfx_rect){ x, top, w, (bar_y - BAR_GAP - top) / t->row_h * t->row_h };

    /* The action bar, under the list: creating on the left, the selection's
     * actions on the right. */
    int lx = x, rx = x + w;
    for (int i = 0; i < 4; i++) {
        struct wg_button *b = bar[i];
        int bw = wg_button_width(b->label);
        if (i < 2) {
            b->r = (struct gfx_rect){ lx, bar_y, bw, t->button_size };
            lx += bw + t->spacing * 2;
        } else {
            b->r = (struct gfx_rect){ rx - bw, bar_y, bw, t->button_size };
            rx -= bw + t->spacing * 2;
        }
    }
}

static void update_buttons(void)
{
    wg_button_set_disabled(&b_rename, list.selected < 0);
    wg_button_set_disabled(&b_delete, list.selected < 0);
}

static void draw_all(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *c = wm_content();
    gfx_fill_rect(c, 0, 0, c->w, c->h, t->window_bg);
    wg_label_centred(c, c->w / 2, 24,
                     "Demo folder, kept in memory until disk support arrives: changes are "
                     "lost at restart.", WG_TEXT_DIM);
    update_buttons();
    wg_list_draw(c, &list);
    for (int i = 0; i < 4; i++)
        wg_button_draw(c, bar[i]);
    if (dialog_for != NONE) {
        wg_dialog_layout(&dialog, c->w, c->h);
        wg_dialog_show(c, &dialog);
    }
    wm_damage(0, 0, c->w, c->h);
    wm_present();
}

/* Redraw what input changed. */
static void draw_dirty(void)
{
    struct gfx_surface *c = wm_content();
    if (dialog_for != NONE) {
        if (dialog.dirty || dialog.entry.dirty) {
            wg_dialog_draw(c, &dialog);
            wm_damage(dialog.r.x, dialog.r.y, dialog.r.w, dialog.r.h);
        }
    } else {
        update_buttons();
        if (list.dirty) {
            wg_list_draw(c, &list);
            wm_damage(list.r.x, list.r.y, list.r.w, list.r.h);
        }
        for (int i = 0; i < 4; i++)
            if (bar[i]->dirty) {
                wg_button_draw(c, bar[i]);
                wm_damage(bar[i]->r.x, bar[i]->r.y, bar[i]->r.w, bar[i]->r.h);
            }
    }
    wm_present();
}

/* --- actions ------------------------------------------------------------------ */

static void open_dialog(enum action a)
{
    int sel = list.selected;
    if ((a == RENAME || a == DELETE) && sel < 0)
        return;
    const char *what = sel >= 0 ? items[sel].name : "";
    switch (a) {
    case NEW_FILE:
        wg_dialog_init(&dialog, "New File", "Name the new file.", "Create", WG_BUTTON_SUGGESTED, 1);
        break;
    case NEW_FOLDER:
        wg_dialog_init(&dialog, "New Folder", "Name the new folder.", "Create",
                       WG_BUTTON_SUGGESTED, 1);
        break;
    case RENAME:            /* dialog_text: the body is copied, the title isn't */
        append(append(append(dialog_text, "Rename \""), what), "\" to:");
        wg_dialog_init(&dialog, items[sel].folder ? "Rename Folder" : "Rename File", dialog_text,
                       "Rename", WG_BUTTON_SUGGESTED, 1);
        wg_entry_set(&dialog.entry, what);
        break;
    case DELETE:
        append(append(append(dialog_text, "Delete \""), what), "\"?");
        wg_dialog_init(&dialog, dialog_text, "It will be gone for good.", "Delete",
                       WG_BUTTON_DESTRUCTIVE, 0);
        break;
    case NONE:
        return;
    }
    dialog_for = a;
    draw_all();
}

static void close_dialog(void)
{
    dialog_for = NONE;
    draw_all();
}

/* The dialog's action button (or Enter): 1 if done, 0 to keep it open. */
static int confirm(void)
{
    const char *name = dialog.entry.text;
    int sel = list.selected;
    if (dialog_for == DELETE) {
        kprintf("files: delete %s\n", items[sel].name);
        remove_at(sel);
        wg_list_set_count(&list, nitems);
        wg_list_select(&list, sel < nitems ? sel : nitems - 1);
        return 1;
    }
    const char *why = bad_name(name);
    int same = find(name);
    if (!why && same >= 0 && !(dialog_for == RENAME && same == sel))
        why = "Something with that name is already here.";
    if (why) {
        wg_dialog_set_body(&dialog, why, 1);
        return 0;
    }
    if (dialog_for == RENAME) {
        if (!identical(items[sel].name, name)) {       /* case-only changes count */
            struct item it = items[sel];
            kprintf("files: rename %s -> %s\n", it.name, name);
            remove_at(sel);
            int i = add(name, it.folder, 0);
            copy(items[i].detail, it.detail, sizeof(items[i].detail));
            wg_list_set_count(&list, nitems);
            wg_list_select(&list, i);
        }
        return 1;
    }
    if (nitems == MAX_ITEMS) {
        wg_dialog_set_body(&dialog, "This demo folder is full.", 1);
        return 0;
    }
    kprintf("files: create %s%s\n", dialog_for == NEW_FOLDER ? "folder " : "", name);
    int i = add(name, dialog_for == NEW_FOLDER, 0);
    wg_list_set_count(&list, nitems);
    wg_list_select(&list, i);
    return 1;
}

static void dialog_choice(int choice)
{
    if (choice == WG_DIALOG_NONE)
        return;
    if (choice == 0 || confirm())
        close_dialog();
    else
        draw_dirty();       /* the error message */
}

/* --- the app ------------------------------------------------------------------ */

void files_open(void)
{
    if (!seeded)
        seed();
    b_new_folder = (struct wg_button){ .label = "New Folder" };
    b_new_file = (struct wg_button){ .label = "New File", .style = WG_BUTTON_SUGGESTED };
    b_rename = (struct wg_button){ .label = "Rename" };
    b_delete = (struct wg_button){ .label = "Delete", .style = WG_BUTTON_DESTRUCTIVE };
    list = (struct wg_list){ .count = nitems, .selected = -1, .hover = -1, .row = row,
                             .last_click_row = -1 };
    dialog_for = NONE;
    layout();
    draw_all();
}

static void key(const struct key_event *k)
{
    if (dialog_for != NONE) {
        dialog_choice(wg_dialog_key(&dialog, k));
        if (dialog_for != NONE)
            draw_dirty();
        return;
    }
    if (k->key == 0x031 && (k->mods & MOD_CTRL))            /* Ctrl+N */
        open_dialog(NEW_FILE);
    else if (k->key == KEY_F1 + 1)                          /* F2, as in GNOME */
        open_dialog(RENAME);
    else if (k->key == KEY_DELETE)
        open_dialog(DELETE);
    else if (wg_list_key(&list, k) == WG_LIST_ACTIVATE)
        kprintf("files: open %s (opening arrives with disk support)\n", items[list.selected].name);
    if (dialog_for == NONE)
        draw_dirty();
}

static void pointer(const struct wm_event *ev)
{
    struct wg_pointer p = wg_pointer_make(ev->x, ev->y, ev->buttons, ev->changed, ev->time_ms);
    if (dialog_for != NONE) {
        dialog_choice(wg_dialog_pointer(&dialog, &p));
        if (dialog_for != NONE)
            draw_dirty();
        return;
    }
    if (wg_list_pointer(&list, &p) == WG_LIST_ACTIVATE)
        kprintf("files: open %s (opening arrives with disk support)\n", items[list.selected].name);
    enum action a = NONE;
    if (wg_button_pointer(&b_new_folder, &p)) a = NEW_FOLDER;
    if (wg_button_pointer(&b_new_file, &p))   a = NEW_FILE;
    if (wg_button_pointer(&b_rename, &p))     a = RENAME;
    if (wg_button_pointer(&b_delete, &p))     a = DELETE;
    if (a != NONE)
        open_dialog(a);
    else
        draw_dirty();
}

void files_event(const struct wm_event *ev)
{
    if (ev->type == WM_EVENT_KEY)
        key(&ev->key);
    else if (ev->type == WM_EVENT_POINTER)
        pointer(ev);
}
