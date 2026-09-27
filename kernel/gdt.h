/* LiteKern X — global descriptor table: flat ring 0 and ring 3 segments, plus
 * the TSS that gives ring 3 -> ring 0 transitions a kernel stack (Phase 1 §5).
 * Selectors for ring 3 carry RPL 3 (| 3). */
#ifndef LKX_GDT_H
#define LKX_GDT_H

#define GDT_KCODE 0x08
#define GDT_KDATA 0x10
#define GDT_UCODE 0x18
#define GDT_UDATA 0x20
#define GDT_TSS   0x28

#ifndef __ASSEMBLER__
void gdt_init(void);
#endif

#endif
