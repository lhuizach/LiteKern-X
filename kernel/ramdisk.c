#include "kernel/ramdisk.h"
#include "kernel/errno.h"
#include "kernel/string.h"

/* kernel/ramdisk_data.asm */
extern const uint8_t ramdisk_data[];
extern const uint32_t ramdisk_size;

#define NAME_MAX 56

struct __attribute__((packed)) entry {
    char name[NAME_MAX];
    uint32_t offset, size;
};

static const struct entry *table(uint32_t *count)
{
    *count = 0;
    if (ramdisk_size < 8 || memcmp(ramdisk_data, "LKXR", 4))
        return 0;
    uint32_t n;
    memcpy(&n, ramdisk_data + 4, 4);
    if (n > (ramdisk_size - 8) / sizeof(struct entry))
        return 0;
    *count = n;
    return (const struct entry *)(ramdisk_data + 8);
}

/* An entry is usable if its name ends inside the field and its data inside
 * the ramdisk. */
static int valid(const struct entry *e)
{
    int terminated = 0;
    for (int i = 0; i < NAME_MAX && !terminated; i++)
        terminated = e->name[i] == '\0';
    return terminated && e->offset <= ramdisk_size &&
           e->size <= ramdisk_size - e->offset;
}

int ramdisk_count(void)
{
    uint32_t n;
    table(&n);
    return (int)n;
}

const char *ramdisk_name(int i)
{
    uint32_t n;
    const struct entry *t = table(&n);
    if (i < 0 || (uint32_t)i >= n || !valid(&t[i]))
        return 0;
    return t[i].name;
}

int ramdisk_find(const char *name, const void **data, uint32_t *size)
{
    uint32_t n;
    const struct entry *t = table(&n);
    for (uint32_t i = 0; i < n; i++)
        if (valid(&t[i]) && !strcmp(t[i].name, name)) {
            *data = ramdisk_data + t[i].offset;
            *size = t[i].size;
            return 0;
        }
    return -ENOENT;
}
