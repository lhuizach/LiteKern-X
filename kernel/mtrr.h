/* LiteKern X — marking a physical range write-combining with an MTRR.
 *
 * Firmware usually leaves the graphics aperture uncached, so every pixel
 * written to the framebuffer is its own bus transaction. v1 measured
 * write-combining as roughly 10-30x faster on the EeePC. Ported from v1's
 * drivers/fb.c, with two changes: it follows the Intel SDM (Vol. 3, 11.11.8)
 * update sequence (caches off and flushed, MTRRs disabled, write, re-enable)
 * instead of writing the MSRs live, and it refuses a range that, once rounded
 * to MTRR alignment, would touch usable RAM. */
#ifndef LKX_MTRR_H
#define LKX_MTRR_H

#include <stdint.h>
#include "boot/bootinfo.h"

enum mtrr_result {
    MTRR_OK,
    MTRR_NOT_SUPPORTED,     /* no MTRRs, or no variable ranges */
    MTRR_NO_FREE_SLOT,      /* every variable range is already in use */
    MTRR_UNSAFE_RANGE,      /* rounded range would cover usable RAM */
};

/* Make [phys, phys+size) write-combining. On MTRR_OK, base and len receive
 * the (power-of-two, aligned) range actually covered. Interrupts must be off. */
enum mtrr_result mtrr_set_write_combining(uint32_t phys, uint32_t size,
                                          const struct boot_info *bi,
                                          uint32_t *base, uint32_t *len);

const char *mtrr_result_name(enum mtrr_result r);

#endif
