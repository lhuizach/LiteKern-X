#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/string.h"

#define FRAMES      (PMM_LIMIT / PAGE_SIZE)
#define LOW_LIMIT   0x100000u       /* BIOS, bootloader, VGA: never handed out */
#define MIB4        0x400000u

extern char __kernel_start[], __kernel_end[];

static uint32_t bitmap[FRAMES / 32];    /* bit set = frame in use (or not RAM) */
static uint32_t total, free_count, next_hint, map_end;

static int is_used(uint32_t frame) { return bitmap[frame / 32] & (1u << (frame % 32)); }
static void set_used(uint32_t frame) { bitmap[frame / 32] |= 1u << (frame % 32); }
static void set_free(uint32_t frame) { bitmap[frame / 32] &= ~(1u << (frame % 32)); }

void pmm_init(const struct boot_info *bi)
{
    const struct e820_entry *e = (const struct e820_entry *)bi->mmap_addr;
    uint64_t ram_end = 0;

    memset(bitmap, 0xff, sizeof(bitmap));
    for (uint32_t i = 0; i < bi->mmap_count; i++) {
        if (e[i].type != 1)
            continue;
        uint64_t start = e[i].base, end = e[i].base + e[i].length;
        if (end > ram_end)
            ram_end = end;
        if (start < LOW_LIMIT)
            start = LOW_LIMIT;
        if (end > PMM_LIMIT)
            end = PMM_LIMIT;
        start = (start + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
        end &= ~(uint64_t)(PAGE_SIZE - 1);
        for (uint64_t a = start; a < end; a += PAGE_SIZE) {
            set_free((uint32_t)(a / PAGE_SIZE));
            free_count++;
        }
    }

    /* The kernel image and bss (both page-aligned by the linker script). */
    for (uint32_t a = (uint32_t)__kernel_start; a < (uint32_t)__kernel_end; a += PAGE_SIZE) {
        if (!is_used(a / PAGE_SIZE)) {
            set_used(a / PAGE_SIZE);
            free_count--;
        }
    }

    total = free_count;
    if (ram_end > PMM_LIMIT)
        ram_end = PMM_LIMIT;
    map_end = (uint32_t)((ram_end + MIB4 - 1) & ~(uint64_t)(MIB4 - 1));
    next_hint = LOW_LIMIT / PAGE_SIZE;
    if (!free_count)
        panic("pmm: no free RAM above 1 MiB");
}

uint32_t pmm_alloc(void)
{
    for (uint32_t n = 0; n < FRAMES; n++) {
        uint32_t frame = (next_hint + n) % FRAMES;
        if (is_used(frame))
            continue;
        set_used(frame);
        free_count--;
        next_hint = frame + 1;
        uint32_t phys = frame * PAGE_SIZE;
        memset((void *)phys, 0, PAGE_SIZE);
        return phys;
    }
    return 0;
}

uint32_t pmm_alloc_contiguous(uint32_t count)
{
    if (!count || count > free_count)
        return 0;
    uint32_t run = 0;
    for (uint32_t frame = LOW_LIMIT / PAGE_SIZE; frame < FRAMES; frame++) {
        run = is_used(frame) ? 0 : run + 1;
        if (run < count)
            continue;
        uint32_t first = frame + 1 - count;
        for (uint32_t f = first; f <= frame; f++)
            set_used(f);
        free_count -= count;
        memset((void *)(first * PAGE_SIZE), 0, count * PAGE_SIZE);
        return first * PAGE_SIZE;
    }
    return 0;
}

void pmm_free(uint32_t phys)
{
    uint32_t frame = phys / PAGE_SIZE;
    if (phys % PAGE_SIZE || phys < LOW_LIMIT || phys >= PMM_LIMIT)
        panic("pmm_free: bad frame 0x%08x", phys);
    if (!is_used(frame))
        panic("pmm_free: double free of frame 0x%08x", phys);
    if (phys >= (uint32_t)__kernel_start && phys < (uint32_t)__kernel_end)
        panic("pmm_free: frame 0x%08x belongs to the kernel image", phys);
    set_free(frame);
    free_count++;
}

uint32_t pmm_free_frames(void) { return free_count; }
uint32_t pmm_total_frames(void) { return total; }
uint32_t pmm_direct_map_end(void) { return map_end; }
