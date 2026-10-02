#include "kernel/acpi.h"
#include "kernel/errno.h"
#include "kernel/io.h"
#include "kernel/printk.h"
#include "kernel/string.h"
#include "kernel/timing.h"
#include "kernel/vmm.h"

#define SLP_EN   (1u << 13)
#define SCI_EN   1u

struct __attribute__((packed)) rsdp {
    char sig[8];                /* "RSD PTR " */
    uint8_t checksum;
    char oem[6];
    uint8_t revision;
    uint32_t rsdt;
};

struct __attribute__((packed)) sdt {
    char sig[4];
    uint32_t length;
    uint8_t revision, checksum;
    char oem[6], oem_table[8];
    uint32_t oem_rev, creator, creator_rev;
};

/* The FADT fields we use (offsets from the ACPI spec, all versions). */
#define FADT_DSDT        40
#define FADT_SMI_CMD     48
#define FADT_ACPI_ENABLE 52
#define FADT_PM1A_CNT    64
#define FADT_PM1B_CNT    68

static uint16_t pm1a_cnt, pm1b_cnt;
static uint16_t slp_typ_a, slp_typ_b;
static uint32_t smi_cmd;
static uint8_t acpi_enable;
static int ready;

static int sum_ok(const void *p, uint32_t len)
{
    uint8_t s = 0;
    for (uint32_t i = 0; i < len; i++)
        s = (uint8_t)(s + ((const uint8_t *)p)[i]);
    return s == 0;
}

/* The RSDP: on a 16-byte boundary in the EBDA's first KiB, or in the BIOS
 * area 0xE0000-0xFFFFF. (The EBDA's segment sits in page 0, which isn't
 * mapped, so its usual places are searched: the KiB below 640 KiB.) */
static const struct rsdp *find_rsdp(void)
{
    static const uint32_t ranges[][2] = { { 0x9fc00, 0xa0000 }, { 0x9f000, 0xa0000 },
                                          { 0xe0000, 0x100000 } };
    for (unsigned r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++)
        for (uint32_t a = ranges[r][0]; a < ranges[r][1]; a += 16) {
            const struct rsdp *p = (const struct rsdp *)a;
            if (!memcmp(p->sig, "RSD PTR ", 8) && sum_ok(p, 20))
                return p;
        }
    return 0;
}

/* A table, mapped and checked; NULL if it's not one. */
static const struct sdt *table(uint32_t phys, const char *sig)
{
    if (!phys || vmm_map_firmware(phys, sizeof(struct sdt)))
        return 0;
    const struct sdt *t = (const struct sdt *)phys;
    if (t->length < sizeof(*t) || t->length > 4 * 1024 * 1024 || vmm_map_firmware(phys, t->length))
        return 0;
    if ((sig && memcmp(t->sig, sig, 4)) || !sum_ok(t, t->length))
        return 0;
    return t;
}

/* One AML integer at *p (ZeroOp, OneOp, or a Byte/Word/DWord prefix). */
static int aml_int(const uint8_t **p, const uint8_t *end, uint32_t *v)
{
    const uint8_t *q = *p;
    if (q >= end)
        return -1;
    switch (*q) {
    case 0x00: *v = 0; *p = q + 1; return 0;            /* ZeroOp */
    case 0x01: *v = 1; *p = q + 1; return 0;            /* OneOp */
    case 0x0a: if (q + 2 > end) return -1; *v = q[1]; *p = q + 2; return 0;           /* BytePrefix */
    case 0x0b: if (q + 3 > end) return -1; *v = q[1] | q[2] << 8; *p = q + 3; return 0;   /* Word */
    case 0x0c: if (q + 5 > end) return -1;
               *v = q[1] | q[2] << 8 | (uint32_t)q[3] << 16 | (uint32_t)q[4] << 24;
               *p = q + 5; return 0;                    /* DWordPrefix */
    default:   return -1;
    }
}

