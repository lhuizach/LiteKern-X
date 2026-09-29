#include "kernel/vbe.h"
#include "kernel/pci.h"
#include "kernel/printk.h"

#define VBIOS_BASE      0xc0000u    /* the video BIOS copy in shadow RAM (identity-mapped) */
#define VBIOS_MAX       0x10000u
#define ATTR_WANTED     0x0091u     /* supported, graphics, linear framebuffer */

static void print_oem(const char *s)
{
    for (unsigned i = 0; i < VBE_OEM_MAX && s[i]; i++)
        kprintf("%c", s[i] >= 0x20 && s[i] < 0x7f ? s[i] : '?');
}

void vbe_report(const struct boot_info *bi)
{
    const struct vbe_mode_entry *m = (const struct vbe_mode_entry *)bi->vbe_modes_addr;
    if (!bi->vbe_version) {
        kprintf("vbe: no VBE report from stage 2\n");
        return;
    }
    kprintf("vbe: VBE %u.%u, '", bi->vbe_version >> 8, bi->vbe_version & 0xff);
    print_oem(bi->vbe_oem);
    kprintf("', %u KiB, %u modes seen; 32 bpp LFB:", bi->vbe_mem_kb, bi->vbe_modes_count);
    for (uint32_t i = 0; i < bi->vbe_modes_count; i++)
        if (m[i].bpp == 32 && m[i].model == 6 && (m[i].attributes & ATTR_WANTED) == ATTR_WANTED)
            kprintf(" %ux%u", m[i].width, m[i].height);
    kprintf("\n");
    if (bi->flags & BI_FLAG_VBIOS_PATCHED)
        kprintf("vbe: patched the Intel video BIOS: mode 0x5c is now 1024x600 (panel native)\n");
    if (bi->flags & BI_FLAG_VBIOS_PATCH_FAILED)
        kprintf("vbe: WARNING: could not patch the Intel video BIOS (shadow RAM stayed "
                "read-only); running at the best standard mode instead\n");
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }

void vbios_diag(const struct boot_info *bi)
{
    const struct vbe_mode_entry *m = (const struct vbe_mode_entry *)bi->vbe_modes_addr;
    const uint8_t *rom = (const uint8_t *)VBIOS_BASE;

    /* 1. Every mode the BIOS described: mode:WxHxBPP/model/attributes. */
    kprintf("diag: vbe modes (mode:WxHxbpp/model/attr):\n");
    for (uint32_t i = 0; i < bi->vbe_modes_count; i++)
        kprintf("%s%03x:%ux%ux%u/%u/%02x%s", i % 4 ? "  " : " ", m[i].mode, m[i].width,
                m[i].height, m[i].bpp, m[i].model, m[i].attributes & 0xff,
                i % 4 == 3 || i + 1 == bi->vbe_modes_count ? "\n" : "");

    /* 2. Chipset shadow-RAM control (945: PAM0-PAM6 at host bridge 0x90-0x96;
     *    3 = read/write RAM, 1 = read-only, 0 = ROM). PAM1/PAM2 cover C0000-CFFFF. */
    uint32_t pam_lo = pci_read32(0, 0, 0, 0x90), pam_hi = pci_read32(0, 0, 0, 0x94);
    uint32_t host = pci_read32(0, 0, 0, 0x00);
    kprintf("diag: host %04x:%04x pam 90-96 = %02x %02x %02x %02x %02x %02x %02x\n",
            host & 0xffff, host >> 16, pam_lo & 0xff, (pam_lo >> 8) & 0xff, (pam_lo >> 16) & 0xff,
            pam_lo >> 24, pam_hi & 0xff, (pam_hi >> 8) & 0xff, (pam_hi >> 16) & 0xff);

    /* 3. The video BIOS image and its Intel mode table. */
    if (rom[0] != 0x55 || rom[1] != 0xaa) {
        kprintf("diag: no option ROM signature at 0x%05x\n", VBIOS_BASE);
        return;
    }
    uint32_t size = rom[2] * 512u;
    if (!size || size > VBIOS_MAX)
        size = VBIOS_MAX;
    kprintf("diag: vbios %u KiB at 0x%05x\n", size / 1024, VBIOS_BASE);

    /* 915resolution's signature: 5-byte entries {mode, bpp, u16 resolution
     * offset, ?} starting with modes 0x30, 0x32, 0x34. */
    uint32_t table = 0;
    for (uint32_t i = 0; i + 15 < size; i++)
        if (rom[i] == 0x30 && rom[i + 5] == 0x32 && rom[i + 10] == 0x34) {
            table = i;
            break;
        }
    if (!table) {
        kprintf("diag: no Intel mode table (0x30/0x32/0x34 pattern) found\n");
        return;
    }

    uint32_t n = 0;
    kprintf("diag: mode table at +0x%04x (mode/bpp/res/?):\n", table);
    for (const uint8_t *e = rom + table; e[0] != 0xff && n < 48 && e + 5 <= rom + size; e += 5, n++)
        kprintf("%s%02x/%u/%04x/%02x%s", n % 6 ? "  " : " ", e[0], e[1], rd16(e + 2), e[4],
                n % 6 == 5 ? "\n" : "");
    kprintf("%sdiag: %u entries\n", n % 6 ? "\n" : "", n);

    /* Each distinct resolution record, raw, plus the two readings 915resolution
     * uses: type 1 (packed bytes) and types 2/3 (first modeline: clock at +6,
     * x-1 at +10, y-1 at +22). */
    uint16_t seen[24];
    uint32_t nseen = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint16_t off = rd16(rom + table + k * 5 + 2);
        int dup = 0;
        for (uint32_t j = 0; j < nseen; j++)
            dup |= seen[j] == off;
        if (dup || nseen == 24 || off + 40u > size)
            continue;
        seen[nseen++] = off;
        const uint8_t *r = rom + off;
        kprintf("res +%04x:", off);
        for (int b = 0; b < 26; b++)
            kprintf(" %02x", r[b]);
        kprintf("\n    t1 %ux%u  t2/3 %ux%u clk %u\n",
                ((r[4] & 0xf0) << 4) | r[2], ((r[7] & 0xf0) << 4) | r[5],
                rd16(r + 10) + 1, rd16(r + 22) + 1, rd32(r + 6));
    }
}
