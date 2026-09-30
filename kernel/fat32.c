#include "kernel/fat32.h"
#include "kernel/errno.h"
#include "kernel/string.h"

#define SECTOR      512
#define SLOT        32                  /* bytes per directory entry */
#define SLOTS_PER_SECTOR (SECTOR / SLOT)
#define EOC         0x0ffffff8u         /* this and above: end of a chain */
#define EOC_MARK    0x0fffffffu
#define FAT_MASK    0x0fffffffu

#define ATTR_RO     0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_LABEL  0x08
#define ATTR_DIR    0x10
#define ATTR_ARCH   0x20
#define ATTR_LFN    0x0f

#define LFN_LAST    0x40
#define LFN_CHARS   13
#define LFN_MAX     255
#define MAX_DEPTH   8                   /* folders inside folders we'll delete */

/* --- little-endian fields --------------------------------------------------- */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

static int same_name(const char *a, const char *b)
{
    while (*a && upper(*a) == upper(*b))
        a++, b++;
    return upper(*a) == upper(*b);
}

/* --- sectors, clusters and the FAT ----------------------------------------- */

static int rd(struct fat_volume *v, uint32_t sector, void *buf)
{
    return v->read(v->ctx, v->part_lba + sector, 1, buf) ? -EIO : 0;
}

static int wr(struct fat_volume *v, uint32_t sector, const void *buf)
{
    if (v->read_only)
        return -EROFS;
    return v->write(v->ctx, v->part_lba + sector, 1, buf) ? -EIO : 0;
}

static int valid_cluster(const struct fat_volume *v, uint32_t c)
{
    return c >= 2 && c < v->clusters + 2;
}

static uint32_t cluster_sector(const struct fat_volume *v, uint32_t c)
{
    return v->data_start + (c - 2) * v->sec_per_clus;
}

/* The FAT entry for cluster c, or 0xffffffff on a read error. */
static uint32_t fat_get(struct fat_volume *v, uint32_t c)
{
    uint8_t s[SECTOR];
    if (rd(v, v->reserved + c * 4 / SECTOR, s))
        return 0xffffffffu;
    return le32(s + c * 4 % SECTOR) & FAT_MASK;
}

/* Set it in every copy of the FAT, keeping the top 4 reserved bits. */
static int fat_set(struct fat_volume *v, uint32_t c, uint32_t value)
{
    uint8_t s[SECTOR];
    for (uint32_t i = 0; i < v->nfats; i++) {
        uint32_t sector = v->reserved + i * v->fat_size + c * 4 / SECTOR;
        int err = rd(v, sector, s);
        if (err)
            return err;
        uint8_t *p = s + c * 4 % SECTOR;
        put32(p, (le32(p) & ~FAT_MASK) | (value & FAT_MASK));
        if ((err = wr(v, sector, s)))
            return err;
    }
    return 0;
}

static int write_fsinfo(struct fat_volume *v)
{
    uint8_t s[SECTOR];
    if (!v->fsinfo)
        return 0;
    int err = rd(v, v->fsinfo, s);
    if (err || le32(s) != 0x41615252u || le32(s + 484) != 0x61417272u)
        return err;
    put32(s + 488, v->free_count);
    put32(s + 492, v->next_free);
    return wr(v, v->fsinfo, s);
}

static int zero_cluster(struct fat_volume *v, uint32_t c)
{
    uint8_t s[SECTOR];
    memset(s, 0, sizeof(s));
    for (uint32_t i = 0; i < v->sec_per_clus; i++) {
        int err = wr(v, cluster_sector(v, c) + i, s);
        if (err)
            return err;
    }
    return 0;
}

/* A free cluster, zeroed and marked as the end of a chain; 0 if none. */
static uint32_t alloc_cluster(struct fat_volume *v)
{
    uint32_t start = valid_cluster(v, v->next_free) ? v->next_free : 2;
    for (uint32_t n = 0, c = start; n < v->clusters; n++, c = valid_cluster(v, c + 1) ? c + 1 : 2) {
        uint32_t e = fat_get(v, c);
        if (e == 0xffffffffu)
            return 0;
        if (e != 0)
            continue;
        if (fat_set(v, c, EOC_MARK) || zero_cluster(v, c))
            return 0;
        if (v->free_count != 0xffffffffu && v->free_count)
            v->free_count--;
        v->next_free = c + 1;
        return c;
    }
    return 0;
}

