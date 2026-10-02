/* LiteKern X — Files, a KERN86 app (ring 3; Phase 2 §5 and §5a).
 *
 * Browse the disks' FAT32 partitions, and create, rename and delete files
 * and folders on them. It starts at Places (the partitions the kernel found)
 * or straight in the only one. Folders open with a double-click or Enter;
 * the header's back button or Backspace goes up. Unsupported partitions are
 * listed but won't open; read-only ones can't be changed.
 *
 * Text files (up to 64 KB) open in a simple viewer: wrapped lines, the arrow
 * keys and Page Up/Down to scroll, Backspace, Esc or the back button to
 * return. Other files say they can't be shown yet.
 *
 * Everything goes through sdk/kern86.h: the window is a canvas it draws with
 * the kernel's widgets, and the disks are reached by path. Actions are
 * logged ("user: files: ...") for the tests. */
#include "kernel/errno.h"
#include "kernel/font.h"
#include "kernel/string.h"
#include "kernel/text.h"
#include "kernel/theme.h"
#include "kernel/widget.h"
#include "sdk/kern86.h"

#define MAX_ITEMS 256
#define MAX_VOLUMES 8
#define LIST_MAX_W 720
#define BAR_GAP 16
#define BACK_ID 1

struct item {
    struct k86_dirent e;
    char detail[40];
    int volume;             /* in Places: which volume */
};

static struct item items[MAX_ITEMS];
static struct k86_dirent listing[MAX_ITEMS];
static int nitems, truncated;

static struct k86_volume volumes[MAX_VOLUMES];
static int nvolumes;
static int vol = -1;                    /* -1: Places */
static char path[K86_PATH_MAX];         /* "/Documents/Work" in vol; "/" at its root */
static int depth;

static struct k86_window win;
static struct gfx_surface canvas;

static char status[96];
static int status_error;

static struct wg_list list;
static struct wg_button b_new_folder, b_new_file, b_rename, b_delete;
static struct wg_button *const bar[] = { &b_new_folder, &b_new_file, &b_rename, &b_delete };

enum action { NONE, NEW_FILE, NEW_FOLDER, RENAME, DELETE };
static struct wg_dialog dialog;
static enum action dialog_for;
static char dialog_text[128];

/* The text viewer (Phase 3 §3). */
#define VIEW_MAX (64 * 1024)
#define VIEW_LINES 8192
static int viewing;
static char view_name[K86_NAME_MAX];
static char view_text[VIEW_MAX + 1];
static int view_len;
static struct { int start, len; } view_line[VIEW_LINES];
static int view_lines, view_top;
static struct gfx_rect view_card;

/* --- text helpers ------------------------------------------------------------ */

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

static void size_text(char *out, int max, uint32_t bytes)
{
    char *end = out + max;
    if (bytes < 1024)
        append(append_num(out, bytes, end), bytes == 1 ? " byte" : " bytes", end);
    else if (bytes < 1024 * 1024)
        append(append_num(out, (bytes + 512) / 1024, end), " KB", end);
    else
        append(append_num(out, (bytes + 512 * 1024) / (1024 * 1024), end), " MB", end);
}

