/* LiteKern X — Files (Phase 2 §5a): browse the disks' FAT32 partitions, and
 * create, rename and delete files and folders on them.
 *
 * It starts at "Places", the list of partitions kernel/storage.h found (or
 * straight in the only one). Folders open with a double-click or Enter; the
 * header's back button goes up. Unsupported partitions (NTFS, ...) are shown
 * but can't be opened, and read-only ones can't be changed. Actions are
 * logged ("files: ...") for the tests. §5 moves the app to ring 3. */
#include "kernel/apps.h"
#include "kernel/errno.h"
#include "kernel/font.h"
#include "kernel/printk.h"
#include "kernel/storage.h"
#include "kernel/string.h"
#include "kernel/theme.h"
#include "kernel/widget.h"

#define MAX_ITEMS 256
#define MAX_DEPTH 16
#define LIST_MAX_W 720
#define BAR_GAP 16
#define BACK_ID 1

struct item {
    struct fat_entry e;
    char detail[20];
};

static struct item items[MAX_ITEMS];
static int nitems;
static int truncated;           /* the folder has more than MAX_ITEMS entries */

static struct storage *vol;     /* NULL: showing Places */
static struct {
    uint32_t cluster;
    char name[FAT_NAME_MAX];
} path[MAX_DEPTH];
static int depth;               /* path[0] is the volume's root */

static char status[96];         /* under the header: where we are, or what went wrong */
static int status_error;

static struct wg_list list;
static struct wg_button b_new_folder, b_new_file, b_rename, b_delete;
static struct wg_button *const bar[] = { &b_new_folder, &b_new_file, &b_rename, &b_delete };

enum action { NONE, NEW_FILE, NEW_FOLDER, RENAME, DELETE };
static struct wg_dialog dialog;
static enum action dialog_for;
static char dialog_text[128];

/* --- text helpers ---------------------------------------------------------------- */

static char *append(char *p, const char *s, char *end)
{
    while (*s && p < end - 1)
        *p++ = *s++;
    *p = '\0';
    return p;
}

static char *append_num(char *p, uint32_t v, char *end)
{
    char digits[12];
    int n = 0;
    do
        digits[n++] = (char)('0' + v % 10);
    while (v /= 10);
    while (n && p < end - 1)
        *p++ = digits[--n];
    *p = '\0';
    return p;
}

static void size_text(char *out, int max, uint64_t bytes)
{
    char *end = out + max;
    if (bytes < 1024) {
        append(append_num(out, (uint32_t)bytes, end), bytes == 1 ? " byte" : " bytes", end);
    } else if (bytes < 1024 * 1024) {
        append(append_num(out, (uint32_t)((bytes + 512) / 1024), end), " KB", end);
    } else {
        append(append_num(out, (uint32_t)((bytes + 512 * 1024) / (1024 * 1024)), end), " MB", end);
    }
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int name_cmp(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b))
        a++, b++;
    return lower(*a) - lower(*b);
}

static void set_status(const char *text, int error)
{
    status[0] = '\0';
    append(status, text, status + sizeof(status));
    status_error = error;
}

static void error_status(const char *what, int err)
{
    char *end = status + sizeof(status);
    char *p = append(status, what, end);
    append(p, err == -ENOSPC ? ": the disk or folder is full." : err == -EROFS ? ": this disk is read-only."
              : err == -EEXIST ? ": that name is taken." : ": the disk reported an error.", end);
    status_error = 1;
    kprintf("files: %s failed (%s)\n", what, errno_name(err));
}

/* --- loading ----------------------------------------------------------------------- */

static int before(const struct fat_entry *a, const struct fat_entry *b)
{
    if (a->is_dir != b->is_dir)
        return a->is_dir;
    return name_cmp(a->name, b->name) < 0;
}

static int collect(void *ctx, const struct fat_entry *e)
{
    (void)ctx;
    if (nitems == MAX_ITEMS) {
        truncated = 1;
        return 1;
    }
    struct item it = { .e = *e };
    if (e->is_dir)
        append(it.detail, "Folder", it.detail + sizeof(it.detail));
    else
        size_text(it.detail, sizeof(it.detail), e->size);
    int i = nitems++;
    while (i > 0 && before(&it.e, &items[i - 1].e)) {   /* folders first, then by name */
        items[i] = items[i - 1];
        i--;
    }
    items[i] = it;
    return 0;
}

static void where(void)
{
    char *end = status + sizeof(status), *p = status;
    if (!vol) {
        append(status, "Places: the disks LiteKern X can see", end);
    } else {
        p = append(p, vol->name, end);
        for (int i = 1; i <= depth; i++)
            p = append(append(p, " / ", end), path[i].name, end);
        if (truncated)
            append(p, "  (showing the first 256 items)", end);
    }
    status_error = 0;
}

/* Fill items from the current folder (or Places). Keeps the selection on
 * `select` if given. */