static int free_chain(struct fat_volume *v, uint32_t c)
{
    for (uint32_t n = 0; valid_cluster(v, c) && n < v->clusters; n++) {
        uint32_t next = fat_get(v, c);
        if (next == 0xffffffffu)
            return -EIO;
        int err = fat_set(v, c, 0);
        if (err)
            return err;
        if (v->free_count != 0xffffffffu)
            v->free_count++;
        c = next;
    }
    return 0;
}

/* --- mounting ----------------------------------------------------------------- */

int fat_mount(struct fat_volume *v)
{
    uint8_t s[SECTOR];
    if (!v->read)
        return -EINVAL;
    if (!v->write)                          /* no way to write: read-only */
        v->read_only = 1;
    int err = rd(v, 0, s);
    if (err)
        return err;
    uint32_t spc = s[13];
    if (s[510] != 0x55 || s[511] != 0xaa || le16(s + 11) != SECTOR || !spc || (spc & (spc - 1)))
        return -EINVAL;
    /* FAT32: no fixed root folder, no 16-bit FAT size. */
    if (le16(s + 17) != 0 || le16(s + 22) != 0 || !le32(s + 36))
        return -EINVAL;
    v->sec_per_clus = spc;
    v->reserved = le16(s + 14);
    v->nfats = s[16];
    v->fat_size = le32(s + 36);
    v->root_clus = le32(s + 44);
    v->fsinfo = le16(s + 48);
    v->total_sectors = le16(s + 19) ? le16(s + 19) : le32(s + 32);
    if (!v->reserved || !v->nfats || v->nfats > 2 || !v->total_sectors)
        return -EINVAL;
    v->data_start = v->reserved + v->nfats * v->fat_size;
    if (v->data_start >= v->total_sectors)
        return -EINVAL;
    v->clusters = (v->total_sectors - v->data_start) / spc;
    if (v->clusters > v->fat_size * (SECTOR / 4) - 2)
        v->clusters = v->fat_size * (SECTOR / 4) - 2;   /* the FAT must cover them */
    if (!valid_cluster(v, v->root_clus))
        return -EINVAL;
    memcpy(v->label, s + 71, 11);
    for (int i = 10; i >= 0 && v->label[i] == ' '; i--)
        v->label[i] = '\0';
    v->label[11] = '\0';

    v->free_count = 0xffffffffu;
    v->next_free = 2;
    if (v->fsinfo && v->fsinfo < v->reserved && !rd(v, v->fsinfo, s) &&
        le32(s) == 0x41615252u && le32(s + 484) == 0x61417272u) {
        if (le32(s + 488) <= v->clusters)
            v->free_count = le32(s + 488);
        if (valid_cluster(v, le32(s + 492)))
            v->next_free = le32(s + 492);
    } else {
        v->fsinfo = 0;
    }
    return 0;
}

uint32_t fat_root(const struct fat_volume *v)
{
    return v->root_clus;
}

uint64_t fat_free_bytes(struct fat_volume *v)
{
    if (v->free_count == 0xffffffffu) {     /* count them, once */
        uint8_t s[SECTOR];
        uint32_t n = 0;
        for (uint32_t c = 2; c < v->clusters + 2; c++) {
            if (c == 2 || c * 4 % SECTOR == 0)
                if (rd(v, v->reserved + c * 4 / SECTOR, s))
                    return 0;
            n += (le32(s + c * 4 % SECTOR) & FAT_MASK) == 0;
        }
        v->free_count = n;
    }
    return (uint64_t)v->free_count * v->sec_per_clus * SECTOR;
}

/* --- walking a folder's entries --------------------------------------------- */

#define WALK_STOP   1
#define WALK_DIRTY  2           /* the callback changed the slot: write it back */

/* Call fn for every 32-byte slot of the folder starting at cluster dir, in
 * order, with its index. Stops at the end of the chain (not at a 0x00
 * entry: callers decide what that means). */