/* Free space from KiB: "512 KB", "63 MB", "12 GB". */
static void free_text(char *out, int max, uint32_t kib)
{
    char *end = out + max;
    if (kib < 1024)
        append(append_num(out, kib, end), " KB", end);
    else if (kib < 1024 * 1024)
        append(append_num(out, (kib + 512) / 1024, end), " MB", end);
    else
        append(append_num(out, (kib + 512 * 1024) / (1024 * 1024), end), " GB", end);
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

static const char *why(int err)
{
    return err == -ENOSPC ? ": the disk or folder is full." : err == -EROFS ? ": this disk is read-only."
         : err == -EEXIST ? ": that name is taken." : ": the disk reported an error.";
}

static void error_status(const char *what, int err)
{
    char *end = status + sizeof(status);
    append(append(status, what, end), why(err), end);
    status_error = 1;
    k86_logf("files: %s failed (%d)", what, err);
}

/* The folder's own name: the last part of the path, or the volume's. */
static const char *folder_name(void)
{
    if (vol < 0)
        return "Places";
    if (depth == 0)
        return volumes[vol].name;
    const char *p = path + strlen(path);
    while (p > path && p[-1] != '/')
        p--;
    return p;
}

/* --- loading ------------------------------------------------------------------ */

static int before(const struct k86_dirent *a, const struct k86_dirent *b)
{
    if (a->is_dir != b->is_dir)
        return a->is_dir;
    return name_cmp(a->name, b->name) < 0;
}

static void add_item(const struct k86_dirent *e)
{
    struct item it;
    memset(&it, 0, sizeof(it));
    it.e = *e;
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
}

static void where(void)
{
    char *end = status + sizeof(status), *p = status;
    if (vol < 0) {
        append(status, "Places: the disks LiteKern X can see", end);
    } else {
        p = append(p, volumes[vol].name, end);
        if (depth)
            p = append(append(p, " ", end), path, end);
        if (truncated)
            append(p, "  (showing the first 256 items)", end);
    }
    status_error = 0;
}

static void load(const char *select)
{
    nitems = truncated = 0;
    if (vol < 0) {
        nvolumes = k86_volumes(volumes, MAX_VOLUMES);
        if (nvolumes > MAX_VOLUMES)
            nvolumes = MAX_VOLUMES;
        for (int i = 0; i < nvolumes; i++) {
            struct item *it = &items[nitems++];
            memset(it, 0, sizeof(*it));
            it->volume = i;
            append(it->e.name, volumes[i].name, it->e.name + sizeof(it->e.name));
            it->e.is_dir = volumes[i].supported;
            char *end = it->detail + sizeof(it->detail);
            if (volumes[i].supported) {
                free_text(it->detail, sizeof(it->detail), volumes[i].free_kib);
                append(it->detail + strlen(it->detail), volumes[i].read_only ? " free, read-only" : " free", end);
            } else {
                append(append(it->detail, "Not supported: ", end), volumes[i].why_not, end);
            }
        }
    } else {
        int n = k86_list(vol, path, listing, MAX_ITEMS);
        if (n < 0) {
            error_status("Reading the folder", n);
        } else {
            truncated = n > MAX_ITEMS;
            for (int i = 0; i < n && i < MAX_ITEMS; i++)
                add_item(&listing[i]);
        }
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

/* --- drawing -------------------------------------------------------------------- */

static void row(void *ctx, int i, struct wg_row *out)
{
    (void)ctx;
    out->name = items[i].e.name;
    out->detail = items[i].detail;
    out->icon = items[i].e.is_dir ? WG_ICON_FOLDER : vol >= 0 ? WG_ICON_FILE : WG_ICON_NONE;
}

static void show(struct gfx_rect r)
{
    k86_present(r.x, r.y, r.w, r.h);
}

static void layout(void)
{
    const struct theme *t = theme_get();
    int w = canvas.w - 4 * t->margin < LIST_MAX_W ? canvas.w - 4 * t->margin : LIST_MAX_W;
    int x = (canvas.w - w) / 2, bar_y = canvas.h - BAR_GAP - t->button_size;
    int top = 20 + wg_label_height(WG_TEXT_BODY) + 12;
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
    return vol >= 0 && !volumes[vol].read_only;
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
    gfx_fill_rect(&canvas, 0, 12, canvas.w, wg_label_height(WG_TEXT_BODY) + 16, t->window_bg);
    wg_label_centred(&canvas, canvas.w / 2, 20, status, status_error ? WG_TEXT_ERROR : WG_TEXT_DIM);
    show((struct gfx_rect){ 0, 12, canvas.w, wg_label_height(WG_TEXT_BODY) + 16 });
}

static void draw_all(void)
{
    const struct theme *t = theme_get();
    gfx_fill_rect(&canvas, 0, 0, canvas.w, canvas.h, t->window_bg);
    wg_label_centred(&canvas, canvas.w / 2, 20, status, status_error ? WG_TEXT_ERROR : WG_TEXT_DIM);
    update_buttons();
    wg_list_draw(&canvas, &list);
    if (!nitems)
        wg_label_centred(&canvas, list.r.x + list.r.w / 2, list.r.y + 60,
                         vol >= 0 ? "This folder is empty." : "No disks found.", WG_TEXT_DIM);
    for (int i = 0; i < 4; i++)
        wg_button_draw(&canvas, bar[i]);
    if (dialog_for != NONE) {
        wg_dialog_layout(&dialog, canvas.w, canvas.h);
        wg_dialog_show(&canvas, &dialog);
    }
    show((struct gfx_rect){ 0, 0, canvas.w, canvas.h });
}

static void draw_dirty(void)
{
    if (dialog_for != NONE) {
        if (dialog.dirty || dialog.entry.dirty) {
            wg_dialog_draw(&canvas, &dialog);
            show(dialog.r);
        }
        return;
    }
    update_buttons();
    if (list.dirty) {
        wg_list_draw(&canvas, &list);
        show(list.r);
    }
    for (int i = 0; i < 4; i++)
        if (bar[i]->dirty) {
            wg_button_draw(&canvas, bar[i]);
            show(bar[i]->r);
        }
}

/* --- moving around ---------------------------------------------------------------- */

static void show_header(void)
{
    struct k86_header h;
    memset(&h, 0, sizeof(h));
    append(h.title, viewing ? view_name : folder_name(), h.title + sizeof(h.title));
    if (viewing || (vol >= 0 && (depth > 0 || nvolumes > 1))) {
        h.nbuttons = 1;
        h.buttons[0].side = 0;
        h.buttons[0].icon = 1;              /* back */
        h.buttons[0].id = BACK_ID;
    }
    k86_header(&h);
}

static void open_volume(int i)
{
    vol = i;
    depth = 0;
    path[0] = '/';
    path[1] = '\0';
    k86_logf("files: open %s", volumes[i].name);
}

static void refresh(const char *select)
{
    status_error = 0;
    load(select);
    show_header();
    draw_all();
}

/* --- the text viewer ------------------------------------------------------------- */

static int view_rows(void)
{
    return (view_card.h - 24) / (text_height(TEXT_BODY) + 2);
}

/* Split the text into lines that fit the card: at newlines, and between
 * words where a line is too long. */
static void view_wrap(void)
{
    int max_w = view_card.w - 40;
    view_lines = 0;
    for (int p = 0; p <= view_len && view_lines < VIEW_LINES;) {
        int end = p;
        while (end < view_len && view_text[end] != '\n')
            end++;
        int line_end = end;
        if (line_end > p && view_text[line_end - 1] == '\r')
            line_end--;
        do {                                    /* one paragraph, wrapped */
            int n = line_end - p;
            if (text_width_n(view_text + p, n, TEXT_BODY) > max_w) {
                int fit = 0;
                while (fit < n && text_width_n(view_text + p, fit + 1, TEXT_BODY) <= max_w)
                    fit++;
                int space = fit;
                while (space > 0 && view_text[p + space] != ' ')
                    space--;
                n = space > 0 ? space : (fit > 0 ? fit : 1);
            }
            view_line[view_lines].start = p;
            view_line[view_lines].len = n;
            view_lines++;
            p += n;
            if (p < line_end && view_text[p] == ' ')
                p++;                            /* the space it broke at starts no line */
        } while (p < line_end && view_lines < VIEW_LINES);
        p = end + 1;
    }
}

static void draw_view(void)
{
    const struct theme *t = theme_get();
    gfx_fill_rect(&canvas, 0, 0, canvas.w, canvas.h, t->window_bg);
    struct gfx_rect c = view_card;
    gfx_fill_round_rect(&canvas, c.x, c.y, c.w, c.h, 12, t->light ? t->view_bg : gfx_mix(t->ink, t->window_bg, 20));
    int lh = text_height(TEXT_BODY) + 2, rows = view_rows();
    struct gfx_surface inner = gfx_sub(&canvas, (struct gfx_rect){ c.x + 20, c.y + 12, c.w - 40, c.h - 24 });
    for (int k = 0; k < rows && view_top + k < view_lines; k++) {
        int i = view_top + k;
        text_draw_n(&inner, 0, k * lh, view_text + view_line[i].start, view_line[i].len, TEXT_BODY, t->fg);
    }
    if (view_lines > rows) {                    /* where we are in it */
        int track = c.h - 16, thumb = track * rows / view_lines;
        if (thumb < 16)
            thumb = 16;
        int y = c.y + 8 + (track - thumb) * view_top / (view_lines - rows);
        gfx_fill_round_rect(&canvas, c.x + c.w - 10, y, 4, thumb, 2, gfx_mix(t->ink, t->window_bg, 110));
    }
    char info[64];
    char *end = info + sizeof(info);
    size_text(info, sizeof(info), (uint32_t)view_len);
    append(info + strlen(info), view_lines == 1 ? ", 1 line" : "", end);
    wg_label_centred(&canvas, canvas.w / 2, 20, info, WG_TEXT_DIM);
    show((struct gfx_rect){ 0, 0, canvas.w, canvas.h });
}

static void view_scroll(int to)
{
    int max = view_lines - view_rows();
    if (to > max)
        to = max;
    if (to < 0)
        to = 0;
    if (to != view_top) {
        view_top = to;
        draw_view();
    }
}

/* Open a file: text up to VIEW_MAX is shown; anything else says why not. */
static void open_file(const struct k86_dirent *e)
{
    if (e->size > VIEW_MAX) {
        k86_logf("files: %s is too big to show (%u bytes)", e->name, e->size);
        set_status("Files can show text files up to 64 KB.", 1);
        draw_status();
        return;
    }
    int n = k86_read(vol, path, e->name, view_text, VIEW_MAX);
    if (n < 0) {
        error_status("Reading the file", n);
        draw_status();
        return;
    }
    int odd = 0;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)view_text[i];
        if (c == '\t')
            view_text[i] = ' ';             /* tabs as spaces: the font has no tab stops */
        else if (c < 0x20 && c != '\n' && c != '\r')
            odd++;
    }
    if (odd > n / 20) {
        k86_logf("files: %s isn't text", e->name);
        set_status("This isn't a text file: LiteKern X can't show it yet.", 1);
        draw_status();
        return;
    }
    view_text[n] = '\0';
    view_len = n;
    view_name[0] = '\0';
    append(view_name, e->name, view_name + sizeof(view_name));
    view_card = (struct gfx_rect){ list.r.x, list.r.y, list.r.w, canvas.h - list.r.y - 16 };
    view_wrap();
    view_top = 0;
    viewing = 1;
    k86_logf("files: view %s (%d bytes, %d lines)", e->name, n, view_lines);
    show_header();
    draw_view();
}

static void view_key(const struct key_event *k)
{
    int page = view_rows() - 1;
    switch (k->key) {
    case KEY_UP:       view_scroll(view_top - 1); break;
    case KEY_DOWN:     view_scroll(view_top + 1); break;
    case KEY_PAGEUP:   view_scroll(view_top - page); break;
    case KEY_PAGEDOWN: view_scroll(view_top + page); break;
    case KEY_HOME:     view_scroll(0); break;
    case KEY_END:      view_scroll(view_lines); break;
    case KEY_BACKSPACE:
    case KEY_ESC:
        viewing = 0;
        refresh(view_name);
        break;
    default:
        break;
    }
}

static void activate(int i)
{
    if (i < 0 || i >= nitems)
        return;
    if (vol < 0) {
        int v = items[i].volume;
        if (!volumes[v].supported) {
            k86_logf("files: %s is not supported (%s)", volumes[v].name, volumes[v].why_not);
            set_status("LiteKern X can only open FAT32 disks; this one is left alone.", 1);
            draw_status();
            return;
        }
        open_volume(v);
    } else if (items[i].e.is_dir) {
        char *end = path + sizeof(path);
        if (strlen(path) + strlen(items[i].e.name) + 2 > sizeof(path)) {
            set_status("Folders that deep aren't supported yet.", 1);
            draw_status();
            return;
        }
        append(append(path + strlen(path), depth ? "/" : "", end), items[i].e.name, end);
        depth++;
        k86_logf("files: open folder %s", items[i].e.name);
    } else {
        open_file(&items[i].e);
        return;
    }
    refresh(0);
}

static void go_back(void)
{
    char from[K86_NAME_MAX];
    if (viewing) {
        viewing = 0;
        refresh(view_name);
        return;
    }
    if (vol < 0)
        return;
    from[0] = '\0';
    append(from, folder_name(), from + sizeof(from));
    if (depth > 0) {
        char *slash = path + strlen(path);
        while (slash > path && *slash != '/')
            slash--;
        if (slash == path)
            slash[1] = '\0';                /* back to "/" */
        else
            *slash = '\0';
        depth--;
    } else if (nvolumes > 1) {
        vol = -1;
    } else {
        return;
    }
    refresh(from);
}

/* --- actions ------------------------------------------------------------------------- */

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

/* A name the kernel would refuse, said the way a person would. */
static const char *bad_name(const char *name)
{
    int n = (int)strlen(name);
    for (int i = 0; i < n; i++) {
        if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] > 0x7e)
            return "Names can only use plain letters, digits and punctuation.";
        if (strchr("\\/:*?\"<>|", name[i]))
            return "Names can't contain \\ / : * ? \" < > |";
    }
    if (!n)
        return "Type a name.";
    if (name[0] == ' ' || name[n - 1] == ' ' || name[n - 1] == '.')
        return "Names can't start or end with a space, or end with a dot.";
    if (!strcmp(name, ".") || !strcmp(name, ".."))
        return "That name is reserved.";
    return 0;
}

