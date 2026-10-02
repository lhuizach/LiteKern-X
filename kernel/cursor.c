#include "kernel/cursor.h"
#include "kernel/errno.h"
#include "kernel/screen.h"

static const struct cursor_shape *shape;
static int cx, cy;

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

const struct cursor_shape *cursor_find(const char *name)
{
    for (int i = 0; i < cursor_shape_count; i++)
        if (same(cursor_shapes[i].name, name))
            return &cursor_shapes[i];
    return 0;
}

static void place(void)
{
    screen_set_overlay(shape->px[0], shape->w, shape->h, cx - shape->hot_x, cy - shape->hot_y);
    screen_present();
}

int cursor_init(void)
{
    if (!screen_ready() || !(shape = cursor_find("arrow")))
        return -ENODEV;
    struct gfx_surface *s = screen_surface();
    cx = s->w / 2;
    cy = s->h / 2;
    place();
    return 0;
}

void cursor_move_to(int x, int y)
{
    if (!shape)
        return;
    struct gfx_surface *s = screen_surface();
    x = x < 0 ? 0 : x >= s->w ? s->w - 1 : x;
    y = y < 0 ? 0 : y >= s->h ? s->h - 1 : y;
    if (x == cx && y == cy)
        return;
    cx = x;
    cy = y;
    place();
}

int cursor_x(void) { return cx; }
int cursor_y(void) { return cy; }

void cursor_refresh(void)
{
    if (shape)
        place();
}

int cursor_set_shape(const char *name)
{
    const struct cursor_shape *s = cursor_find(name);
    if (!s)
        return -ENODEV;
    if (shape && s != shape) {
        shape = s;
        place();
    }
    return 0;
}

void cursor_hide(void)
{
    shape = 0;                  /* no more moves either */
    screen_set_overlay(0, 0, 0, 0, 0);
}