static int walk(struct fat_volume *v, uint32_t dir,
                int (*fn)(void *ctx, uint8_t *slot, uint32_t index), void *ctx)
{
    uint8_t s[SECTOR];
    uint32_t index = 0;
    for (uint32_t n = 0, c = dir; valid_cluster(v, c) && n < v->clusters; n++) {
        for (uint32_t i = 0; i < v->sec_per_clus; i++) {
            uint32_t sector = cluster_sector(v, c) + i;
            int err = rd(v, sector, s), dirty = 0;
            if (err)
                return err;
            for (uint32_t k = 0; k < SLOTS_PER_SECTOR; k++, index++) {
                int r = fn(ctx, s + k * SLOT, index);
                dirty |= r & WALK_DIRTY;
                if (r & WALK_STOP) {
                    if (dirty && (err = wr(v, sector, s)))
                        return err;
                    return 0;
                }
            }
            if (dirty && (err = wr(v, sector, s)))
                return err;
        }
        c = fat_get(v, c);
        if (c == 0xffffffffu)
            return -EIO;
    }
    return 0;
}

/* --- reading entries (short names + VFAT long names) -------------------------- */

static uint8_t lfn_checksum(const uint8_t *short_name)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + short_name[i]);
    return sum;
}

static const uint8_t lfn_offsets[LFN_CHARS] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

struct reader {
    int (*fn)(void *ctx, const struct fat_entry *e);
    void *ctx;
    uint32_t dir;
    int count, stopped;
    char lfn[LFN_MAX + 1];
    int lfn_ok;                 /* the long-name entries so far fit together */
    uint8_t lfn_sum, lfn_next;  /* checksum; the sequence number expected next */
    uint32_t lfn_slots;
};

static void short_name_text(const uint8_t *e, char *out)
{
    int n = 0, lower_base = e[12] & 0x08, lower_ext = e[12] & 0x10;
    for (int i = 0; i < 8 && e[i] != ' '; i++) {
        char c = (char)(i == 0 && e[0] == 0x05 ? 0xe5 : e[i]);
        out[n++] = lower_base && c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
    }
    if (e[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++)
            out[n++] = lower_ext && e[i] >= 'A' && e[i] <= 'Z' ? (char)(e[i] + 32) : (char)e[i];
    }
    out[n] = '\0';
}

static int read_slot(void *ctx, uint8_t *e, uint32_t index)
{
    struct reader *r = ctx;
    if (e[0] == 0x00)
        return WALK_STOP;                   /* the end of the folder */
    if (e[0] == 0xe5) {
        r->lfn_ok = 0;
        return 0;
    }
    if (e[11] == ATTR_LFN) {
        int seq = e[0] & 0x1f;
        if (e[0] & LFN_LAST) {
            r->lfn_ok = seq >= 1 && seq <= 20;
            r->lfn_sum = e[13];
            r->lfn_slots = 0;
            memset(r->lfn, 0, sizeof(r->lfn));
        } else if (!r->lfn_ok || seq != r->lfn_next || e[13] != r->lfn_sum) {
            r->lfn_ok = 0;
        }
        if (r->lfn_ok) {
            r->lfn_next = (uint8_t)(seq - 1);
            r->lfn_slots++;
            for (int i = 0; i < LFN_CHARS; i++) {
                int pos = (seq - 1) * LFN_CHARS + i;
                uint16_t ch = le16(e + lfn_offsets[i]);
                if (pos < LFN_MAX && ch != 0 && ch != 0xffff)
                    r->lfn[pos] = ch < 0x80 ? (char)ch : '?';
            }
        }
        return 0;
    }
    int had_lfn = r->lfn_ok && r->lfn_next == 0 && r->lfn_sum == lfn_checksum(e);
    uint32_t lfn_slots = had_lfn ? r->lfn_slots : 0;
    r->lfn_ok = 0;
    if ((e[11] & ATTR_LABEL) || e[0] == '.')
        return 0;                           /* the volume label; "." and ".." */

    struct fat_entry out;
    memset(&out, 0, sizeof(out));
    if (had_lfn && r->lfn[0]) {
        int n = 0;
        for (; r->lfn[n] && n < FAT_NAME_MAX - 1; n++)
            out.name[n] = r->lfn[n];
        if (r->lfn[n])
            out.name[FAT_NAME_MAX - 2] = '~';   /* too long for us: marked cut */
    } else {
        short_name_text(e, out.name);
    }
    out.is_dir = (e[11] & ATTR_DIR) != 0;
    out.cluster = (uint32_t)le16(e + 20) << 16 | le16(e + 26);
    out.size = out.is_dir ? 0 : le32(e + 28);
    out.time = le16(e + 22);
    out.date = le16(e + 24);
    out.dir = r->dir;
    out.slot = index;
    out.lfn_slots = lfn_slots;
    r->count++;
    if (r->fn(r->ctx, &out)) {
        r->stopped = 1;
        return WALK_STOP;
    }
    return 0;
}