/* Name(_S5, Package() { SLP_TYPa, SLP_TYPb, ... }) somewhere in the DSDT. */
static int find_s5(const struct sdt *dsdt)
{
    const uint8_t *start = (const uint8_t *)dsdt + sizeof(*dsdt);
    const uint8_t *end = (const uint8_t *)dsdt + dsdt->length;
    for (const uint8_t *p = start; p + 8 < end; p++) {
        if (memcmp(p, "_S5_", 4))
            continue;
        /* NameOp before it (maybe with a root prefix '\'), PackageOp after. */
        if (!(p[-1] == 0x08 || (p[-1] == '\\' && p[-2] == 0x08)) || p[4] != 0x12)
            continue;
        const uint8_t *q = p + 5;
        q += 1 + (*q >> 6);             /* PkgLength: its lead byte says how many follow */
        q++;                            /* NumElements */
        uint32_t a, b;
        if (aml_int(&q, end, &a) || aml_int(&q, end, &b))
            continue;
        slp_typ_a = (uint16_t)(a & 7);
        slp_typ_b = (uint16_t)(b & 7);
        return 0;
    }
    return -ENOENT;
}

int acpi_init(void)
{
    const struct rsdp *rp = find_rsdp();
    if (!rp) {
        kprintf("acpi: no RSDP; power-off unavailable\n");
        return -ENODEV;
    }
    const struct sdt *rsdt = table(rp->rsdt, "RSDT");
    if (!rsdt) {
        kprintf("acpi: RSDP at 0x%05x, but no valid RSDT; power-off unavailable\n", (uint32_t)rp);
        return -ENODEV;
    }
    const struct sdt *fadt = 0;
    uint32_t n = (rsdt->length - sizeof(*rsdt)) / 4;
    const uint32_t *entries = (const uint32_t *)(rsdt + 1);
    for (uint32_t i = 0; i < n && !fadt; i++)
        fadt = table(entries[i], "FACP");
    if (!fadt || fadt->length < FADT_PM1B_CNT + 4) {
        kprintf("acpi: no FADT; power-off unavailable\n");
        return -ENODEV;
    }
    const uint8_t *f = (const uint8_t *)fadt;
    uint32_t dsdt_addr, a_cnt, b_cnt;
    memcpy(&dsdt_addr, f + FADT_DSDT, 4);
    memcpy(&smi_cmd, f + FADT_SMI_CMD, 4);
    acpi_enable = f[FADT_ACPI_ENABLE];
    memcpy(&a_cnt, f + FADT_PM1A_CNT, 4);
    memcpy(&b_cnt, f + FADT_PM1B_CNT, 4);
    pm1a_cnt = (uint16_t)a_cnt;
    pm1b_cnt = (uint16_t)b_cnt;
    const struct sdt *dsdt = table(dsdt_addr, "DSDT");
    if (!pm1a_cnt || !dsdt || find_s5(dsdt)) {
        kprintf("acpi: %s; power-off unavailable\n", !pm1a_cnt ? "no PM1a control register"
                                                   : !dsdt ? "no DSDT" : "no _S5 in the DSDT");
        return -ENODEV;
    }
    ready = 1;
    char oem[7];
    memcpy(oem, rp->oem, 6);
    oem[6] = '\0';
    for (int i = 5; i >= 0 && oem[i] == ' '; i--)
        oem[i] = '\0';
    kprintf("acpi: %s, RSDP rev %u at 0x%05x; PM1a_CNT 0x%x, PM1b_CNT 0x%x; _S5 SLP_TYP %u/%u\n",
            oem, rp->revision, (uint32_t)rp, pm1a_cnt, pm1b_cnt, slp_typ_a, slp_typ_b);
    return 0;
}

int acpi_can_power_off(void)
{
    return ready;
}

void acpi_power_off(void)
{
    if (!ready)
        return;
    /* Into ACPI mode first, if the firmware left it in legacy (SMM) mode. */
    if (!(inw(pm1a_cnt) & SCI_EN) && smi_cmd && acpi_enable) {
        outb((uint16_t)smi_cmd, acpi_enable);
        uint32_t t0 = uptime_ms();
        while (!(inw(pm1a_cnt) & SCI_EN) && uptime_ms() - t0 < 3000)
            __asm__ volatile("pause");
    }
    outw(pm1a_cnt, (uint16_t)(slp_typ_a << 10 | SLP_EN));
    if (pm1b_cnt)
        outw(pm1b_cnt, (uint16_t)(slp_typ_b << 10 | SLP_EN));
    uint32_t t0 = uptime_ms();
    while (uptime_ms() - t0 < 1000)     /* it takes a moment */
        __asm__ volatile("pause");
}
