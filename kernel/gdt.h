/* LiteKern X — global descriptor table. Flat ring 0 segments for now; user
 * segments and the TSS arrive with ring separation (Phase 1 §5). */
#ifndef LKX_GDT_H
#define LKX_GDT_H

#define GDT_KCODE 0x08
#define GDT_KDATA 0x10

void gdt_init(void);

#endif