int fat_list(struct fat_volume *v, uint32_t dir,
             int (*fn)(void *ctx, const struct fat_entry *e), void *ctx)
{
    struct reader r;
    memset(&r, 0, sizeof(r));
    r.fn = fn;
    r.ctx = ctx;
    r.dir = dir;
    int err = walk(v, dir, read_slot, &r);
    return err ? err : r.count;
}

struct finder {
    const char *name;
    struct fat_entry *out;
    int found;
};

static int find_fn(void *ctx, const struct fat_entry *e)
{
    struct finder *f = ctx;
    if (!same_name(e->name, f->name))
        return 0;
    *f->out = *e;
    f->found = 1;
    return 1;
}

int fat_find(struct fat_volume *v, uint32_t dir, const char *name, struct fat_entry *out)
{
    struct fat_entry tmp;
    struct finder f = { name, out ? out : &tmp, 0 };
    int err = fat_list(v, dir, find_fn, &f);
    if (err < 0)
        return err;
    return f.found ? 0 : -ENOENT;
}

int fat_read(struct fat_volume *v, const struct fat_entry *e, uint32_t offset, void *buf,
             uint32_t len)
{
    uint8_t s[SECTOR];
    uint32_t clus_bytes = v->sec_per_clus * SECTOR, done = 0;
    if (e->is_dir)
        return -EINVAL;
    if (offset >= e->size)
        return 0;
    if (len > e->size - offset)
        len = e->size - offset;
    uint32_t c = e->cluster;
    for (uint32_t skip = offset / clus_bytes; skip && valid_cluster(v, c); skip--)
        c = fat_get(v, c);
    uint32_t pos = offset % clus_bytes;
    while (done < len && valid_cluster(v, c)) {
        int err = rd(v, cluster_sector(v, c) + pos / SECTOR, s);
        if (err)
            return err;
        uint32_t n = SECTOR - pos % SECTOR;
        if (n > len - done)
            n = len - done;
        memcpy((uint8_t *)buf + done, s + pos % SECTOR, n);
        done += n;
        pos += n;
        if (pos == clus_bytes) {
            pos = 0;
            c = fat_get(v, c);
        }
    }
    return (int)done;
}

/* --- names ---------------------------------------------------------------------- */

const char *fat_bad_name(const char *name)
{
    int n = 0;
    for (; name[n]; n++) {
        unsigned char c = (unsigned char)name[n];
        if (c < 0x20 || c > 0x7e)
            return "Names can only use plain letters, digits and punctuation.";
        for (const char *bad = "\\/:*?\"<>|"; *bad; bad++)
            if (c == (unsigned char)*bad)
                return "Names can't contain \\ / : * ? \" < > |";
    }
    if (n == 0)
        return "Type a name.";
    if (n >= FAT_NAME_MAX)
        return "That name is too long.";
    if (name[0] == ' ' || name[n - 1] == ' ' || name[n - 1] == '.')
        return "Names can't start or end with a space, or end with a dot.";
    if (!strcmp(name, ".") || !strcmp(name, ".."))
        return "That name is reserved.";
    return 0;
}

static int short_char_ok(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr("$%'-_@~`!(){}^#&", c));
}

/* Is name already a plain upper-case 8.3 name? Then it needs no long name. */
static int is_short_name(const char *name, uint8_t out[11])
{
    int n = 0, dot = -1;
    for (; name[n]; n++) {
        if (name[n] == '.') {
            if (dot >= 0)
                return 0;
            dot = n;
        } else if (!short_char_ok(name[n])) {
            return 0;
        }
    }
    int base = dot < 0 ? n : dot, ext = dot < 0 ? 0 : n - dot - 1;
    if (base < 1 || base > 8 || ext > 3 || (dot >= 0 && ext == 0))
        return 0;
    memset(out, ' ', 11);
    memcpy(out, name, (size_t)base);
    if (ext)
        memcpy(out + 8, name + dot + 1, (size_t)ext);
    return 1;
}

