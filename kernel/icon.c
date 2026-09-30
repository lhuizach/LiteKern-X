#include "kernel/icon.h"

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

const struct icon *icon_find(const char *name)
{
    for (int i = 0; i < icon_count; i++)
        if (same(icons[i].name, name))
            return &icons[i];
    return 0;
}