static void load(const char *select)
{
    nitems = 0;
    truncated = 0;
    if (!vol) {
        for (int i = 0; i < storage_count() && i < MAX_ITEMS; i++) {
            struct storage *s = storage_get(i);
            struct item *it = &items[nitems++];
            memset(it, 0, sizeof(*it));
            append(it->e.name, s->name, it->e.name + sizeof(it->e.name));
            it->e.is_dir = s->supported;
            if (s->supported) {
                size_text(it->detail, sizeof(it->detail), fat_free_bytes(&s->fat));
                append(it->detail + strlen(it->detail), s->fat.read_only ? " free, read-only" : " free",
                       it->detail + sizeof(it->detail));
            } else {
                append(append(it->detail, "Not supported: ", it->detail + sizeof(it->detail)),
                       s->why_not, it->detail + sizeof(it->detail));
            }
        }
    } else {
        int n = fat_list(&vol->fat, path[depth].cluster, collect, 0);
        if (n < 0)
            error_status("Reading the folder", n);
    }
    wg_list_set_count(&list, nitems);
    list.selected = -1;
    list.top = 0;
    for (int i = 0; select && i < nitems; i++)
        if (!name_cmp(items[i].e.name, select))
            wg_list_select(&list, i);
    if (!status_error)
        where();
}

/* --- drawing ------------------------------------------------------------------------- */

static void row(void *ctx, int i, struct wg_row *out)
{
    (void)ctx;
    out->name = items[i].e.name;
    out->detail = items[i].detail;
    out->icon = items[i].e.is_dir || !vol ? WG_ICON_FOLDER : WG_ICON_FILE;
}