struct short_names {
    const uint8_t *want;
    int taken;
};

static int short_taken_fn(void *ctx, uint8_t *e, uint32_t index)
{
    (void)index;
    struct short_names *sn = ctx;
    if (e[0] == 0x00)
        return WALK_STOP;
    if (e[0] != 0xe5 && e[11] != ATTR_LFN && !memcmp(e, sn->want, 11)) {
        sn->taken = 1;
        return WALK_STOP;
    }
    return 0;
}

/* 1 if a short name is in use in dir, 0 if not, or a negative error. */
static int short_taken(struct fat_volume *v, uint32_t dir, const uint8_t *sfn)
{
    struct short_names sn = { sfn, 0 };
    int err = walk(v, dir, short_taken_fn, &sn);
    return err ? err : sn.taken;
}

/* A short alias for a long name, as Windows makes them: "LONGNA~1.TXT". */
static int make_alias(struct fat_volume *v, uint32_t dir, const char *name, uint8_t out[11])
{
    uint8_t basis[11];
    int n = (int)strlen(name), dot = -1, b = 0, x = 0;
    for (int i = n - 1; i > 0; i--)
        if (name[i] == '.') {
            dot = i;
            break;
        }
    memset(basis, ' ', 11);
    for (int i = 0; i < (dot < 0 ? n : dot) && b < 8; i++) {
        char c = upper(name[i]);
        if (c == ' ' || c == '.')
            continue;
        basis[b++] = (uint8_t)(short_char_ok(c) ? c : '_');
    }
    for (int i = dot + 1; dot >= 0 && i < n && x < 3; i++) {
        char c = upper(name[i]);
        if (c != ' ')
            basis[8 + x++] = (uint8_t)(short_char_ok(c) ? c : '_');
    }
    if (b == 0)
        basis[b++] = '_';
    for (int k = 1; k < 1000000; k++) {
        char num[8];
        int digits = 0;
        for (int t = k; t; t /= 10)
            num[digits++] = (char)('0' + t % 10);
        int keep = b < 8 - 1 - digits ? b : 8 - 1 - digits;
        memcpy(out, basis, 11);
        memset(out + keep, ' ', 8 - (size_t)keep);
        out[keep] = '~';
        for (int d = 0; d < digits; d++)
            out[keep + 1 + d] = (uint8_t)num[digits - 1 - d];
        int taken = short_taken(v, dir, out);
        if (taken <= 0)
            return taken;

    }
    return -EEXIST;
}

/* --- writing entries ------------------------------------------------------------- */

struct slot_search {
    uint32_t need, run, start, end_seen, total;
};

static int free_run_fn(void *ctx, uint8_t *e, uint32_t index)
{
    struct slot_search *s = ctx;
    s->total = index + 1;
    if (e[0] == 0x00 || e[0] == 0xe5) {
        if (!s->run)
            s->start = index;
        if (++s->run == s->need)
            return WALK_STOP;
    } else {
        s->run = 0;
    }
    return 0;
}

/* After the old end: turn 0x00 "end of folder" slots into deleted ones, so
 * entries in a newly added cluster aren't hidden behind them. */
static int unend_fn(void *ctx, uint8_t *e, uint32_t index)
{
    (void)ctx, (void)index;
    if (e[0] == 0x00) {
        e[0] = 0xe5;
        return WALK_DIRTY;
    }
    return 0;
}

static uint32_t last_cluster(struct fat_volume *v, uint32_t c)
{
    for (uint32_t n = 0; n < v->clusters; n++) {
        uint32_t next = fat_get(v, c);
        if (!valid_cluster(v, next))
            return c;
        c = next;
    }
    return c;
}

/* Find `need` consecutive free slots in the folder, growing it by a cluster
 * if there aren't any. Returns the first slot's index, or a negative error. */
static int64_t find_slots(struct fat_volume *v, uint32_t dir, uint32_t need)
{
    struct slot_search s = { need, 0, 0, 0, 0 };
    int err = walk(v, dir, free_run_fn, &s);
    if (err)
        return err;
    if (s.run == need)
        return s.start;
    uint32_t per_cluster = v->sec_per_clus * SLOTS_PER_SECTOR;
    if (need > per_cluster || s.total >= 65536 - per_cluster)
        return -ENOSPC;                     /* FAT folders stop at 65536 entries */
    if ((err = walk(v, dir, unend_fn, 0)))
        return err;
    uint32_t c = alloc_cluster(v);
    if (!c)
        return -ENOSPC;
    if ((err = fat_set(v, last_cluster(v, dir), c)))
        return err;
    return s.total;                         /* the new cluster's first slot */
}

