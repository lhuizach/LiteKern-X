/* LiteKern X — FAT32 driver test on Linux (tests/fat/test-fat.sh).
 *
 * Builds kernel/fat32.c as an ordinary program (with AddressSanitizer) and
 * works on a copy of the FAT32 partition image the build makes: reads what
 * mkfs.fat and mcopy wrote, then creates, renames and deletes. The script
 * then has fsck.fat check the result and compares mtools' listing with the
 * one this prints. Usage: fat_host IMAGE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/errno.h"
#include "kernel/fat32.h"

static FILE *img;
static int failed;

static int dev_read(void *ctx, uint32_t lba, uint32_t count, void *buf)
{
    (void)ctx;
    return fseek(img, (long)lba * 512, SEEK_SET) || fread(buf, 512, count, img) != count;
}

static int dev_write(void *ctx, uint32_t lba, uint32_t count, const void *buf)
{
    (void)ctx;
    return fseek(img, (long)lba * 512, SEEK_SET) || fwrite(buf, 512, count, img) != count;
}

static void now(void *ctx, uint16_t *date, uint16_t *time)
{
    (void)ctx;
    *date = (2026 - 1980) << 9 | 9 << 5 | 30;
    *time = 18 << 11 | 30 << 5;
}

static void check(int ok, const char *what)
{
    printf("fat: %s %s\n", ok ? "ok  " : "FAIL", what);
    failed += !ok;
}

/* Print every path (folders end in /), depth first, like `mdir -/ -b`. */
struct walk_ctx {
    struct fat_volume *v;
    char path[640];
};

static int print_fn(void *ctx, const struct fat_entry *e);

static void print_tree(struct fat_volume *v, uint32_t dir, const char *prefix)
{
    struct walk_ctx w = { v, "" };
    snprintf(w.path, sizeof(w.path), "%s", prefix);
    fat_list(v, dir, print_fn, &w);
}

static int print_fn(void *ctx, const struct fat_entry *e)
{
    struct walk_ctx *w = ctx;
    char path[640];
    snprintf(path, sizeof(path), "%s/%s", w->path, e->name);
    printf("tree: %s%s\n", path, e->is_dir ? "/" : "");
    if (e->is_dir)
        print_tree(w->v, e->cluster, path);
    return 0;
}