static int confirm(void)
{
    char name[K86_NAME_MAX], old[K86_NAME_MAX];
    int sel = list.selected, err;
    name[0] = old[0] = '\0';
    append(name, dialog.entry.text, name + sizeof(name));
    if (sel >= 0)
        append(old, items[sel].e.name, old + sizeof(old));
    if (dialog_for == DELETE) {
        k86_logf("files: delete %s", old);
        if ((err = k86_delete(vol, path, old)))
            error_status("Deleting", err);
        load(sel + 1 < nitems ? items[sel + 1].e.name : sel > 0 ? items[sel - 1].e.name : 0);
        return 1;
    }
    const char *bad = bad_name(name);
    if (bad) {
        wg_dialog_set_body(&dialog, bad, 1);
        return 0;
    }
    if (dialog_for == RENAME) {
        if (!strcmp(name, old))
            return 1;
        err = k86_rename(vol, path, old, name);
    } else {
        err = k86_create(vol, path, name, dialog_for == NEW_FOLDER);
    }
    if (err == -EEXIST) {
        wg_dialog_set_body(&dialog, "Something with that name is already here.", 1);
        return 0;
    }
    if (err)
        error_status(dialog_for == RENAME ? "Renaming" : "Creating", err);
    else if (dialog_for == RENAME)
        k86_logf("files: rename %s -> %s", old, name);
    else
        k86_logf("files: create %s%s", dialog_for == NEW_FOLDER ? "folder " : "", name);
    load(err && dialog_for == RENAME ? old : name);
    return 1;
}