/* Read or write one slot by index. */
static int slot_io(struct fat_volume *v, uint32_t dir, uint32_t index, uint8_t *e, int write)
{
    uint8_t s[SECTOR];
    uint32_t per_cluster = v->sec_per_clus * SLOTS_PER_SECTOR, c = dir;
    for (uint32_t k = index / per_cluster; k; k--) {
        c = fat_get(v, c);
        if (!valid_cluster(v, c))
            return -EIO;
    }
    uint32_t sector = cluster_sector(v, c) + index % per_cluster / SLOTS_PER_SECTOR;
    int err = rd(v, sector, s);
    if (err)
        return err;
    uint8_t *p = s + index % SLOTS_PER_SECTOR * SLOT;
    if (!write) {
        memcpy(e, p, SLOT);
        return 0;
    }
    memcpy(p, e, SLOT);
    return wr(v, sector, s);
}

/* Write `name` into dir as long-name entries (if it needs them) plus the
 * short entry `sfn_entry` (its name bytes are filled in here). */
static int insert(struct fat_volume *v, uint32_t dir, const char *name, uint8_t sfn_entry[SLOT],
                  struct fat_entry *out)
{
    uint8_t sfn[11];
    int lfn = !is_short_name(name, sfn);
    if (lfn) {
        int err = make_alias(v, dir, name, sfn);
        if (err)
            return err;
    }
    uint32_t len = (uint32_t)strlen(name), nlfn = lfn ? (len + LFN_CHARS - 1) / LFN_CHARS : 0;
    int64_t first = find_slots(v, dir, nlfn + 1);
    if (first < 0)
        return (int)first;
    memcpy(sfn_entry, sfn, 11);
    uint8_t sum = lfn_checksum(sfn);
    for (uint32_t k = 0; k < nlfn; k++) {
        uint8_t e[SLOT];
        uint32_t seq = nlfn - k;            /* stored last part first */
        memset(e, 0, SLOT);
        e[0] = (uint8_t)(seq | (k == 0 ? LFN_LAST : 0));
        e[11] = ATTR_LFN;
        e[13] = sum;
        for (int i = 0; i < LFN_CHARS; i++) {
            uint32_t pos = (seq - 1) * LFN_CHARS + (uint32_t)i;
            uint16_t ch = pos < len ? (uint8_t)name[pos] : pos == len ? 0 : 0xffff;
            put16(e + lfn_offsets[i], ch);
        }
        int err = slot_io(v, dir, (uint32_t)first + k, e, 1);
        if (err)
            return err;
    }
    int err = slot_io(v, dir, (uint32_t)first + nlfn, sfn_entry, 1);
    if (err)
        return err;
    if (out) {
        memset(out, 0, sizeof(*out));
        int n = 0;
        for (; name[n] && n < FAT_NAME_MAX - 1; n++)
            out->name[n] = name[n];
        out->is_dir = (sfn_entry[11] & ATTR_DIR) != 0;
        out->cluster = (uint32_t)le16(sfn_entry + 20) << 16 | le16(sfn_entry + 26);
        out->size = le32(sfn_entry + 28);
        out->time = le16(sfn_entry + 22);
        out->date = le16(sfn_entry + 24);
        out->dir = dir;
        out->slot = (uint32_t)first + nlfn;
        out->lfn_slots = nlfn;
    }
    return 0;
}

/* Mark an entry's slots (long-name ones too) deleted. */
static int erase_slots(struct fat_volume *v, const struct fat_entry *e)
{
    for (uint32_t i = e->slot - e->lfn_slots; i <= e->slot; i++) {
        uint8_t s[SLOT];
        int err = slot_io(v, e->dir, i, s, 0);
        if (err)
            return err;
        s[0] = 0xe5;
        if ((err = slot_io(v, e->dir, i, s, 1)))
            return err;
    }
    return 0;
}