static void layout(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *c = wm_content();
    int w = c->w - 4 * t->margin < LIST_MAX_W ? c->w - 4 * t->margin : LIST_MAX_W;
    int x = (c->w - w) / 2, bar_y = c->h - BAR_GAP - t->button_size;
    int top = 20 + FONT_H + 14;
    list.r = (struct gfx_rect){ x, top, w, (bar_y - BAR_GAP - top) / t->row_h * t->row_h };
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

static int writable(void)
{
    return vol && !vol->fat.read_only;
}

static void update_buttons(void)
{
    wg_button_set_disabled(&b_new_folder, !writable());
    wg_button_set_disabled(&b_new_file, !writable());
    wg_button_set_disabled(&b_rename, !writable() || list.selected < 0);
    wg_button_set_disabled(&b_delete, !writable() || list.selected < 0);
}

static void draw_status(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *c = wm_content();
    gfx_fill_rect(c, 0, 12, c->w, FONT_H + 16, t->window_bg);
    wg_label_centred(c, c->w / 2, 20, status, status_error ? WG_TEXT_ERROR : WG_TEXT_DIM);
    wm_damage(0, 12, c->w, FONT_H + 16);
}

static void draw_all(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *c = wm_content();
    gfx_fill_rect(c, 0, 0, c->w, c->h, t->window_bg);
    draw_status();
    update_buttons();
    wg_list_draw(c, &list);
    if (!nitems)
        wg_label_centred(c, list.r.x + list.r.w / 2, list.r.y + 60,
                         vol ? "This folder is empty." : "No disks found.", WG_TEXT_DIM);
    for (int i = 0; i < 4; i++)
        wg_button_draw(c, bar[i]);
    if (dialog_for != NONE) {
        wg_dialog_layout(&dialog, c->w, c->h);
        wg_dialog_show(c, &dialog);
    }
    wm_damage(0, 0, c->w, c->h);
    wm_present();
}

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

/* --- moving around --------------------------------------------------------------------- */

static void show_header(void)
{
    wm_clear_buttons();
    if (vol && (depth > 0 || storage_count() > 1))
        wm_add_button(WM_LEFT, WM_ICON_BACK, 0, BACK_ID);
    wm_set_title(!vol ? "Places" : depth ? path[depth].name : vol->name);
}

static void open_volume(struct storage *s)
{
    vol = s;
    depth = 0;
    path[0].cluster = fat_root(&s->fat);
    append(path[0].name, s->name, path[0].name + sizeof(path[0].name));
    kprintf("files: open %s\n", s->name);
}

static void activate(int i)
{
    if (i < 0 || i >= nitems)
        return;
    if (!vol) {
        struct storage *s = storage_get(i);
        if (!s || !s->supported) {
            set_status("LiteKern X can only open FAT32 disks; this one is left alone.", 1);
            draw_status();
            wm_present();
            return;
        }
        open_volume(s);
    } else if (items[i].e.is_dir) {
        if (depth == MAX_DEPTH - 1) {
            set_status("Folders that deep aren't supported yet.", 1);
            draw_status();
            wm_present();
            return;
        }
        depth++;
        path[depth].cluster = items[i].e.cluster;
        path[depth].name[0] = '\0';
        append(path[depth].name, items[i].e.name, path[depth].name + sizeof(path[depth].name));
        kprintf("files: open folder %s\n", items[i].e.name);
    } else {
        kprintf("files: open %s (opening files arrives later)\n", items[i].e.name);
        set_status("Opening files arrives with the first apps that can show them.", 0);
        draw_status();
        wm_present();
        return;
    }
    status_error = 0;
    load(0);
    show_header();
    draw_all();
}

static void go_back(void)
{
    char from[FAT_NAME_MAX];
    if (!vol)
        return;
    if (depth > 0) {
        append(from, path[depth].name, from + sizeof(from));
        depth--;
    } else {
        append(from, vol->name, from + sizeof(from));
        vol = 0;
    }
    status_error = 0;
    load(from);
    show_header();
    draw_all();
}

/* --- actions ------------------------------------------------------------------------------ */

static void open_dialog(enum action a)
{
    int sel = list.selected;
    if (!writable() || ((a == RENAME || a == DELETE) && sel < 0))
        return;
    const char *what = sel >= 0 ? items[sel].e.name : "";
    char *end = dialog_text + sizeof(dialog_text);
    switch (a) {
    case NEW_FILE:
        wg_dialog_init(&dialog, "New File", "Name the new file.", "Create", WG_BUTTON_SUGGESTED, 1);
        break;
    case NEW_FOLDER:
        wg_dialog_init(&dialog, "New Folder", "Name the new folder.", "Create", WG_BUTTON_SUGGESTED, 1);
        break;
    case RENAME:            /* the body is copied into the dialog; the title isn't */
        append(append(append(dialog_text, "Rename \"", end), what, end), "\" to:", end);
        wg_dialog_init(&dialog, items[sel].e.is_dir ? "Rename Folder" : "Rename File", dialog_text,
                       "Rename", WG_BUTTON_SUGGESTED, 1);
        wg_entry_set(&dialog.entry, what);
        break;
    case DELETE:
        append(append(append(dialog_text, "Delete \"", end), what, end), "\"?", end);
        wg_dialog_init(&dialog, dialog_text,
                       items[sel].e.is_dir ? "It and everything in it will be gone for good."
                                           : "It will be gone for good.",
                       "Delete", WG_BUTTON_DESTRUCTIVE, 0);
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

/* The dialog's action: 1 when done (the dialog closes), 0 to keep it open. */
static int confirm(void)
{
    char name[FAT_NAME_MAX];
    int sel = list.selected, err;
    name[0] = '\0';
    append(name, dialog.entry.text, name + sizeof(name));
    if (dialog_for == DELETE) {
        kprintf("files: delete %s\n", items[sel].e.name);
        err = fat_delete(&vol->fat, &items[sel].e);
        if (err)
            error_status("Deleting", err);
        load(sel + 1 < nitems ? items[sel + 1].e.name : sel > 0 ? items[sel - 1].e.name : 0);
        return 1;
    }
    const char *why = fat_bad_name(name);
    if (why) {
        wg_dialog_set_body(&dialog, why, 1);
        return 0;
    }
    if (dialog_for == RENAME) {
        char old[FAT_NAME_MAX];
        old[0] = '\0';
        append(old, items[sel].e.name, old + sizeof(old));
        if (!strcmp(name, old))
            return 1;
        err = fat_rename(&vol->fat, &items[sel].e, name);
        if (err == -EEXIST) {
            wg_dialog_set_body(&dialog, "Something with that name is already here.", 1);
            return 0;
        }
        if (err)
            error_status("Renaming", err);
        else
            kprintf("files: rename %s -> %s\n", old, name);
        load(err ? items[sel].e.name : name);
        return 1;
    }
    err = fat_create(&vol->fat, path[depth].cluster, name, dialog_for == NEW_FOLDER, 0);
    if (err == -EEXIST) {
        wg_dialog_set_body(&dialog, "Something with that name is already here.", 1);
        return 0;
    }
    if (err)
        error_status("Creating", err);
    else
        kprintf("files: create %s%s\n", dialog_for == NEW_FOLDER ? "folder " : "", name);
    load(name);
    return 1;
}

static void dialog_choice(int choice)
{
    if (choice == WG_DIALOG_NONE)
        return;
    if (choice == 0 || confirm())
        close_dialog();
    else
        draw_dirty();
}

/* --- the app ---------------------------------------------------------------------------------- */

void files_open(void)
{
    b_new_folder = (struct wg_button){ .label = "New Folder" };
    b_new_file = (struct wg_button){ .label = "New File", .style = WG_BUTTON_SUGGESTED };
    b_rename = (struct wg_button){ .label = "Rename" };
    b_delete = (struct wg_button){ .label = "Delete", .style = WG_BUTTON_DESTRUCTIVE };
    list = (struct wg_list){ .count = 0, .selected = -1, .hover = -1, .row = row, .last_click_row = -1 };
    dialog_for = NONE;
    status_error = 0;
    vol = 0;
    if (storage_count() == 1 && storage_get(0)->supported)
        open_volume(storage_get(0));        /* only one disk: straight in */
    layout();
    load(0);
    show_header();
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
    else if (k->key == KEY_BACKSPACE || (k->key == KEY_LEFT && (k->mods & MOD_ALT)))
        go_back();
    else if (wg_list_key(&list, k) == WG_LIST_ACTIVATE)
        activate(list.selected);
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
    if (wg_list_pointer(&list, &p) == WG_LIST_ACTIVATE) {
        activate(list.selected);
        return;
    }
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
    else if (ev->type == WM_EVENT_HEADER && ev->id == BACK_ID)
        go_back();
}