static int count_fn(void *ctx, const struct fat_entry *e)
{
    (void)e;
    ++*(int *)ctx;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2 || !(img = fopen(argv[1], "r+b"))) {
        fprintf(stderr, "usage: fat_host IMAGE\n");
        return 2;
    }
    struct fat_volume v = { .read = dev_read, .write = dev_write, .now = now };
    check(fat_mount(&v) == 0 && !strcmp(v.label, "LITEKERNX"), "mounts the FAT32 partition (label LITEKERNX)");

    /* What mkfs.fat + mcopy put there. */
    struct fat_entry e, docs, pics;
    check(fat_find(&v, fat_root(&v), "Welcome to LiteKern X.txt", &e) == 0 && !e.is_dir &&
              e.lfn_slots == 2, "finds a long name (VFAT) written by mcopy");
    char text[256] = { 0 };
    int n = fat_read(&v, &e, 0, text, sizeof(text) - 1);
    check(n == (int)e.size && !strncmp(text, "Welcome to LiteKern X!", 22), "reads its contents");
    check(fat_read(&v, &e, 11, text, 7) == 7 && !strncmp(text, "LiteKer", 7), "reads from an offset");
    check(fat_find(&v, fat_root(&v), "readme.txt", &e) == 0 && !strcmp(e.name, "README.TXT"),
          "finds a short name, ignoring case");
    check(fat_find(&v, fat_root(&v), "documents", &docs) == 0 && docs.is_dir, "finds a folder");
    check(fat_find(&v, fat_root(&v), "Pictures", &pics) == 0 && pics.is_dir, "finds an empty folder");
    check(fat_find(&v, fat_root(&v), "nope.txt", 0) == -ENOENT, "a missing name is -ENOENT");

    /* Creating. */
    uint64_t free0 = fat_free_bytes(&v);
    struct fat_entry hello, projects, notes;
    check(fat_create(&v, fat_root(&v), "hello world.txt", 0, &hello) == 0 && hello.lfn_slots == 2,
          "creates a file with a long name");
    check(fat_create(&v, fat_root(&v), "NOTES.TXT", 0, &notes) == 0 && notes.lfn_slots == 0,
          "creates a file with a plain 8.3 name (no long name needed)");
    check(fat_create(&v, fat_root(&v), "Projects", 1, &projects) == 0 && projects.is_dir &&
              projects.cluster, "creates a folder");
    check(fat_create(&v, projects.cluster, "main.c", 0, 0) == 0, "creates a file inside it");
    check(fat_create(&v, fat_root(&v), "HELLO WORLD.TXT", 0, 0) == -EEXIST,
          "refuses a name that exists (in any case)");
    check(fat_create(&v, fat_root(&v), "a:b", 0, 0) == -EINVAL && fat_bad_name("bad?") &&
              !fat_bad_name("fine name.txt"), "refuses names FAT can't hold");
    check(fat_free_bytes(&v) == free0 - 512, "the new folder took one cluster of free space");

    /* Enough files to spill Pictures over several clusters (16 slots each). */
    int ok = 1;
    for (int i = 0; i < 40 && ok; i++) {
        char name[64];
        snprintf(name, sizeof(name), "holiday photo %02d.png", i);
        ok = fat_create(&v, pics.cluster, name, 0, 0) == 0;
    }
    int count = 0;
    fat_list(&v, pics.cluster, count_fn, &count);
    check(ok && count == 40, "a folder grows past its first cluster (40 long names)");
    check(fat_find(&v, pics.cluster, "holiday photo 39.png", 0) == 0, "entries in the new clusters are found");

    /* Renaming. */
    check(fat_rename(&v, &hello, "Hello World.txt") == 0 && !strcmp(hello.name, "Hello World.txt"),
          "renames: a change of case only");
    check(fat_find(&v, fat_root(&v), "README.TXT", &e) == 0 && fat_rename(&v, &e, "read me first.md") == 0,
          "renames a short name to a long one");
    check(fat_find(&v, fat_root(&v), "README.TXT", 0) == -ENOENT &&
              fat_find(&v, fat_root(&v), "read me first.md", &e) == 0 && e.size == 12,
          "the old name is gone; the file keeps its contents");
    check(fat_rename(&v, &e, "NOTES.TXT") == -EEXIST, "renaming onto another name is refused");

    /* Deleting. */
    check(fat_find(&v, fat_root(&v), "Documents", &docs) == 0 && fat_delete(&v, &docs) == 0 &&
              fat_find(&v, fat_root(&v), "Documents", 0) == -ENOENT,
          "deletes a folder with a file in it");
    check(fat_find(&v, fat_root(&v), "NOTES.TXT", &e) == 0 && fat_delete(&v, &e) == 0,
          "deletes a file");
    uint64_t before = fat_free_bytes(&v);
    check(fat_find(&v, fat_root(&v), "Pictures", &pics) == 0 && fat_delete(&v, &pics) == 0 &&
              fat_free_bytes(&v) > before, "deletes a big folder and frees its clusters");
    check(fat_create(&v, fat_root(&v), "Pictures", 1, &pics) == 0, "creates a folder again");

    struct fat_volume ro = { .read = dev_read, .ctx = 0 };
    check(fat_mount(&ro) == 0 && ro.read_only && fat_create(&ro, fat_root(&ro), "x", 0, 0) == -EROFS,
          "without a write callback it's read-only");

    print_tree(&v, fat_root(&v), "");
    fclose(img);
    printf("fat: %s\n", failed ? "FAILED" : "all passed");
    return failed ? 1 : 0;
}