static void stamp(struct fat_volume *v, uint8_t e[SLOT])
{
    uint16_t date = (2026 - 1980) << 9 | 1 << 5 | 1, time = 0;
    if (v->now)
        v->now(v->ctx, &date, &time);
    put16(e + 14, time);        /* created */
    put16(e + 16, date);
    put16(e + 18, date);        /* accessed */
    put16(e + 22, time);        /* modified */
    put16(e + 24, date);
}

static void set_cluster(uint8_t e[SLOT], uint32_t c)
{
    put16(e + 20, (uint16_t)(c >> 16));
    put16(e + 26, (uint16_t)c);
}

int fat_create(struct fat_volume *v, uint32_t dir, const char *name, int is_dir,
               struct fat_entry *out)
{
    if (v->read_only)
        return -EROFS;
    if (fat_bad_name(name))
        return -EINVAL;
    int err = fat_find(v, dir, name, 0);
    if (err != -ENOENT)
        return err ? err : -EEXIST;

    uint8_t e[SLOT];
    memset(e, 0, SLOT);
    e[11] = is_dir ? ATTR_DIR : ATTR_ARCH;
    stamp(v, e);
    uint32_t c = 0;
    if (is_dir) {                           /* its first cluster, with "." and ".." */
        if (!(c = alloc_cluster(v)))
            return -ENOSPC;
        uint8_t dots[2][SLOT];
        memcpy(dots[0], e, SLOT);
        memcpy(dots[1], e, SLOT);
        memcpy(dots[0], ".          ", 11);
        memcpy(dots[1], "..         ", 11);
        set_cluster(dots[0], c);
        set_cluster(dots[1], dir == v->root_clus ? 0 : dir);   /* the root is "0" here */
        if ((err = slot_io(v, c, 0, dots[0], 1)) || (err = slot_io(v, c, 1, dots[1], 1))) {
            free_chain(v, c);
            return err;
        }
        set_cluster(e, c);
    }
    if ((err = insert(v, dir, name, e, out))) {
        if (c)
            free_chain(v, c);
        write_fsinfo(v);
        return err;
    }
    return write_fsinfo(v);
}

int fat_rename(struct fat_volume *v, struct fat_entry *e, const char *new_name)
{
    if (v->read_only)
        return -EROFS;
    if (fat_bad_name(new_name))
        return -EINVAL;
    struct fat_entry other;
    int err = fat_find(v, e->dir, new_name, &other);
    if (err == 0 && other.slot != e->slot)
        return -EEXIST;
    if (err && err != -ENOENT)
        return err;

    uint8_t sfn[SLOT];
    if ((err = slot_io(v, e->dir, e->slot, sfn, 0)))
        return err;
    struct fat_entry old = *e, fresh;
    /* Take the old name's slots out first, so its short name is free for
     * reuse (e.g. a change of case only). */
    if ((err = erase_slots(v, &old)))
        return err;
    if ((err = insert(v, old.dir, new_name, sfn, &fresh))) {
        /* Put the old entries back as they were: un-erase the short entry
         * under its old name (long name lost only in this unlikely case). */
        slot_io(v, old.dir, old.slot, sfn, 1);
        return err;
    }
    *e = fresh;
    return write_fsinfo(v);
}

struct first_child {
    struct fat_entry e;
    int found;
};

static int first_child_fn(void *ctx, const struct fat_entry *e)
{
    struct first_child *f = ctx;
    f->e = *e;
    f->found = 1;
    return 1;
}

static int delete_at(struct fat_volume *v, const struct fat_entry *e, int depth)
{
    if (e->is_dir) {
        if (depth >= MAX_DEPTH)
            return -ENOSPC;                 /* too deep for our stack */
        for (;;) {                          /* one child at a time: the folder changes */
            struct first_child f = { .found = 0 };
            int n = fat_list(v, e->cluster, first_child_fn, &f);
            if (n < 0)
                return n;
            if (!f.found)
                break;
            int err = delete_at(v, &f.e, depth + 1);
            if (err)
                return err;
        }
    }
    int err = erase_slots(v, e);
    if (!err && e->cluster)
        err = free_chain(v, e->cluster);
    return err;
}

int fat_delete(struct fat_volume *v, const struct fat_entry *e)
{
    if (v->read_only)
        return -EROFS;
    if (e->is_dir && e->cluster == v->root_clus)
        return -EINVAL;
    int err = delete_at(v, e, 0);
    int err2 = write_fsinfo(v);
    return err ? err : err2;
}