static void dialog_choice(int choice)
{
    if (choice == WG_DIALOG_NONE)
        return;
    if (choice == 0 || confirm()) {
        dialog_for = NONE;
        draw_all();
    } else {
        draw_dirty();
    }
}

/* --- events ---------------------------------------------------------------------------- */

static void key(const struct key_event *k)
{
    if (viewing) {
        if (k->pressed)
            view_key(k);
        return;
    }
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

static void pointer(const struct k86_event *ev)
{
    if (viewing)
        return;
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

/* The window was resized: everything is laid out again for the new size. */
static void resized(int w, int h)
{
    canvas.w = w;
    canvas.h = h;
    layout();
    if (viewing) {
        int top = view_top;
        view_card = (struct gfx_rect){ list.r.x, list.r.y, list.r.w, canvas.h - list.r.y - 16 };
        view_wrap();
        int max = view_lines - view_rows();
        view_top = top < max ? top : max > 0 ? max : 0;
        draw_view();
    } else {
        draw_all();
    }
}

int main(void)
{
    if (k86_window_open(&win))
        return 1;
    canvas = (struct gfx_surface){ win.canvas, win.w, win.h, win.stride };
    b_new_folder = (struct wg_button){ .label = "New Folder" };
    b_new_file = (struct wg_button){ .label = "New File", .style = WG_BUTTON_SUGGESTED };
    b_rename = (struct wg_button){ .label = "Rename" };
    b_delete = (struct wg_button){ .label = "Delete", .style = WG_BUTTON_DESTRUCTIVE };
    list = (struct wg_list){ .selected = -1, .hover = -1, .row = row, .last_click_row = -1 };
    layout();

    nvolumes = k86_volumes(volumes, MAX_VOLUMES);
    if (nvolumes == 1 && volumes[0].supported)
        open_volume(0);                     /* only one disk: straight in */
    refresh(0);

    for (;;) {
        struct k86_event ev;
        if (k86_wait_event(&ev))
            return 1;
        switch (ev.type) {
        case K86_EVENT_KEY:     key(&ev.key); break;
        case K86_EVENT_POINTER: pointer(&ev); break;
        case K86_EVENT_HEADER:  if (ev.id == BACK_ID) go_back(); break;
        case K86_EVENT_THEME:   if (viewing) draw_view(); else draw_all(); break;
        case K86_EVENT_RESIZE:  resized(ev.x, ev.y); break;
        case K86_EVENT_CLOSE:   return 0;
        default:                break;
        }
    }
}
